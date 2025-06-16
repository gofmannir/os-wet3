# test_server.py
import pytest
import requests
from requests_futures.sessions import FuturesSession
from concurrent.futures import ThreadPoolExecutor
import time
import os
import re  # For parsing headers

# Define parameter sets for server configurations
# These tuples will be (threads, queue_size)
server_configurations = [
    {"threads": 1, "queue_size": 1, "id": "t1_q1"},
    {"threads": 2, "queue_size": 1, "id": "t2_q1"},
    {"threads": 4, "queue_size": 20, "id": "t4_q20"},
    {"threads": 8, "queue_size": 16, "id": "t8_q16"},
    {"threads": 16, "queue_size": 4, "id": "t16_q4"},
]


def parse_stats_headers(headers):
    """Helper function to parse Stat-* headers into a dictionary."""
    stats = {}
    for key, value in headers.items():
        if key.startswith("Stat-"):
            # Clean up key name for dictionary
            # print("value:", value)
            stat_key = key.replace("Stat-", "").replace("-", "_").lower()
            # Attempt to convert numeric-like values
            if ":" in value:  # Handles the double colon in spec
                actual_value = value.split(":")[1].strip()
            else:
                actual_value = value.strip()

            # if re.match(r"^\d+\.\d{6}$", actual_value):  # timeval format
            #     parts = actual_value.split('.')
            #     stats[stat_key] = (int(parts), int(parts))
            if actual_value.isdigit():
                stats[stat_key] = int(actual_value)
            else:
                # Store as string if not clearly numeric
                stats[stat_key] = actual_value
    return stats

# Parametrize tests to run with different server configurations


@pytest.mark.parametrize("managed_server", server_configurations,
                         indirect=True, ids=[c["id"] for c in server_configurations])
