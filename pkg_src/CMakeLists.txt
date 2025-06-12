cmake_minimum_required(VERSION 3.16)
project(ur-wireless-tools C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Common flags
add_compile_options(-Wall -Wextra -pthread -Wno-stringop-truncation)
add_compile_options(-Wno-unused-result -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -std=gnu99 -pthread -O2)

# Add definitions for Linux wireless support
add_definitions(-D_GNU_SOURCE -DUSE_WEXT)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/main.c
    src/wifi_scanner.c
    src/scan_alternatives.c
    src/interface_detector.c
    src/json_formatter.c
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} pthread m)

