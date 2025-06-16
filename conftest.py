# conftest.py
import pytest
import requests
import subprocess
import socket
import time
import os
import signal

SERVER_EXECUTABLE = "./server"  # Path to the server executable
DEFAULT_HOST = "localhost"


@pytest.fixture(scope="session")
def free_port():
    """Dynamically find and reserve a free TCP port."""
    # with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    #     s.bind((DEFAULT_HOST, 0))
    #     _, port = s.getsockname()  # Extract the port from the tuple
    #     return port
    return 7777


# Scope can be 'module' if server setup is slow
@pytest.fixture(scope="function")
def managed_server(request, free_port):
    """
    Manages the lifecycle of the web server for a test.
    Parametrized by 'threads' and 'queue_size'.
    Logs server output to a file named based on parameters.
    """
    threads = request.param.get(
        "threads", 4)  # Default values if not parametrized
    queue_size = request.param.get("queue_size", 8)
    port = free_port

    log_filename = (
        f"server_log_threads_{threads} _queue_{queue_size} _port_{port}.log")
    server_process = None
    log_file = None

    try:
        # Ensure server executable exists
        if not os.path.exists(SERVER_EXECUTABLE):
            pytest.fail(f"Server executable not found at {SERVER_EXECUTABLE}")
        if not os.access(SERVER_EXECUTABLE, os.X_OK):
            pytest.fail(
                f"Server executable at {SERVER_EXECUTABLE} is not executable.")

        # Command to start the server
        server_cmd = [
            SERVER_EXECUTABLE,
            str(port),
            str(threads),
            str(queue_size)]

        # Open log file for server's stdout and stderr
        log_file = open(log_filename, "w")

        # Start the server process
        server_process = subprocess.Popen(
            server_cmd, stdout=log_file, stderr=subprocess.STDOUT)

        # Allow server some time to start and perform a basic health check
        time.sleep(1.0)  # Adjust as needed

        # Health check (optional but recommended)
        try:
            print(
                f"making health check on http://{DEFAULT_HOST}:{port}/home.html")
            # time.sleep(20)
            response = requests.get(
                f"http://{DEFAULT_HOST}:{port}/home.html", timeout=1)
            if response.status_code != 200:
                raise ConnectionError(
                    "Server did not respond with 200 OK to health check.")
        except requests.exceptions.RequestException as e:
            pytest.fail(
                f"Server health check failed: {e}. Check server log: {log_filename}")

        yield {"host": DEFAULT_HOST, "port": port, "threads": threads, "queue_size": queue_size}

    finally:
        if server_process:
            # Terminate the server process
            server_process.terminate()
            try:
                server_process.wait(timeout=5)  # Wait for graceful termination
            except subprocess.TimeoutExpired:
                server_process.kill()  # Force kill if terminate fails
                server_process.wait()  # Ensure process is reaped
        if log_file:
            log_file.close()
        # print(f"Server logs available at: {log_filename}") # Optional: print
        # log location
