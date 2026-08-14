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
        PUBLIC_HEADER "include/framelink.h;include/framelink_dmabuf.h;include/framelink_slots.h")
elseif(ANDROID)
    # Static: APK native modules embed it, and there is no system loader story
    # for a bare shared library on Android anyway.
    add_library(framelink STATIC
        src/framelink_android.cpp
        src/framelink_slots.cpp
        src/fl_ipc_posix.cpp)
    target_link_libraries(framelink PRIVATE android log)
    set_target_properties(framelink PROPERTIES
        PUBLIC_HEADER "include/framelink.h;include/framelink_ahb.h;include/framelink_slots.h")
else()
    message(FATAL_ERROR "framelink has no backend for this platform yet")
endif()

target_include_directories(framelink
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(framelink PROPERTIES VERSION ${PROJECT_VERSION})
if(WIN32)
    set_target_properties(framelink PROPERTIES
        PUBLIC_HEADER "include/framelink.h;include/framelink_d3d11.h;include/framelink_slots.h")
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
