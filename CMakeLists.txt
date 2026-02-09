cmake_minimum_required(VERSION 3.16)
project(QuantumFlow LANGUAGES CXX C)

# ─────────────────────────────────────────────
# C++ Standard
# ─────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────
# Build type
# ─────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ─────────────────────────────────────────────
# SIMD Detection (from Graphics Engine)
# ─────────────────────────────────────────────
include(CheckCXXCompilerFlag)

set(SIMD_FLAGS)
check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512)
if(COMPILER_SUPPORTS_AVX512)
    list(APPEND SIMD_FLAGS -mavx512f -mavx512dq -mavx512bw -mavx512vl)
    message(STATUS "SIMD: AVX-512 enabled")
else()
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    check_cxx_compiler_flag("-mfma" COMPILER_SUPPORTS_FMA)
    if(COMPILER_SUPPORTS_AVX2)
        list(APPEND SIMD_FLAGS -mavx2)
        if(COMPILER_SUPPORTS_FMA)
            list(APPEND SIMD_FLAGS -mfma)
        endif()
        message(STATUS "SIMD: AVX2 + FMA enabled")
    else()
        check_cxx_compiler_flag("-msse4.1" COMPILER_SUPPORTS_SSE41)
        if(COMPILER_SUPPORTS_SSE41)
            list(APPEND SIMD_FLAGS -msse4.1)
            message(STATUS "SIMD: SSE4.1 enabled")
        else()
            check_cxx_compiler_flag("-msse2" COMPILER_SUPPORTS_SSE2)
            if(COMPILER_SUPPORTS_SSE2)
                list(APPEND SIMD_FLAGS -msse2)
                message(STATUS "SIMD: SSE2 enabled")
            else()
                message(STATUS "SIMD: Scalar fallback")
            endif()
        endif()
    endif()
endif()

# ─────────────────────────────────────────────
# Optimization flags
# ─────────────────────────────────────────────
set(OPT_FLAGS)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND OPT_FLAGS
        -O3
        -ffast-math
        -funroll-loops
        -ftree-vectorize
        -march=native
    )
endif()

# ─────────────────────────────────────────────
# Options
# ─────────────────────────────────────────────
option(QUANTUMFLOW_BUILD_WEBUI "Build with WebSocket UI support" ON)
option(QUANTUMFLOW_BUILD_BRIDGE "Build PyBind11 bridge module" ON)
option(QUANTUMFLOW_BUILD_TESTS "Build test targets" ON)
option(QUANTUMFLOW_BUILD_BENCHMARKS "Build benchmark targets" ON)

# ─────────────────────────────────────────────
# Third-party dependencies
# ─────────────────────────────────────────────
add_subdirectory(third_party)