class TestWebServer:
    BASE_URL_FORMAT = "http://{host}:{port}"

    def test_get_home_html(self, managed_server):
        """Test basic GET request for home.html and validate common headers."""
        base_url = self.BASE_URL_FORMAT.format(**managed_server)
        response = requests.get(f"{base_url}/home.html")

        assert response.status_code == 200
        assert response.headers["Content-Type"] == "text/html"  # As per spec example
        # Add assertion for Content-Length if file size is known/stable
        # Add assertion for body content if necessary

        stats = parse_stats_headers(response.headers)
        assert "req_arrival" in stats
        assert "req_dispatch" in stats
        assert "thread_id" in stats
        # assert stats["thread_id"] >= 1 and stats["thread_id"] <= managed_server["threads"]
        assert "thread_count" in stats
        assert "thread_static" in stats  # home.html is static
        assert "thread_dynamic" in stats
        assert "thread_post" in stats

    def test_404_not_found(self, managed_server):
        """Test response for a non-existent file."""
        base_url = self.BASE_URL_FORMAT.format(**managed_server)
        response_before = requests.get(
            f"{base_url}/home.html")  # Get initial stats
        stats_before = parse_stats_headers(response_before.headers)
        thread_id_to_check = stats_before["thread_id"]
        count_before = stats_before["thread_count"]
        static_before = stats_before["thread_static"]
        dynamic_before = stats_before["thread_dynamic"]
        post_before = stats_before["thread_post"]

        response_404 = requests.get(f"{base_url}/non_existent_file.html")
        assert response_404.status_code == 404

        # Make another valid request to the same thread if possible, or analyze general stats
        # This is tricky with multiple threads; for simplicity, assume we might hit same thread
        # or that total counts are what we check.
        # A more robust test might use a 1-thread server config for this specific check.
        # For now, let's assume we check the headers of the 404 response itself.
        stats_404 = parse_stats_headers(response_404.headers)
        if stats_404["thread_id"] == thread_id_to_check:
            # Incremented for the 404
            assert int(stats_404["thread_count"]) == int(count_before) + 1
            # Not incremented for 404
            assert stats_404["thread_static"] == static_before
            # Not incremented
            assert stats_404["thread_dynamic"] == dynamic_before
            assert stats_404["thread_post"] == post_before  # Not incremented
        # else: this request was handled by a different thread, comparison is
        # harder.

    def test_501_not_implemented(self, managed_server):
        """Test response for an unsupported HTTP method."""
        base_url = self.BASE_URL_FORMAT.format(**managed_server)
        response = requests.request(
            "UNSUPPORTED_METHOD",
            f"{base_url}/home.html")
        assert response.status_code == 501
        # Add stat counter checks similar to test_404_not_found

    # --- Queue and Concurrency Tests ---
    def test_queue_full_behavior(self, managed_server):
        """
        Tests server behavior when the request queue is expected to fill.
        Sends more concurrent requests than queue_size + threads.
        Uses a CGI script that sleeps to ensure workers are busy.
        """
        if not os.path.exists(
                "./output.cgi"):  # Assumes output.cgi is available for this test
            pytest.skip("output.cgi not found, skipping queue_full test")

        base_url = self.BASE_URL_FORMAT.format(**managed_server)
        num_threads = managed_server["threads"]
        queue_size = managed_server["queue_size"]

        # Number of requests to send: more than can be immediately processed or queued
        # Each CGI call should sleep for a bit to ensure workers are occupied
        # and queue has time to fill.
        num_requests = num_threads + queue_size + 5

        # Use a session with enough workers to send all requests concurrently
        with FuturesSession(executor=ThreadPoolExecutor(max_workers=num_requests)) as session:
            futures = []
            for i in range(num_requests):
                # Assuming output.cgi takes a 'sleep' parameter
                # If not, use a CGI script that inherently takes time
                future = session.get(f"{base_url}/output.cgi?value={i}&sleep=1")
                futures.append(future)

            completed_count = 0
            error_count = 0
            max_dispatch_sec = 0

            start_time = time.time()
            for future in futures:
                try:
                    response = future.result(timeout=10)  # Generous timeout
                    if response.status_code == 200:
                        completed_count += 1
                        stats = parse_stats_headers(response.headers)
                        dispatch_sec, dispatch_usec = stats.get(
                            "req_dispatch", (0, 0))
                        max_dispatch_sec = max(
                            max_dispatch_sec, dispatch_sec + dispatch_usec / 1_000_000.0)
                    else:
                        error_count += 1
                except Exception as e:  # Catches timeouts, connection errors
                    # print(f"Request failed or timed out: {e}")
                    error_count += 1

        elapsed_time = time.time() - start_time
        # print(f"Queue test: {completed_count} completed, {error_count}
        # errors/timeouts. Max dispatch: {max_dispatch_sec:.3f}s. Total time:
        # {elapsed_time:.2f}s")

        # Assertions:
        # 1. Server should not crash (implied if tests continue).
        # 2. A significant number of requests should complete successfully.
        # Perfect completion is not guaranteed if client timeouts are shorter
        # than server processing.
        # Heuristic: at least half should eventually pass
        assert completed_count > (num_threads + queue_size) / 2
        # 3. Some requests should show higher dispatch times if queue was full.
        #    This is a soft check; exact values depend on many factors.
        # If queue_size is very small, dispatch times for later requests should
        # be > 0.
        # Only for small configurations where effect is clear
        if queue_size <= 2 and num_threads <= 2 and completed_count > 0:
            # Expect some delay if queueing happened. Adjust based on CGI sleep
            # time.
            assert max_dispatch_sec > 0.1

    # --- Server Log Tests ---
    # def test_get_appends_to_log_and_post_retrieves(self, managed_server):
    #     """Test that GET appends to log and POST retrieves it."""
    #     base_url = self.BASE_URL_FORMAT.format(**managed_server)

    #     # Make a GET request
    #     get_response = requests.get(f"{base_url}/home.html")
    #     assert get_response.status_code == 200
    #     get_stats = parse_stats_headers(get_response.headers)

    #     # Construct the expected log entry string from the GET response's stats
    #     # This needs to precisely match the server's append_stats format
    #     # Example: "Stat-Req-Arrival:: %ld.%06ld\r\nStat-Req-Dispatch:: %ld.%06ld\r\n..."
    #     # This part is complex due to exact formatting including \r\n
    #     expected_log_entry_parts =}.{get_stats['req_arrival']: 06d}\r\n",
    #         f"Stat-Req-Dispatch:: {get_stats['req_dispatch']}.{get_stats['req_dispatch']:06d}\r\n",
    #         f"Stat-Thread-Id:: {get_stats['thread_id']}\r\n",
    #         f"Stat-Thread-Count:: {get_stats['thread_count']}\r\n",
    #         f"Stat-Thread-Static:: {get_stats['thread_static']}\r\n",
    #         f"Stat-Thread-Dynamic:: {get_stats['thread_dynamic']}\r\n",
    #         # Double \r\n at the end
    #         f"Stat-Thread-Post:: {get_stats['thread_post']}\r\n\r\n"
    #     ]
    #     expected_log_entry= "".join(expected_log_entry_parts)

    #     # Make a POST request to retrieve the log
    #     post_response= requests.post(f"{base_url}/any_path_for_post") # Path for POST might not matter
    #     assert post_response.status_code == 200

    #     # The server spec says "Log is not implemented" for the base server's POST.
    #     # For the student's server, it should return the log.
    #     # The initial log might be empty or contain previous test entries depending on server state
    #     # and fixture scope. If fixture is 'function' scope, log should be relatively clean.
    #     # For simplicity, we check if our specific GET's entry is present.
    #     assert expected_log_entry in post_response.text

    # Add more tests for FIFO, writer priority (more complex), other stat
    # details etc.
