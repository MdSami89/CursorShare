cmake_minimum_required(VERSION 3.20)
project(CursorShare VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Static MSVC runtime — makes .exe files self-contained (no vcredist needed)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /WX- /MP /permissive- /await)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
    # Release optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# C++/WinRT — find SDK cppwinrt headers
if(DEFINED ENV{WindowsSdkDir})
    set(WINSDK_DIR $ENV{WindowsSdkDir})
else()
    set(WINSDK_DIR "C:/Program Files (x86)/Windows Kits/10")
endif()
if(DEFINED ENV{WindowsSDKVersion})
    string(REGEX REPLACE "\\\\$" "" WINSDK_VER $ENV{WindowsSDKVersion})
else()
    set(WINSDK_VER "10.0.26100.0")
endif()
set(CPPWINRT_INCLUDE "${WINSDK_DIR}/Include/${WINSDK_VER}/cppwinrt")
if(EXISTS "${CPPWINRT_INCLUDE}")
    message(STATUS "C++/WinRT headers found: ${CPPWINRT_INCLUDE}")
else()
    message(WARNING "C++/WinRT headers not found at ${CPPWINRT_INCLUDE}")
endif()

# ===========================================================================
# Common headers (header-only library)
# ===========================================================================
add_library(CursorShareCommon INTERFACE)
target_include_directories(CursorShareCommon INTERFACE ${CMAKE_SOURCE_DIR}/src)

# ===========================================================================
# Logger (static library — used by all components)
# ===========================================================================
add_library(CursorShareLogger STATIC
    src/common/logger.cpp
)
target_include_directories(CursorShareLogger PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(CursorShareLogger PUBLIC CursorShareCommon)

# ===========================================================================
# Bluetooth subsystem
# ===========================================================================
add_library(CursorShareBluetooth STATIC
    src/bluetooth/bt_validator.cpp
    src/bluetooth/bt_hid_service.cpp
    src/bluetooth/bt_pairing.cpp
    src/bluetooth/ble_hid_service.cpp
)
target_include_directories(CursorShareBluetooth PUBLIC ${CPPWINRT_INCLUDE})
target_link_libraries(CursorShareBluetooth
    PUBLIC CursorShareCommon CursorShareLogger
    PRIVATE Bthprops Ws2_32 SetupAPI WindowsApp
)

# ===========================================================================
# Input subsystem
# ===========================================================================
add_library(CursorShareInput STATIC
    src/input/raw_input_capture.cpp
    src/input/input_router.cpp
    src/input/shortcut_manager.cpp
    src/input/mouse_boundary.cpp
)
target_link_libraries(CursorShareInput
    PUBLIC CursorShareCommon
)

# ===========================================================================
# Service
# ===========================================================================
add_library(CursorShareServiceLib STATIC
    src/service/cursorshare_service.cpp
    src/service/latency_monitor.cpp
)
target_link_libraries(CursorShareServiceLib
    PUBLIC CursorShareCommon CursorShareBluetooth CursorShareInput CursorShareLogger
)

# ===========================================================================
# Main executable
# ===========================================================================
add_executable(CursorShare src/main.cpp)
target_link_libraries(CursorShare
    PRIVATE CursorShareServiceLib
)

# ===========================================================================
# Bluetooth diagnostic tool
# ===========================================================================
add_executable(BtDiagnostic tools/bt_diagnostic.cpp)
target_link_libraries(BtDiagnostic
    PRIVATE CursorShareBluetooth
)

# ===========================================================================
# Latency benchmark tool
# ===========================================================================
add_executable(LatencyBenchmark tools/latency_benchmark.cpp)
target_link_libraries(LatencyBenchmark
    PRIVATE CursorShareServiceLib
)

# ===========================================================================
# Tests (optional, requires Google Test)
# ===========================================================================
option(BUILD_TESTS "Build test suite" OFF)

if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    add_executable(CursorShareTests
        tests/ring_buffer_tests.cpp
        tests/hid_descriptor_tests.cpp
        tests/input_router_tests.cpp
        tests/mouse_boundary_tests.cpp
    )
    target_link_libraries(CursorShareTests
        PRIVATE CursorShareServiceLib GTest::GTest GTest::Main
    )
    add_test(NAME CursorShareTests COMMAND CursorShareTests)
endif()

# ===========================================================================
# Install
# ===========================================================================
install(TARGETS CursorShare BtDiagnostic LatencyBenchmark
    RUNTIME DESTINATION bin
)

# ===========================================================================
# Note: KMDF drivers are NOT built via CMake.
# Use Visual Studio with WDK for driver projects:
#   src/driver/kbfilter/
#   src/driver/moufilter/
#   src/btdriver/
# ===========================================================================