# ─────────────────────────────────────────────
# Include directories (project root for #include paths)
# ─────────────────────────────────────────────
set(QF_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ═══════════════════════════════════════════════
# ORDER BOOK LIBRARY (from High_Performance_LOB)
# ═══════════════════════════════════════════════
add_library(orderbook_lib STATIC
    orderbook/src/Book.cpp
    orderbook/src/Level.cpp
    orderbook/src/Order.cpp
)

target_include_directories(orderbook_lib PUBLIC
    ${QF_ROOT}/orderbook/include
)

target_compile_options(orderbook_lib PRIVATE ${OPT_FLAGS})

# ═══════════════════════════════════════════════
# GRAPHICS ENGINE LIBRARY (from HP_Graphics_Engine)
# ═══════════════════════════════════════════════
add_library(graphics_lib STATIC
    graphics/src/simd_math.cpp
    graphics/src/pipeline.cpp
    graphics/src/vertex_manager.cpp
    graphics/src/profiler.cpp
)

target_include_directories(graphics_lib PUBLIC
    ${QF_ROOT}/graphics/include
)

target_compile_options(graphics_lib PRIVATE ${SIMD_FLAGS} ${OPT_FLAGS})
target_link_libraries(graphics_lib PUBLIC pthread)

# ═══════════════════════════════════════════════
# STRATEGIES LIBRARY
# ═══════════════════════════════════════════════
add_library(strategies_lib STATIC
    strategies/strategy_base.cpp
    strategies/strategy_engine.cpp
)

target_include_directories(strategies_lib PUBLIC
    ${QF_ROOT}
)

target_link_libraries(strategies_lib PUBLIC orderbook_lib graphics_lib)
target_compile_options(strategies_lib PRIVATE ${OPT_FLAGS})

# ═══════════════════════════════════════════════
# WEBSOCKET SERVER LIBRARY (only if building WebUI)
# ═══════════════════════════════════════════════
if(QUANTUMFLOW_BUILD_WEBUI)
    add_library(ws_server_lib STATIC
        ws/ws_server.cpp
        ws/json_serializer.cpp
    )

    target_include_directories(ws_server_lib PUBLIC ${QF_ROOT})
    target_link_libraries(ws_server_lib PUBLIC
        strategies_lib
        uwebsockets
        nlohmann_json::nlohmann_json
    )
    target_compile_options(ws_server_lib PRIVATE ${OPT_FLAGS})
endif()

# ═══════════════════════════════════════════════
# MAIN EXECUTABLE
# ═══════════════════════════════════════════════
if(QUANTUMFLOW_BUILD_WEBUI)
    add_executable(quantumflow main.cpp)

    target_include_directories(quantumflow PRIVATE ${QF_ROOT})
    target_link_libraries(quantumflow PRIVATE
        strategies_lib
        orderbook_lib
        graphics_lib
        ws_server_lib
    )
    target_compile_options(quantumflow PRIVATE ${OPT_FLAGS})
else()
    add_executable(quantumflow main.cpp)
    target_include_directories(quantumflow PRIVATE ${QF_ROOT})
    target_compile_definitions(quantumflow PRIVATE QUANTUMFLOW_HEADLESS)
    target_link_libraries(quantumflow PRIVATE
        strategies_lib
        orderbook_lib
        graphics_lib
    )
    target_compile_options(quantumflow PRIVATE ${OPT_FLAGS})
endif()

# Headless-only build (no WebUI dependency)
add_executable(quantumflow_headless main.cpp)
target_include_directories(quantumflow_headless PRIVATE ${QF_ROOT})
target_compile_definitions(quantumflow_headless PRIVATE QUANTUMFLOW_HEADLESS)
target_link_libraries(quantumflow_headless PRIVATE
    strategies_lib
    orderbook_lib
    graphics_lib
)
target_compile_options(quantumflow_headless PRIVATE ${OPT_FLAGS})

# ═══════════════════════════════════════════════
# PYBIND11 BRIDGE MODULE
# ═══════════════════════════════════════════════
if(QUANTUMFLOW_BUILD_BRIDGE)
    pybind11_add_module(quantumflow_bridge
        bridge/quantumflow_bridge.cpp
    )

    target_include_directories(quantumflow_bridge PRIVATE ${QF_ROOT})
    target_link_libraries(quantumflow_bridge PRIVATE
        orderbook_lib
        graphics_lib
    )
    target_compile_options(quantumflow_bridge PRIVATE ${OPT_FLAGS})
endif()

# ═══════════════════════════════════════════════
# TESTS
# ═══════════════════════════════════════════════
if(QUANTUMFLOW_BUILD_TESTS)
    enable_testing()

    # LOBTest (original order book test)
    add_executable(LOBTest
        orderbook/test/LOBTest.cpp
    )
    target_include_directories(LOBTest PRIVATE ${QF_ROOT}/orderbook/include)
    target_link_libraries(LOBTest PRIVATE orderbook_lib GTest::gtest GTest::gtest_main)
    add_test(NAME LOBTest COMMAND LOBTest)

    # Graphics engine test (original)
    add_executable(test_engine
        graphics/tests/test_engine.cpp
    )
    target_include_directories(test_engine PRIVATE ${QF_ROOT}/graphics/include)
    target_compile_options(test_engine PRIVATE ${SIMD_FLAGS} ${OPT_FLAGS})
    target_link_libraries(test_engine PRIVATE graphics_lib pthread)
    add_test(NAME EngineTests COMMAND test_engine)

    # Price converter test
    add_executable(test_price_converter
        tests/test_price_converter.cpp
    )
    target_include_directories(test_price_converter PRIVATE ${QF_ROOT} ${QF_ROOT}/orderbook/include)
    target_link_libraries(test_price_converter PRIVATE orderbook_lib GTest::gtest GTest::gtest_main)
    add_test(NAME PriceConverterTest COMMAND test_price_converter)

    # Shared memory test
    add_executable(test_shared_memory
        tests/test_shared_memory.cpp
    )
    target_include_directories(test_shared_memory PRIVATE ${QF_ROOT} ${QF_ROOT}/graphics/include)
    target_link_libraries(test_shared_memory PRIVATE graphics_lib GTest::gtest GTest::gtest_main pthread)
    add_test(NAME SharedMemoryTest COMMAND test_shared_memory)

    # Strategies test
    add_executable(test_strategies
        tests/test_strategies.cpp
    )
    target_include_directories(test_strategies PRIVATE ${QF_ROOT} ${QF_ROOT}/orderbook/include)
    target_link_libraries(test_strategies PRIVATE strategies_lib GTest::gtest GTest::gtest_main)
    add_test(NAME StrategiesTest COMMAND test_strategies)
endif()

# ═══════════════════════════════════════════════
# BENCHMARKS
# ═══════════════════════════════════════════════
if(QUANTUMFLOW_BUILD_BENCHMARKS)
    # LOB benchmark
    add_executable(LOBBench
        orderbook/bench/bench_orderbook.cpp
    )
    target_include_directories(LOBBench PRIVATE ${QF_ROOT}/orderbook/include)
    target_link_libraries(LOBBench PRIVATE orderbook_lib)
    target_compile_options(LOBBench PRIVATE ${OPT_FLAGS})

    # Graphics benchmark
    add_executable(graphics_benchmark
        graphics/tests/benchmark.cpp
    )
    target_include_directories(graphics_benchmark PRIVATE ${QF_ROOT}/graphics/include)
    target_compile_options(graphics_benchmark PRIVATE ${SIMD_FLAGS} ${OPT_FLAGS})
    target_link_libraries(graphics_benchmark PRIVATE graphics_lib pthread)
endif()

# ─────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────
message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════")
message(STATUS "  QuantumFlow Trading Engine Configuration")
message(STATUS "═══════════════════════════════════════════════════")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  SIMD flags:     ${SIMD_FLAGS}")
message(STATUS "  WebUI:          ${QUANTUMFLOW_BUILD_WEBUI}")
message(STATUS "  PyBind11:       ${QUANTUMFLOW_BUILD_BRIDGE}")
message(STATUS "  Tests:          ${QUANTUMFLOW_BUILD_TESTS}")
message(STATUS "  Benchmarks:     ${QUANTUMFLOW_BUILD_BENCHMARKS}")
message(STATUS "═══════════════════════════════════════════════════")
message(STATUS "")
