include(FetchContent)

set(QF_THIRD_PARTY_DIR "${CMAKE_CURRENT_LIST_DIR}")

# ─────────────────────────────────────────────
# uWebSockets + uSockets + nlohmann/json
# ─────────────────────────────────────────────
if(QUANTUMFLOW_BUILD_WEBUI)
    # nlohmann/json (header-only)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(nlohmann_json)

    FetchContent_Declare(
        usockets
        GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
        GIT_TAG        v0.8.8
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(usockets)
    if(NOT usockets_POPULATED)
        FetchContent_Populate(usockets)
    endif()

    add_library(usockets STATIC
        ${usockets_SOURCE_DIR}/src/eventing/epoll_kqueue.c
        ${usockets_SOURCE_DIR}/src/eventing/gcd.c
        ${usockets_SOURCE_DIR}/src/eventing/libuv.c
        ${usockets_SOURCE_DIR}/src/context.c
        ${usockets_SOURCE_DIR}/src/loop.c
        ${usockets_SOURCE_DIR}/src/socket.c
        ${usockets_SOURCE_DIR}/src/udp.c
        ${usockets_SOURCE_DIR}/src/quic.c
        ${usockets_SOURCE_DIR}/src/bsd.c
        ${usockets_SOURCE_DIR}/src/crypto/openssl.c
        ${usockets_SOURCE_DIR}/src/crypto/sni_tree.cpp
        ${usockets_SOURCE_DIR}/src/io_uring/io_context.c
        ${usockets_SOURCE_DIR}/src/io_uring/io_loop.c
        ${usockets_SOURCE_DIR}/src/io_uring/io_socket.c
    )

    target_include_directories(usockets PUBLIC
        ${usockets_SOURCE_DIR}/src
    )

    target_compile_definitions(usockets PRIVATE LIBUS_NO_SSL)

    FetchContent_Declare(
        uwebsockets
        GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
        GIT_TAG        v20.70.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(uwebsockets)
    if(NOT uwebsockets_POPULATED)
        FetchContent_Populate(uwebsockets)
    endif()

    add_library(uwebsockets INTERFACE)
    target_include_directories(uwebsockets INTERFACE
        ${uwebsockets_SOURCE_DIR}/src
    )
    find_package(ZLIB REQUIRED)
    target_link_libraries(uwebsockets INTERFACE usockets ZLIB::ZLIB)
    target_compile_definitions(uwebsockets INTERFACE LIBUS_NO_SSL)
    target_compile_options(uwebsockets INTERFACE -Wno-stringop-overflow)
endif()

# ─────────────────────────────────────────────
# Google Test
# ─────────────────────────────────────────────
if(QUANTUMFLOW_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()
