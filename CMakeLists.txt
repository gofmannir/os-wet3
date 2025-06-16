cmake_minimum_required(VERSION 3.15)
project(MyCApp C)

# Compile all your .c files into one executable
add_executable(server
        log.c
        request.c
        segel.c
        server.c
        # …add as many as you need
)
# If you have headers in sub-dirs, you can add:
# target_include_directories(my_app PRIVATE ${CMAKE_SOURCE_DIR}/include)