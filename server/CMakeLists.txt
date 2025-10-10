cmake_minimum_required(VERSION 3.16)
project(HabboCloneServer)

# ==========================
# C++ Version
# ==========================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================
# Build Type (Debug/Release)
# ==========================
# Default to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

message(STATUS "🔧 Build type: ${CMAKE_BUILD_TYPE}")

# Optional: set custom flags for each build type
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ==========================
# Paths to third-party libs
# ==========================

# uWebSockets
set(UWEBSOCKETS_DIR "${CMAKE_SOURCE_DIR}/../third_party/uWebSockets")
set(USOCKETS_LIB "${UWEBSOCKETS_DIR}/uSockets/libuSockets.a")

if(NOT EXISTS ${USOCKETS_LIB})
    message(FATAL_ERROR "libuSockets.a not found. Go to ${UWEBSOCKETS_DIR}/uSockets and run: make && ar rcs libuSockets.a *.o")
endif()

# Bcrypt
set(BCRYPT_DIR "${CMAKE_SOURCE_DIR}/../third_party/bcrypt")
set(BCRYPT_INCLUDE_DIR "${BCRYPT_DIR}/include/bcrypt")
set(BCRYPT_SRC
    "${BCRYPT_DIR}/src/bcrypt.cpp"
    "${BCRYPT_DIR}/src/blowfish.cpp"
)

# ==========================
# Include directories
# ==========================
include_directories(
    ${UWEBSOCKETS_DIR}/src
    ${UWEBSOCKETS_DIR}/uSockets/src
    ${UWEBSOCKETS_DIR}/uSockets/boringssl/include
    ${BCRYPT_INCLUDE_DIR}
    ${BCRYPT_DIR}/include
    ${BCRYPT_DIR}/src

    # Project internal includes
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/entities
    ${CMAKE_SOURCE_DIR}/network
    ${CMAKE_SOURCE_DIR}/security
    ${CMAKE_SOURCE_DIR}/data
)

# ==========================
# Collect all .cpp and .hpp files
# ==========================
file(GLOB_RECURSE PROJECT_SOURCES
    "${CMAKE_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_SOURCE_DIR}/entities/*.cpp"
    "${CMAKE_SOURCE_DIR}/network/*.cpp"
    "${CMAKE_SOURCE_DIR}/security/*.cpp"
    "${CMAKE_SOURCE_DIR}/data/*.cpp"
)

file(GLOB_RECURSE PROJECT_HEADERS
    "${CMAKE_SOURCE_DIR}/core/*.hpp"
    "${CMAKE_SOURCE_DIR}/entities/*.hpp"
    "${CMAKE_SOURCE_DIR}/network/*.hpp"
    "${CMAKE_SOURCE_DIR}/security/*.hpp"
    "${CMAKE_SOURCE_DIR}/data/*.hpp"
)

# ==========================
# Add executable
# ==========================
add_executable(habbo_server
    main.cpp
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
    ${BCRYPT_SRC}
)

# ==========================
# Link libraries
# ==========================
add_library(uSockets STATIC IMPORTED)
set_target_properties(uSockets PROPERTIES
    IMPORTED_LOCATION ${USOCKETS_LIB}
)

target_link_libraries(habbo_server
    uSockets
    ssl
    crypto
    z
    pthread
    pqxx
)

# ==========================
# Output settings
# ==========================
set_target_properties(habbo_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

message(STATUS "✅ HabboCloneServer configured successfully with automatic build-type handling!")
