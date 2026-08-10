cmake_minimum_required(VERSION 3.20)
project(framelink LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FRAMELINK_BUILD_TESTS "Build the framelink self-tests" ON)

if(WIN32)
    add_library(framelink SHARED
        src/framelink_win.cpp
        src/fl_ipc_win32.cpp)
    target_link_libraries(framelink PRIVATE d3d11 dxgi)
    target_compile_definitions(framelink PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # The dma-buf backend (Linux) and AHardwareBuffer backend (Android) are not
    # written yet; the wire format and API already account for them.
    message(FATAL_ERROR "framelink currently has a Windows backend only")
endif()

target_include_directories(framelink
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(framelink PROPERTIES
    VERSION ${PROJECT_VERSION}
    PUBLIC_HEADER "include/framelink.h;include/framelink_d3d11.h")

if(FRAMELINK_BUILD_TESTS)
    # Two processes, because that is the only way to test what this library
    # does. fl_consume owns the ring and verifies pixels; fl_produce attaches
    # and draws.
    add_executable(fl_consume test/fl_consume.cpp)
    target_link_libraries(fl_consume PRIVATE framelink d3d11)
    add_executable(fl_produce test/fl_produce.cpp)
    target_link_libraries(fl_produce PRIVATE framelink d3d11)

    enable_testing()
    # The consumer is the test: it exits non-zero on any pixel mismatch. The
    # producer is started by the fixture below.
    add_test(NAME roundtrip
             COMMAND ${CMAKE_COMMAND}
                     -DCONSUMER=$<TARGET_FILE:fl_consume>
                     -DPRODUCER=$<TARGET_FILE:fl_produce>
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_roundtrip.cmake)
endif()

include(GNUInstallDirs)
install(TARGETS framelink
        EXPORT framelinkTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
