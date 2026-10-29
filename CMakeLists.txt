cmake_minimum_required(VERSION 3.20)
project(framelink LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FRAMELINK_BUILD_TESTS "Build the framelink self-tests" ON)
option(FRAMELINK_BUILD_GST "Build the GStreamer elements when GStreamer is found" ON)

if(WIN32)
    add_library(framelink SHARED
        src/framelink_win.cpp
        src/framelink_slots.cpp
        src/fl_ipc_win32.cpp)
    target_link_libraries(framelink PRIVATE d3d11 dxgi)
    target_compile_definitions(framelink PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(UNIX AND NOT ANDROID)
    find_package(PkgConfig REQUIRED)
    # GBM allocates the ring and exports dma-buf fds; libdrm supplies the
    # DRM_FORMAT_* codes. Both are Mesa-side, no GPU vendor SDK involved.
    pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
    pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
    add_library(framelink SHARED
        src/framelink_linux.cpp
        src/framelink_slots.cpp
        src/fl_ipc_posix.cpp)
    target_link_libraries(framelink PRIVATE PkgConfig::GBM PkgConfig::DRM pthread)
    set_target_properties(framelink PROPERTIES
        PUBLIC_HEADER "include/framelink.h;include/framelink_dmabuf.h;include/framelink_slots.h;include/framelink_trace.hpp;include/framelink_sync_fd.h")
elseif(ANDROID)
    # Static: APK native modules embed it, and there is no system loader story
    # for a bare shared library on Android anyway.
    add_library(framelink STATIC
        src/framelink_android.cpp
        src/framelink_slots.cpp
        src/fl_ipc_posix.cpp)
    target_link_libraries(framelink PRIVATE android log)
    set_target_properties(framelink PROPERTIES
        PUBLIC_HEADER "include/framelink.h;include/framelink_ahb.h;include/framelink_slots.h;include/framelink_trace.hpp;include/framelink_sync_fd.h")
else()
    message(FATAL_ERROR "framelink has no backend for this platform yet")
endif()

target_sources(framelink PRIVATE src/fl_trace.cpp)
option(FRAMELINK_DISABLE_TRACE "Compile out all tracing" OFF)
if(FRAMELINK_DISABLE_TRACE)
    add_compile_definitions(FL_TRACE_COMPILED_OUT)
endif()

target_include_directories(framelink
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(framelink PROPERTIES VERSION ${PROJECT_VERSION})
if(WIN32)
    set_target_properties(framelink PROPERTIES
        PUBLIC_HEADER "include/framelink.h;include/framelink_d3d11.h;include/framelink_slots.h;include/framelink_trace.hpp;include/framelink_sync_fd.h")
endif()

if(FRAMELINK_BUILD_TESTS)
    # Two processes, because that is the only way to test what this library
    # does. fl_consume owns the ring and verifies pixels; fl_produce attaches
    # and draws.
    enable_testing()
    if(WIN32)
        # Separate binaries because the D3D11 path draws on the GPU, which is
        # what makes the fence wait meaningful.
        add_executable(fl_consume test/fl_consume.cpp)
        target_link_libraries(fl_consume PRIVATE framelink d3d11)
        add_executable(fl_produce test/fl_produce.cpp)
        target_link_libraries(fl_produce PRIVATE framelink d3d11)
        set(FL_CONSUMER $<TARGET_FILE:fl_consume>)
        set(FL_PRODUCER $<TARGET_FILE:fl_produce>)
        set(FL_CONSUMER_ARGS flci 20)
        set(FL_PRODUCER_ARGS flci 400 0)
    else()
        # One binary, both roles: the POSIX path touches pixels through
        # flImageMap, so there is no GPU code to keep apart.
        add_executable(fl_roundtrip test/fl_roundtrip_posix.cpp)
        target_link_libraries(fl_roundtrip PRIVATE framelink)
        set(FL_CONSUMER $<TARGET_FILE:fl_roundtrip>)
        set(FL_PRODUCER $<TARGET_FILE:fl_roundtrip>)
        set(FL_CONSUMER_ARGS consume flci 20)
        set(FL_PRODUCER_ARGS produce flci 400)
    endif()

    # The consumer is the test: it exits non-zero on any pixel mismatch.
    add_test(NAME roundtrip
             COMMAND ${CMAKE_COMMAND}
                     -DCONSUMER=${FL_CONSUMER} -DPRODUCER=${FL_PRODUCER}
                     "-DCONSUMER_ARGS=${FL_CONSUMER_ARGS}"
                     "-DPRODUCER_ARGS=${FL_PRODUCER_ARGS}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_roundtrip.cmake)

    # Same verification across a producer detach/reattach - the case a channel
    # sees every time one presenter leaves and the next takes the slot. It is a
    # separate test because the failure it guards against (a fence carried over
    # to a producer whose counter restarts at 1) looks nothing like the failure
    # the plain round trip catches.
    if(WIN32)
        add_test(NAME roundtrip_reattach
                 COMMAND ${CMAKE_COMMAND}
                         -DCONSUMER=${FL_CONSUMER} -DPRODUCER=${FL_PRODUCER}
                         "-DCONSUMER_ARGS=flre;20;0;1"
                         "-DPRODUCER_ARGS=flre;400;0;1"
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_roundtrip.cmake)
    endif()
endif()

# --- GStreamer elements (gst/) ----------------------------------------------
# framelinksrc + framelinksink: any GStreamer pipeline as a framelink producer
# or consumer. Optional on purpose - the core library stays dependency-free,
# and the plugin quietly skips itself where GStreamer is absent (Android).
if(FRAMELINK_BUILD_GST AND NOT ANDROID)
    set(FL_GST_FOUND FALSE)
    if(WIN32)
        # Official GStreamer MSVC runtime + development MSIs; the devel MSI
        # sets GSTREAMER_1_0_ROOT_MSVC_X86_64 (same detection vkrender uses).
        set(FL_GST_ROOT "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}")
        foreach(_cand "C:/gstreamer/1.0/msvc_x86_64"
                      "C:/Program Files/gstreamer/1.0/msvc_x86_64")
            if(NOT FL_GST_ROOT AND EXISTS "${_cand}/lib/gstreamer-1.0.lib")
                set(FL_GST_ROOT "${_cand}")
            endif()
        endforeach()
        if(EXISTS "${FL_GST_ROOT}/lib/gstreamer-1.0.lib")
            set(FL_GST_FOUND TRUE)
            add_library(gstframelink MODULE gst/gstframelink.cpp)
            target_include_directories(gstframelink PRIVATE
                "${FL_GST_ROOT}/include/gstreamer-1.0"
                "${FL_GST_ROOT}/include/glib-2.0"
                "${FL_GST_ROOT}/lib/glib-2.0/include"
                "${FL_GST_ROOT}/lib/gstreamer-1.0/include")
            target_link_directories(gstframelink PRIVATE "${FL_GST_ROOT}/lib")
            target_link_libraries(gstframelink PRIVATE framelink d3d11
                gstreamer-1.0.lib gstbase-1.0.lib gstvideo-1.0.lib
                gobject-2.0.lib glib-2.0.lib)
            target_compile_definitions(gstframelink PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(FL_GST QUIET IMPORTED_TARGET
                gstreamer-1.0>=1.20 gstreamer-base-1.0 gstreamer-video-1.0)
            if(FL_GST_FOUND)
                add_library(gstframelink MODULE gst/gstframelink.cpp)
                target_link_libraries(gstframelink PRIVATE framelink PkgConfig::FL_GST)
            endif()
        endif()
    endif()
    if(FL_GST_FOUND)
        target_compile_definitions(gstframelink PRIVATE
            PACKAGE="framelink" PACKAGE_VERSION="${PROJECT_VERSION}")
        message(STATUS "framelink: building GStreamer elements (gstframelink)")
    else()
        message(STATUS "framelink: GStreamer not found - skipping the elements")
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS framelink
        EXPORT framelinkTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(FRAMELINK_BUILD_TESTS)
    add_executable(fl_nv12_layout test/fl_nv12_layout.cpp)
    target_include_directories(fl_nv12_layout PRIVATE include src)
    add_test(NAME nv12_layout COMMAND fl_nv12_layout)
    if(UNIX AND NOT ANDROID)
        add_executable(fl_nv12_roundtrip test/fl_nv12_roundtrip.cpp)
        target_link_libraries(fl_nv12_roundtrip PRIVATE framelink PkgConfig::DRM)
        add_test(NAME nv12_roundtrip COMMAND fl_nv12_roundtrip)
        set_tests_properties(nv12_roundtrip PROPERTIES TIMEOUT 25)
    endif()
endif()

if(FRAMELINK_BUILD_TESTS AND FL_GST_FOUND AND UNIX AND NOT ANDROID)
    pkg_check_modules(FL_GST_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
    add_executable(fl_gst_nv12 test/fl_gst_nv12.cpp)
    target_link_libraries(fl_gst_nv12 PRIVATE PkgConfig::FL_GST PkgConfig::FL_GST_APP)
    add_test(NAME gst_nv12_roundtrip COMMAND fl_gst_nv12 $<TARGET_FILE:gstframelink>)
    add_test(NAME gst_rgb10a2_roundtrip COMMAND fl_gst_nv12 $<TARGET_FILE:gstframelink> rgb10a2)
    set_tests_properties(gst_nv12_roundtrip gst_rgb10a2_roundtrip PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND FL_GST_FOUND AND WIN32)
    add_executable(fl_gst_nv12 test/fl_gst_nv12.cpp)
    target_include_directories(fl_gst_nv12 PRIVATE
        "${FL_GST_ROOT}/include/gstreamer-1.0" "${FL_GST_ROOT}/include/glib-2.0"
        "${FL_GST_ROOT}/lib/glib-2.0/include")
    target_link_directories(fl_gst_nv12 PRIVATE "${FL_GST_ROOT}/lib")
    target_link_libraries(fl_gst_nv12 PRIVATE gstreamer-1.0.lib gstapp-1.0.lib
        gstvideo-1.0.lib gobject-2.0.lib glib-2.0.lib)
    add_test(NAME gst_nv12_roundtrip COMMAND fl_gst_nv12 $<TARGET_FILE:gstframelink>)
    add_test(NAME gst_rgb10a2_roundtrip COMMAND fl_gst_nv12 $<TARGET_FILE:gstframelink> rgb10a2)
    set_tests_properties(gst_nv12_roundtrip gst_rgb10a2_roundtrip PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND WIN32)
    add_executable(fl_nv12_win test/fl_nv12_win.cpp)
    target_link_libraries(fl_nv12_win PRIVATE framelink d3d11)
    add_test(NAME nv12_windows_roundtrip COMMAND ${CMAKE_COMMAND}
        -DPRODUCER=$<TARGET_FILE:fl_nv12_win> -DCONSUMER=$<TARGET_FILE:fl_nv12_win>
        "-DPRODUCER_ARGS=produce;flci-nv12" "-DCONSUMER_ARGS=consume;flci-nv12"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_roundtrip.cmake)
    set_tests_properties(nv12_windows_roundtrip PROPERTIES TIMEOUT 95)
endif()

if(FRAMELINK_BUILD_TESTS AND WIN32)
    add_test(NAME nv12_windows_1080p COMMAND ${CMAKE_COMMAND}
        -DPRODUCER=$<TARGET_FILE:fl_nv12_win> -DCONSUMER=$<TARGET_FILE:fl_nv12_win>
        "-DPRODUCER_ARGS=produce;flci-nv12-hd;1920;1080" "-DCONSUMER_ARGS=consume;flci-nv12-hd;1920;1080"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_roundtrip.cmake)
    set_tests_properties(nv12_windows_1080p PROPERTIES TIMEOUT 95)
endif()

option(FRAMELINK_BUILD_SAMPLES "Build configurable producer/consumer validation tools" ON)
if(FRAMELINK_BUILD_SAMPLES)
    foreach(role producer consumer)
        add_executable(framelink_${role} samples/${role}.cpp samples/sample_common.cpp)
        target_link_libraries(framelink_${role} PRIVATE framelink)
        if(WIN32)
            target_link_libraries(framelink_${role} PRIVATE d3d11 dxgi)
        endif()
    endforeach()
endif()

if(FRAMELINK_BUILD_TESTS AND FRAMELINK_BUILD_SAMPLES AND NOT ANDROID)
    function(fl_sample_case name consumer_args producer_args)
        add_test(NAME sample_${name} COMMAND ${CMAKE_COMMAND}
            -DPRODUCER=$<TARGET_FILE:framelink_producer>
            -DCONSUMER=$<TARGET_FILE:framelink_consumer>
            "-DPRODUCER_ARGS=--channel;sample-${name};--frames;200;--fps;120;${producer_args}"
            "-DCONSUMER_ARGS=--channel;sample-${name};--frames;12;--size;130x66;${consumer_args}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_samples.cmake)
        set_tests_properties(sample_${name} PROPERTIES TIMEOUT 30)
    endfunction()
    fl_sample_case(bgra "--format;bgra;--pool;1" "")
    fl_sample_case(nv12 "--format;nv12;--pool;8" "--format;nv12")
    fl_sample_case(slow "--format;nv12;--delay-ms;20;--pool;2" "--format;nv12")
    fl_sample_case(metadata "--verify;0" "")
    fl_sample_case(reconnect "--format;nv12" "--reconnect-at;4")
    fl_sample_case(geometry "--format;nv12" "--size;194x98")
    fl_sample_case(slots "--channel;sample-slots.0" "--slots;2")
    fl_sample_case(gpu_sync "--format;nv12;--gpu-sync;1" "--submit;manual")
    add_test(NAME sample_invalid_cli COMMAND framelink_consumer --format nv12 --size 131x66)
    set_tests_properties(sample_invalid_cli PROPERTIES WILL_FAIL TRUE)
    add_test(NAME sample_wrong_role COMMAND framelink_producer --pool 4)
    set_tests_properties(sample_wrong_role PROPERTIES WILL_FAIL TRUE)
endif()

if(FRAMELINK_BUILD_TESTS AND FRAMELINK_BUILD_SAMPLES AND NOT ANDROID)
    add_test(NAME sample_corruption COMMAND ${CMAKE_COMMAND}
        -DPRODUCER=$<TARGET_FILE:framelink_producer> -DCONSUMER=$<TARGET_FILE:framelink_consumer>
        "-DPRODUCER_ARGS=--channel;sample-corruption;--frames;100;--fps;10;--corrupt-at;0"
        "-DCONSUMER_ARGS=--channel;sample-corruption;--size;130x66;--format;nv12;--frames;10"
        -DEXPECT_CONSUMER=1 -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_samples.cmake)
    set_tests_properties(sample_corruption PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND WIN32)
    add_executable(fl_disconnect_win test/fl_disconnect_win.cpp)
    target_link_libraries(fl_disconnect_win PRIVATE framelink)
    add_test(NAME disconnect_full_ring COMMAND fl_disconnect_win)
    set_tests_properties(disconnect_full_ring PROPERTIES TIMEOUT 10)
endif()

if(FRAMELINK_BUILD_TESTS)
    add_executable(fl_trace_test test/fl_trace_test.cpp)
    target_include_directories(fl_trace_test PRIVATE include)
    if(UNIX AND NOT ANDROID)
        target_link_libraries(fl_trace_test PRIVATE pthread)
    endif()
    add_test(NAME trace_disabled COMMAND fl_trace_test)
    set_tests_properties(trace_disabled PROPERTIES ENVIRONMENT "FRAMELINK_TRACE=")
    add_test(NAME trace_bounded_concurrent COMMAND fl_trace_test)
    set_tests_properties(trace_bounded_concurrent PROPERTIES ENVIRONMENT
        "FRAMELINK_TRACE=${CMAKE_CURRENT_BINARY_DIR}/trace-test;FRAMELINK_TRACE_EVENTS=16")
endif()

if(FRAMELINK_BUILD_TESTS)
    add_test(NAME trace_thread_limit COMMAND fl_trace_test threads)
    set_tests_properties(trace_thread_limit PROPERTIES ENVIRONMENT
        "FRAMELINK_TRACE=${CMAKE_CURRENT_BINARY_DIR}/trace-threads;FRAMELINK_TRACE_EVENTS=16")
endif()

if(FRAMELINK_BUILD_TESTS)
    add_executable(fl_portable_map test/fl_portable_map.cpp)
    target_link_libraries(fl_portable_map PRIVATE framelink)
    add_test(NAME portable_map_nv12 COMMAND fl_portable_map nv12)
    add_test(NAME portable_map_rgba COMMAND fl_portable_map rgba)
    add_test(NAME portable_map_bgra COMMAND fl_portable_map bgra)
    add_test(NAME portable_map_rgb10a2 COMMAND fl_portable_map rgb10a2)
    set_tests_properties(portable_map_nv12 portable_map_rgba portable_map_bgra portable_map_rgb10a2 PROPERTIES TIMEOUT 20)
endif()

if(FRAMELINK_BUILD_TESTS AND WIN32)
    add_executable(fl_ready_timeout_win test/fl_ready_timeout_win.cpp)
    target_link_libraries(fl_ready_timeout_win PRIVATE framelink d3d11)
    add_test(NAME ready_fence_timeout COMMAND fl_ready_timeout_win)
    set_tests_properties(ready_fence_timeout PROPERTIES TIMEOUT 10)
endif()

if(FRAMELINK_BUILD_TESTS AND UNIX)
    add_executable(fl_sync_fd_test test/fl_sync_fd.cpp)
    target_link_libraries(fl_sync_fd_test PRIVATE framelink)
    add_test(NAME sync_fd_contract COMMAND fl_sync_fd_test)
    set_tests_properties(sync_fd_contract PROPERTIES TIMEOUT 10)
endif()

# Android GStreamer SDK ships static libraries. Register gstframelink in the
# APK's native initialization with GST_PLUGIN_STATIC_REGISTER(framelink).
set(FRAMELINK_GST_ANDROID_ROOT "" CACHE PATH "GStreamer Android SDK architecture directory")
if(ANDROID AND FRAMELINK_BUILD_GST AND FRAMELINK_GST_ANDROID_ROOT)
    find_package(PkgConfig REQUIRED)
    set(_fl_saved_pc_libdir "$ENV{PKG_CONFIG_LIBDIR}")
    set(_fl_saved_pc_path "$ENV{PKG_CONFIG_PATH}")
    set(ENV{PKG_CONFIG_LIBDIR} "${FRAMELINK_GST_ANDROID_ROOT}/lib/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "")
    pkg_check_modules(FL_ANDROID_GST REQUIRED gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0 gstreamer-app-1.0)
    set(ENV{PKG_CONFIG_LIBDIR} "${_fl_saved_pc_libdir}")
    set(ENV{PKG_CONFIG_PATH} "${_fl_saved_pc_path}")
    add_library(gstframelink STATIC gst/gstframelink.cpp)
    target_include_directories(gstframelink PUBLIC ${FL_ANDROID_GST_INCLUDE_DIRS})
    target_compile_options(gstframelink PRIVATE ${FL_ANDROID_GST_CFLAGS_OTHER})
    target_compile_definitions(gstframelink PRIVATE PACKAGE="framelink" PACKAGE_VERSION="${PROJECT_VERSION}")
    target_link_libraries(gstframelink PUBLIC framelink ${FL_ANDROID_GST_STATIC_LDFLAGS})
    if(FRAMELINK_BUILD_TESTS)
        add_executable(fl_gst_nv12 test/fl_gst_nv12.cpp)
        target_link_libraries(fl_gst_nv12 PRIVATE gstframelink
            "${FRAMELINK_GST_ANDROID_ROOT}/lib/gstreamer-1.0/libgstapp.a" ${FL_ANDROID_GST_STATIC_LDFLAGS})
    endif()
endif()

if(FRAMELINK_BUILD_TESTS)
    add_executable(fl_reconnect_held test/fl_reconnect_held.cpp)
    target_link_libraries(fl_reconnect_held PRIVATE framelink)
    add_test(NAME reconnect_held COMMAND fl_reconnect_held)
    set_tests_properties(reconnect_held PROPERTIES TIMEOUT 20)
endif()
