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

target_sources(framelink PRIVATE src/fl_trace.cpp src/framelink_session.cpp src/framelink_memory.cpp)
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

set_property(TARGET framelink APPEND PROPERTY PUBLIC_HEADER include/framelink_session.h include/framelink_memory.h)

if(FRAMELINK_BUILD_TESTS)
    # Two processes, because that is the only way to test what this library
    # does. fl_consume owns the ring and verifies pixels; fl_produce attaches
    # and draws.
    enable_testing()
    if(ANDROID)
        add_executable(fl_read_lease_egl test/fl_read_lease_egl.cpp)
        target_link_libraries(fl_read_lease_egl PRIVATE framelink EGL GLESv2)
    elseif(UNIX)
        pkg_check_modules(LEASE_EGL QUIET IMPORTED_TARGET egl glesv2)
        if(LEASE_EGL_FOUND)
            add_executable(fl_read_lease_egl test/fl_read_lease_egl.cpp)
            target_link_libraries(fl_read_lease_egl PRIVATE framelink PkgConfig::LEASE_EGL)
        endif()
    endif()
    if(TARGET fl_read_lease_egl)
        add_test(NAME read_lease_egl COMMAND fl_read_lease_egl)
        add_test(NAME read_lease_egl_producer_death COMMAND fl_read_lease_egl --producer-death)
        set_tests_properties(read_lease_egl read_lease_egl_producer_death PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
    endif()
    add_executable(fl_read_leases test/fl_read_leases.cpp)
    target_link_libraries(fl_read_leases PRIVATE framelink)
    add_test(NAME read_leases COMMAND fl_read_leases)
    set_tests_properties(read_leases PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
    add_executable(fl_frame_metadata_contract test/fl_frame_metadata_contract.cpp)
    target_include_directories(fl_frame_metadata_contract PRIVATE include src)
    add_test(NAME frame_metadata_contract COMMAND fl_frame_metadata_contract)
    add_executable(fl_read_ledger test/fl_read_ledger.cpp)
    target_include_directories(fl_read_ledger PRIVATE include src)
    add_test(NAME read_ledger COMMAND fl_read_ledger)
    set_tests_properties(read_ledger PROPERTIES TIMEOUT 10)
    add_executable(fl_session_contract test/fl_session_contract.cpp)
    target_link_libraries(fl_session_contract PRIVATE framelink)
    add_test(NAME session_contract COMMAND fl_session_contract)
    enable_language(C)
    add_executable(fl_session_c_api test/fl_session_c_api.c)
    set_target_properties(fl_session_c_api PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_link_libraries(fl_session_c_api PRIVATE framelink)
    add_test(NAME session_contract_c_api COMMAND fl_session_c_api)
    add_executable(fl_memory_boundary test/fl_memory_boundary.cpp)
    target_link_libraries(fl_memory_boundary PRIVATE framelink)
    add_test(NAME memory_boundary COMMAND fl_memory_boundary)
    set_tests_properties(memory_boundary PROPERTIES TIMEOUT 10)
    add_executable(fl_memory_contract test/fl_memory_contract.cpp)
    target_link_libraries(fl_memory_contract PRIVATE framelink)
    add_test(NAME memory_contract COMMAND fl_memory_contract)
    add_executable(fl_memory_c_api test/fl_memory_c_api.c)
    set_target_properties(fl_memory_c_api PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_link_libraries(fl_memory_c_api PRIVATE framelink)
    add_test(NAME memory_contract_c_api COMMAND fl_memory_c_api)
    set_tests_properties(memory_contract memory_contract_c_api PROPERTIES TIMEOUT 10)
    if(WIN32)
        add_executable(fl_read_lease_d3d11 test/fl_read_lease_d3d11.cpp)
        target_link_libraries(fl_read_lease_d3d11 PRIVATE framelink d3d11 dxgi)
        add_test(NAME read_lease_d3d11 COMMAND fl_read_lease_d3d11)
        add_test(NAME read_lease_d3d11_producer_death COMMAND fl_read_lease_d3d11 --producer-death)
        set_tests_properties(read_lease_d3d11 read_lease_d3d11_producer_death PROPERTIES TIMEOUT 20)
        add_executable(fl_import_failure_win test/fl_import_failure_win.cpp src/fl_ipc_win32.cpp src/fl_trace.cpp src/framelink_memory.cpp)
        target_include_directories(fl_import_failure_win PRIVATE include src)
        target_link_libraries(fl_import_failure_win PRIVATE d3d11 dxgi)
        add_test(NAME import_failure_win COMMAND fl_import_failure_win)
        set_tests_properties(import_failure_win PROPERTIES TIMEOUT 20)
        add_executable(fl_external_pool_win test/fl_external_pool_win.cpp)
        target_link_libraries(fl_external_pool_win PRIVATE framelink d3d11 dxgi)
        foreach(_format 1 2 3 4)
            add_test(NAME external_pool_${_format} COMMAND fl_external_pool_win ${_format})
            set_tests_properties(external_pool_${_format} PROPERTIES TIMEOUT 20)
        endforeach()
    endif()
    if(UNIX AND NOT ANDROID)
        add_executable(fl_external_pool_linux test/fl_external_pool_linux.cpp)
        target_link_libraries(fl_external_pool_linux PRIVATE framelink PkgConfig::GBM PkgConfig::DRM)
        foreach(_format 1 2 3 4)
            add_test(NAME external_pool_${_format} COMMAND fl_external_pool_linux ${_format})
            set_tests_properties(external_pool_${_format} PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
        endforeach()
    endif()
    if(ANDROID)
        add_executable(fl_external_pool_android test/fl_external_pool_android.cpp)
        target_link_libraries(fl_external_pool_android PRIVATE framelink dl)
        foreach(_format 1 2 3 4)
            add_test(NAME external_pool_${_format} COMMAND fl_external_pool_android ${_format})
            add_test(NAME external_pool_broker_${_format} COMMAND fl_external_pool_android ${_format} broker)
            set_tests_properties(external_pool_${_format} external_pool_broker_${_format} PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
        endforeach()
    endif()
    add_executable(fl_buffer_references test/fl_buffer_references.cpp)
    target_link_libraries(fl_buffer_references PRIVATE framelink)
    add_test(NAME buffer_references COMMAND fl_buffer_references)
    set_tests_properties(buffer_references PROPERTIES TIMEOUT 20)
    add_executable(fl_submit_ownership test/fl_submit_ownership.cpp)
    target_link_libraries(fl_submit_ownership PRIVATE framelink)
    add_test(NAME submit_ownership COMMAND fl_submit_ownership)
    set_tests_properties(submit_ownership PROPERTIES TIMEOUT 15 SKIP_RETURN_CODE 77)
    add_executable(fl_memory_resource test/fl_memory_resource.cpp)
    target_link_libraries(fl_memory_resource PRIVATE framelink)
    foreach(_format 1 2 3 4)
        add_test(NAME memory_resource_${_format} COMMAND fl_memory_resource ${_format})
        set_tests_properties(memory_resource_${_format} PROPERTIES TIMEOUT 15 SKIP_RETURN_CODE 77)
    endforeach()
    find_package(Threads REQUIRED)
    if(WIN32)
        set(FL_TEST_IPC_SOURCE src/fl_ipc_win32.cpp)
    else()
        set(FL_TEST_IPC_SOURCE src/fl_ipc_posix.cpp)
        add_executable(fl_ipc_capacity test/fl_ipc_capacity.cpp ${FL_TEST_IPC_SOURCE})
        target_include_directories(fl_ipc_capacity PRIVATE src include)
        target_link_libraries(fl_ipc_capacity PRIVATE Threads::Threads)
        add_test(NAME ipc_expanded_capacity COMMAND fl_ipc_capacity)
        set_tests_properties(ipc_expanded_capacity PROPERTIES TIMEOUT 15)
        add_executable(fl_ipc_adopt test/fl_ipc_adopt.cpp ${FL_TEST_IPC_SOURCE})
        target_include_directories(fl_ipc_adopt PRIVATE src include)
        target_link_libraries(fl_ipc_adopt PRIVATE Threads::Threads)
        add_test(NAME ipc_connected_fd COMMAND fl_ipc_adopt)
        set_tests_properties(ipc_connected_fd PROPERTIES TIMEOUT 5)
    endif()
    add_executable(fl_frame_metadata test/fl_frame_metadata.cpp ${FL_TEST_IPC_SOURCE})
    target_link_libraries(fl_frame_metadata PRIVATE framelink)
    add_test(NAME frame_metadata COMMAND fl_frame_metadata)
    set_tests_properties(frame_metadata PROPERTIES TIMEOUT 20)

    add_executable(fl_expanded_pool test/fl_expanded_pool.cpp ${FL_TEST_IPC_SOURCE})
    target_include_directories(fl_expanded_pool PRIVATE src)
    target_link_libraries(fl_expanded_pool PRIVATE framelink Threads::Threads)
    add_test(NAME expanded_pool COMMAND fl_expanded_pool)
    set_tests_properties(expanded_pool PROPERTIES TIMEOUT 30 LABELS "gpu;integration")
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

if(FRAMELINK_BUILD_TESTS AND ANDROID)
    add_executable(fl_android_connected_fd test/fl_android_connected_fd.cpp)
    target_link_libraries(fl_android_connected_fd PRIVATE framelink android)
    add_test(NAME android_connected_fd COMMAND fl_android_connected_fd)
    set_tests_properties(android_connected_fd PROPERTIES TIMEOUT 15)
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
            if(EXISTS "${FL_GST_ROOT}/lib/gstd3d11-1.0.lib")
                target_link_libraries(gstframelink PRIVATE gstd3d11-1.0.lib)
                target_compile_definitions(gstframelink PRIVATE FL_GST_D3D11=1)
                set(FL_GST_D3D11_FOUND TRUE)
            endif()
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
                pkg_check_modules(FL_GST_ALLOCATORS REQUIRED IMPORTED_TARGET gstreamer-allocators-1.0)
                target_link_libraries(gstframelink PRIVATE PkgConfig::FL_GST_ALLOCATORS)
                target_compile_definitions(gstframelink PRIVATE FL_GST_DMABUF=1)
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
install(FILES gst/framelink_native.h gst/framelink_memory_policy.h gst/framelink_meta.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/framelink)
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
    add_executable(fl_gst_native_producer_linux test/fl_gst_native_producer_linux.cpp)
    target_link_libraries(fl_gst_native_producer_linux PRIVATE framelink PkgConfig::FL_GST PkgConfig::FL_GST_APP PkgConfig::FL_GST_ALLOCATORS)
    add_test(NAME gst_native_producer_linux COMMAND fl_gst_native_producer_linux $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_native_producer_linux PROPERTIES TIMEOUT 20)
    add_executable(fl_gst_native_source test/fl_gst_native_source.cpp)
    target_link_libraries(fl_gst_native_source PRIVATE framelink PkgConfig::FL_GST PkgConfig::FL_GST_APP PkgConfig::FL_GST_ALLOCATORS)
    add_test(NAME gst_native_source COMMAND fl_gst_native_source $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_native_source PROPERTIES TIMEOUT 15)
    add_executable(fl_gst_nv12 test/fl_gst_nv12.cpp)
    target_link_libraries(fl_gst_nv12 PRIVATE PkgConfig::FL_GST PkgConfig::FL_GST_APP)
    add_test(NAME gst_nv12_roundtrip COMMAND fl_gst_nv12 $<TARGET_FILE:gstframelink>)
    add_test(NAME gst_rgb10a2_roundtrip COMMAND fl_gst_nv12 $<TARGET_FILE:gstframelink> rgb10a2)
    set_tests_properties(gst_nv12_roundtrip gst_rgb10a2_roundtrip PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND FL_GST_FOUND AND WIN32)
    if(FL_GST_D3D11_FOUND)
    add_executable(fl_gst_native_source_win test/fl_gst_native_source_win.cpp)
    target_include_directories(fl_gst_native_source_win PRIVATE
        "${FL_GST_ROOT}/lib/gstreamer-1.0/include"
        "${FL_GST_ROOT}/include/gstreamer-1.0" "${FL_GST_ROOT}/include/glib-2.0"
        "${FL_GST_ROOT}/lib/glib-2.0/include")
    target_link_directories(fl_gst_native_source_win PRIVATE "${FL_GST_ROOT}/lib")
    target_link_libraries(fl_gst_native_source_win PRIVATE framelink gstreamer-1.0.lib gstapp-1.0.lib gstvideo-1.0.lib gstd3d11-1.0.lib gobject-2.0.lib glib-2.0.lib)
    add_executable(fl_gst_native_producer_win test/fl_gst_native_producer_win.cpp)
    target_include_directories(fl_gst_native_producer_win PRIVATE $<TARGET_PROPERTY:fl_gst_native_source_win,INCLUDE_DIRECTORIES>)
    target_link_directories(fl_gst_native_producer_win PRIVATE "${FL_GST_ROOT}/lib")
    target_link_libraries(fl_gst_native_producer_win PRIVATE $<TARGET_PROPERTY:fl_gst_native_source_win,LINK_LIBRARIES>)
    add_test(NAME gst_native_producer_win COMMAND fl_gst_native_producer_win $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_native_producer_win PROPERTIES TIMEOUT 20)
    add_test(NAME gst_native_source_win COMMAND fl_gst_native_source_win $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_native_source_win PROPERTIES TIMEOUT 20)
    add_test(NAME gst_native_source_win_copy_negative COMMAND fl_gst_native_source_win $<TARGET_FILE:gstframelink> --copy-negative)
    set_tests_properties(gst_native_source_win_copy_negative PROPERTIES TIMEOUT 20)
    endif()
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
    if(UNIX AND NOT ANDROID)
        # Deterministically exercise the legal release-after-peer-death result,
        # after the real release has relinquished ownership.
        add_library(fl_release_disconnect_shim SHARED test/fl_release_disconnect_shim.cpp)
        target_include_directories(fl_release_disconnect_shim PRIVATE include)
        target_link_libraries(fl_release_disconnect_shim PRIVATE ${CMAKE_DL_LIBS})
        add_test(NAME sample_release_disconnected COMMAND ${CMAKE_COMMAND}
            -DPRODUCER=$<TARGET_FILE:framelink_producer>
            -DCONSUMER=$<TARGET_FILE:framelink_consumer>
            -DRELEASE_SHIM=$<TARGET_FILE:fl_release_disconnect_shim>
            "-DPRODUCER_ARGS=--channel;sample-release-disconnected;--frames;200;--fps;120"
            "-DCONSUMER_ARGS=--channel;sample-release-disconnected;--frames;12;--size;130x66;--format;nv12"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_samples.cmake)
        set_tests_properties(sample_release_disconnected PROPERTIES TIMEOUT 30)
        add_test(NAME sample_consumed_disconnected COMMAND ${CMAKE_COMMAND}
            -DPRODUCER=$<TARGET_FILE:framelink_producer>
            -DCONSUMER=$<TARGET_FILE:framelink_consumer>
            -DRELEASE_SHIM=$<TARGET_FILE:fl_release_disconnect_shim>
            -DEXPECT_CONSUMED_INJECTION=1
            "-DPRODUCER_ARGS=--channel;sample-consumed-disconnected;--frames;200;--fps;120"
            "-DCONSUMER_ARGS=--channel;sample-consumed-disconnected;--frames;12;--size;130x66;--format;nv12;--gpu-sync;1"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_samples.cmake)
        set_tests_properties(sample_consumed_disconnected PROPERTIES TIMEOUT 30)
    endif()
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
    if(FRAMELINK_BUILD_SAMPLES)
        pkg_check_modules(FL_ANDROID_AV REQUIRED gio-2.0 gstreamer-net-1.0)
    endif()
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

# Existing GStreamer transports carry the separate PCM track; no audio core ABI.
if(FRAMELINK_BUILD_SAMPLES AND FL_GST_FOUND AND NOT ANDROID)
    add_executable(framelink_av_session samples/av_session.cpp)
    if(WIN32)
        target_include_directories(framelink_av_session PRIVATE
            "${FL_GST_ROOT}/include/gstreamer-1.0" "${FL_GST_ROOT}/include/glib-2.0"
            "${FL_GST_ROOT}/lib/glib-2.0/include")
        target_link_directories(framelink_av_session PRIVATE "${FL_GST_ROOT}/lib")
        target_link_libraries(framelink_av_session PRIVATE gstreamer-1.0.lib gstapp-1.0.lib gobject-2.0.lib glib-2.0.lib)
    else()
        pkg_check_modules(FL_AV_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
        target_link_libraries(framelink_av_session PRIVATE PkgConfig::FL_GST PkgConfig::FL_AV_APP)
    endif()
    if(FRAMELINK_BUILD_TESTS)
        add_test(NAME gst_av_session COMMAND framelink_av_session $<TARGET_FILE:gstframelink>)
        add_test(NAME gst_av_faults COMMAND framelink_av_session $<TARGET_FILE:gstframelink> --faults)
        set_tests_properties(gst_av_session gst_av_faults PROPERTIES TIMEOUT 25)
    endif()
endif()

if(ANDROID AND FRAMELINK_BUILD_SAMPLES AND TARGET gstframelink)
    add_executable(framelink_av_session samples/av_session.cpp)
    target_link_libraries(framelink_av_session PRIVATE gstframelink
        "${FRAMELINK_GST_ANDROID_ROOT}/lib/gstreamer-1.0/libgstapp.a"
        "${FRAMELINK_GST_ANDROID_ROOT}/lib/gstreamer-1.0/libgstcoreelements.a"
        "${FRAMELINK_GST_ANDROID_ROOT}/lib/gstreamer-1.0/libgstgdp.a"
        "${FRAMELINK_GST_ANDROID_ROOT}/lib/gstreamer-1.0/libgsttcp.a"
        ${FL_ANDROID_AV_STATIC_LDFLAGS} ${FL_ANDROID_GST_STATIC_LDFLAGS})
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET fl_gst_nv12 AND NOT ANDROID)
    add_executable(fl_gst_timestamps test/fl_gst_timestamps.cpp)
    target_include_directories(fl_gst_timestamps PRIVATE include)
    get_target_property(_fl_test_includes fl_gst_nv12 INCLUDE_DIRECTORIES)
    if(_fl_test_includes)
        target_include_directories(fl_gst_timestamps PRIVATE ${_fl_test_includes})
    endif()
    get_target_property(_fl_test_dirs fl_gst_nv12 LINK_DIRECTORIES)
    if(_fl_test_dirs)
        target_link_directories(fl_gst_timestamps PRIVATE ${_fl_test_dirs})
    endif()
    get_target_property(_fl_test_libs fl_gst_nv12 LINK_LIBRARIES)
    target_link_libraries(fl_gst_timestamps PRIVATE ${_fl_test_libs})
    add_test(NAME gst_timestamp_contract COMMAND fl_gst_timestamps $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_timestamp_contract PROPERTIES TIMEOUT 25)
endif()

if(FRAMELINK_BUILD_TESTS AND NOT ANDROID)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(fl_session_peer test/fl_session_peer.cpp)
    target_link_libraries(fl_session_peer PRIVATE framelink)
    add_test(NAME session_contract_peers COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/session_peers.py $<TARGET_FILE:fl_session_peer>)
    set_tests_properties(session_contract_peers PROPERTIES TIMEOUT 30)
endif()

# Independent desktop A/V peers. GDP PCM uses authenticated GIO streams.
if(FRAMELINK_BUILD_SAMPLES AND FL_GST_FOUND AND NOT ANDROID)
    foreach(_role producer consumer playback)
        if(_role STREQUAL "playback")
            add_library(framelink_av_playback STATIC samples/av_apps.cpp samples/av_rendezvous.cpp samples/av_auth.cpp samples/av_memory_agreement.cpp)
            target_compile_definitions(framelink_av_playback PRIVATE AV_PLAYBACK_LIBRARY=1)
            target_include_directories(framelink_av_playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/samples)
        else()
            add_executable(framelink_av_${_role} samples/av_apps.cpp samples/av_rendezvous.cpp samples/av_auth.cpp samples/av_memory_agreement.cpp)
        endif()
        target_link_libraries(framelink_av_${_role} PUBLIC framelink)
        if(WIN32)
            target_include_directories(framelink_av_${_role} PUBLIC
                "${FL_GST_ROOT}/include/gstreamer-1.0" "${FL_GST_ROOT}/include/glib-2.0"
                "${FL_GST_ROOT}/lib/glib-2.0/include")
            target_link_directories(framelink_av_${_role} PUBLIC "${FL_GST_ROOT}/lib")
            target_link_libraries(framelink_av_${_role} PUBLIC gstreamer-1.0.lib gstapp-1.0.lib gstvideo-1.0.lib gstaudio-1.0.lib gstbase-1.0.lib gio-2.0.lib gobject-2.0.lib glib-2.0.lib advapi32 bcrypt)
        else()
            pkg_check_modules(FL_AV_IO REQUIRED IMPORTED_TARGET gio-2.0 gstreamer-video-1.0 gstreamer-audio-1.0)
            target_link_libraries(framelink_av_${_role} PUBLIC PkgConfig::FL_GST PkgConfig::FL_AV_APP PkgConfig::FL_AV_IO)
        endif()
    endforeach()
    target_compile_definitions(framelink_av_producer PRIVATE AV_PRODUCER=1)
endif()

# Pure local control-plane gates; no GPU device or media sink required.
if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_producer)
    add_executable(fl_av_rendezvous test/test_av_rendezvous.cpp samples/av_rendezvous.cpp)
    target_include_directories(fl_av_rendezvous PRIVATE samples)
    add_executable(fl_av_auth test/test_av_auth.cpp samples/av_auth.cpp samples/av_rendezvous.cpp)
    target_include_directories(fl_av_auth PRIVATE samples)
    if(WIN32)
        target_link_libraries(fl_av_rendezvous PRIVATE advapi32 bcrypt)
        target_include_directories(fl_av_auth PRIVATE "${FL_GST_ROOT}/include/glib-2.0" "${FL_GST_ROOT}/lib/glib-2.0/include")
        target_link_directories(fl_av_auth PRIVATE "${FL_GST_ROOT}/lib")
        target_link_libraries(fl_av_auth PRIVATE gio-2.0.lib gobject-2.0.lib glib-2.0.lib advapi32 bcrypt)
    else()
        target_link_libraries(fl_av_auth PRIVATE PkgConfig::FL_AV_IO)
    endif()
    if(NOT WIN32)
        add_executable(fl_av_connected_fd test/test_av_connected_fd.cpp samples/av_auth.cpp samples/av_rendezvous.cpp)
        target_include_directories(fl_av_connected_fd PRIVATE samples include)
        target_link_libraries(fl_av_connected_fd PRIVATE PkgConfig::FL_AV_IO)
        add_test(NAME av_connected_fd COMMAND fl_av_connected_fd)
        set_tests_properties(av_connected_fd PROPERTIES TIMEOUT 15)
    endif()
    add_test(NAME av_rendezvous COMMAND fl_av_rendezvous)
    add_test(NAME av_auth COMMAND fl_av_auth)
    set_tests_properties(av_rendezvous av_auth PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_producer)
    add_test(NAME av_producer_help COMMAND framelink_av_producer --help)
    add_test(NAME av_consumer_help COMMAND framelink_av_consumer --help)
    if(Python3_Interpreter_FOUND)
        add_test(NAME m3_av_apps COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/m3_apps.py
            --producer $<TARGET_FILE:framelink_av_producer> --consumer $<TARGET_FILE:framelink_av_consumer>
            --plugin $<TARGET_FILE:gstframelink>)
        set_tests_properties(m3_av_apps PROPERTIES TIMEOUT 180)
    endif()
endif()

# Real transport timing checks (GPU-backed FrameLink + GDP/TCP PCM).
# Not part of the hosted CPU-only whitelist; run on native hardware.
if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_session)
    if(ANDROID)
        add_executable(fl_gst_timestamps test/fl_gst_timestamps.cpp)
        target_include_directories(fl_gst_timestamps PRIVATE include $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
        target_link_directories(fl_gst_timestamps PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_DIRECTORIES>)
        target_link_libraries(fl_gst_timestamps PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)
    endif()
    add_executable(fl_gst_pool_policy test/fl_gst_pool_policy.cpp)
    target_include_directories(fl_gst_pool_policy PRIVATE $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
    target_link_directories(fl_gst_pool_policy PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_DIRECTORIES>)
    target_link_libraries(fl_gst_pool_policy PRIVATE framelink $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)
    add_test(NAME gst_pool_policy COMMAND fl_gst_pool_policy $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_pool_policy PROPERTIES TIMEOUT 30)
    add_executable(fl_m4_transport_timing test/m4_transport_timing.cpp)
    target_include_directories(fl_m4_transport_timing PRIVATE $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
    target_link_directories(fl_m4_transport_timing PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_DIRECTORIES>)
    target_link_libraries(fl_m4_transport_timing PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)
    add_test(NAME m5a_transport_relay COMMAND fl_m4_transport_timing $<TARGET_FILE:gstframelink> --relay)
    add_test(NAME m5a_transport_relay_audio_leads COMMAND fl_m4_transport_timing $<TARGET_FILE:gstframelink> --relay --audio-leads)
    add_test(NAME m5a_transport_relay_sync_negative COMMAND fl_m4_transport_timing $<TARGET_FILE:gstframelink> --relay-sync-negative)
    set_tests_properties(m5a_transport_relay m5a_transport_relay_audio_leads m5a_transport_relay_sync_negative PROPERTIES TIMEOUT 30)
    add_test(NAME m4_transport_audio_lags COMMAND fl_m4_transport_timing $<TARGET_FILE:gstframelink>)
    add_test(NAME m4_transport_audio_leads COMMAND fl_m4_transport_timing $<TARGET_FILE:gstframelink> --audio-leads)
    add_test(NAME m4_transport_jitter_drift COMMAND fl_m4_transport_timing $<TARGET_FILE:gstframelink> --faults)
    add_test(NAME m4_transport_negative_retimestamps COMMAND fl_m4_transport_timing $<TARGET_FILE:gstframelink> --negative)
    set_tests_properties(m4_transport_audio_lags m4_transport_audio_leads m4_transport_jitter_drift m4_transport_negative_retimestamps PROPERTIES TIMEOUT 25)
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_playback)
    add_executable(fl_audio_alignment test/fl_audio_alignment.cpp)
    target_link_libraries(fl_audio_alignment PRIVATE framelink_av_playback)
    if(WIN32)
        target_link_libraries(fl_audio_alignment PRIVATE gstcheck-1.0.lib)
    else()
        pkg_check_modules(GST_ALIGNMENT_CHECK REQUIRED IMPORTED_TARGET gstreamer-check-1.0)
        target_link_libraries(fl_audio_alignment PRIVATE PkgConfig::GST_ALIGNMENT_CHECK)
    endif()
    add_test(NAME audio_alignment COMMAND fl_audio_alignment)
    set_tests_properties(audio_alignment PROPERTIES TIMEOUT 30)
    add_executable(fl_playback_recovery test/fl_playback_recovery.cpp)
    target_link_libraries(fl_playback_recovery PRIVATE framelink_av_playback)
endif()
if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_playback)
    add_executable(fl_av_offer_bounds test/fl_av_offer_bounds.cpp)
    target_link_libraries(fl_av_offer_bounds PRIVATE framelink_av_playback)
    add_test(NAME av_offer_bounds COMMAND fl_av_offer_bounds)
    if(Python3_Interpreter_FOUND)
        add_test(NAME m4_av_recovery COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/m4_recovery.py --build $<TARGET_FILE_DIR:fl_playback_recovery>)
        set_tests_properties(m4_av_recovery PROPERTIES TIMEOUT 90)
        add_test(NAME m5_finite_budget COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/m5_finite_budget.py --build $<TARGET_FILE_DIR:fl_playback_recovery>)
        set_tests_properties(m5_finite_budget PROPERTIES TIMEOUT 45)
    endif()
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_producer AND Python3_Interpreter_FOUND)
    add_test(NAME m4_authenticated_offsets COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/m4_app_offsets.py
        $<TARGET_FILE:framelink_av_producer> $<TARGET_FILE:framelink_av_consumer>
        $<TARGET_FILE:gstframelink>)
    set_tests_properties(m4_authenticated_offsets PROPERTIES TIMEOUT 60)
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET fl_playback_recovery AND Python3_Interpreter_FOUND)
    add_test(NAME m4_source_drift COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/m4_source_drift.py --build $<TARGET_FILE_DIR:fl_playback_recovery>)
    set_tests_properties(m4_source_drift PROPERTIES TIMEOUT 100)
endif()

# Optional PulseAudio mechanism probe, intentionally not an unconditional CTest:
# requires a dedicated/null sink and does not qualify physical output timing.
if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_playback AND UNIX AND NOT ANDROID)
    add_executable(fl_m4_device_slaving test/m4_device_slaving.cpp)
    target_link_libraries(fl_m4_device_slaving PRIVATE framelink_av_playback)
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_playback)
    add_executable(fl_audio_segment test/fl_audio_segment.cpp)
    target_link_libraries(fl_audio_segment PRIVATE framelink_av_playback)
    add_test(NAME av_audio_segment COMMAND fl_audio_segment)
endif()

if(FRAMELINK_BUILD_TESTS AND WIN32)
    add_executable(fl_startup_win test/fl_startup_win.cpp)
    target_link_libraries(fl_startup_win PRIVATE framelink)
    add_test(NAME windows_endpoint_ready COMMAND fl_startup_win)
    set_tests_properties(windows_endpoint_ready PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_producer AND Python3_Interpreter_FOUND)
    add_test(NAME m5_external_pipeline COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/m5_external_pipeline.py
        --build $<TARGET_FILE_DIR:framelink_av_producer>)
    set_tests_properties(m5_external_pipeline PROPERTIES TIMEOUT 90 LABELS "gpu;integration")
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_playback AND TARGET gstframelink)
    add_executable(fl_gst_sink_stats test/fl_gst_sink_stats.cpp)
    target_link_libraries(fl_gst_sink_stats PRIVATE framelink_av_playback)
    add_test(NAME gst_sink_stats COMMAND fl_gst_sink_stats $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_sink_stats PROPERTIES TIMEOUT 15)
    add_executable(fl_gst_preconnect_slots test/fl_gst_preconnect_slots.cpp)
    target_link_libraries(fl_gst_preconnect_slots PRIVATE framelink_av_playback framelink)
    add_test(NAME gst_preconnect_slots COMMAND fl_gst_preconnect_slots $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_preconnect_slots PROPERTIES TIMEOUT 20 LABELS "gpu;integration")
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_producer AND TARGET gstframelink AND Python3_Interpreter_FOUND)
    add_test(NAME m5_startup COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/m5_startup.py
        --build $<TARGET_FILE_DIR:framelink_av_producer>)
    set_tests_properties(m5_startup PROPERTIES TIMEOUT 130 LABELS "gpu;integration")
    add_test(NAME m5_preconnect COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/m5_preconnect.py
        --build $<TARGET_FILE_DIR:framelink_av_producer>)
    set_tests_properties(m5_preconnect PROPERTIES TIMEOUT 150 LABELS "gpu;integration")
    add_test(NAME m5_consumer_drain COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/m5_consumer_drain.py
        --build $<TARGET_FILE_DIR:framelink_av_producer>)
    set_tests_properties(m5_consumer_drain PROPERTIES TIMEOUT 180 LABELS "gpu;integration")
endif()

if(FRAMELINK_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME frame_accounting_negative_controls COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/test_frame_accounting.py)
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_playback AND UNIX AND NOT ANDROID)
    add_executable(fl_broker_producer test/fl_broker_producer.cpp)
    target_link_libraries(fl_broker_producer PRIVATE framelink_av_playback framelink)
    add_dependencies(fl_broker_producer gstframelink)
    add_test(NAME broker_producer_stream COMMAND fl_broker_producer $<TARGET_FILE:gstframelink>)
    add_test(NAME broker_memory_extension COMMAND fl_broker_producer $<TARGET_FILE:gstframelink> memory)
    set_tests_properties(broker_memory_extension PROPERTIES TIMEOUT 45)
    set_tests_properties(broker_producer_stream PROPERTIES TIMEOUT 40)
endif()

if(ANDROID AND FRAMELINK_BUILD_TESTS AND TARGET framelink_av_session)
    add_executable(fl_gst_native_source_android test/fl_gst_native_source_android.cpp)
    target_include_directories(fl_gst_native_source_android PRIVATE $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
    target_link_libraries(fl_gst_native_source_android PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)
    add_executable(fl_gst_native_producer_android test/fl_gst_native_producer_android.cpp)
    target_include_directories(fl_gst_native_producer_android PRIVATE $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
    target_link_libraries(fl_gst_native_producer_android PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)

endif()

# Explicit opt-in memory agreement is distinct from the legacy A/V session wire.
if(FRAMELINK_BUILD_TESTS AND (TARGET framelink_av_playback OR TARGET fl_gst_native_source_android))
    add_executable(fl_av_memory_agreement test/test_av_memory_agreement.cpp samples/av_memory_agreement.cpp samples/av_auth.cpp samples/av_rendezvous.cpp)
    target_include_directories(fl_av_memory_agreement PRIVATE samples)
    if(ANDROID)
        target_link_libraries(fl_av_memory_agreement PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)
        target_include_directories(fl_av_memory_agreement PRIVATE $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
    else()
        target_link_libraries(fl_av_memory_agreement PRIVATE framelink_av_playback)
    endif()
    add_test(NAME av_memory_agreement COMMAND fl_av_memory_agreement)
    set_tests_properties(av_memory_agreement PROPERTIES TIMEOUT 20)
endif()
if(FRAMELINK_BUILD_TESTS AND TARGET framelink_av_playback AND TARGET PkgConfig::GBM)
    add_executable(fl_broker_memory_binding test/fl_broker_memory_binding.cpp)
    target_link_libraries(fl_broker_memory_binding PRIVATE framelink_av_playback PkgConfig::GBM PkgConfig::DRM)
    add_dependencies(fl_broker_memory_binding gstframelink)
    add_test(NAME broker_memory_binding COMMAND fl_broker_memory_binding $<TARGET_FILE:gstframelink>)
    set_tests_properties(broker_memory_binding PROPERTIES TIMEOUT 30)
endif()
if(FRAMELINK_BUILD_TESTS AND ANDROID AND TARGET framelink_av_session)
    add_executable(fl_ahb_memory_binding test/fl_ahb_memory_binding.cpp)
    target_include_directories(fl_ahb_memory_binding PRIVATE samples $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
    target_link_libraries(fl_ahb_memory_binding PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)
    add_test(NAME ahb_memory_binding COMMAND fl_ahb_memory_binding)
    set_tests_properties(ahb_memory_binding PROPERTIES TIMEOUT 15)
endif()
if(FRAMELINK_BUILD_TESTS AND WIN32 AND TARGET framelink_av_playback)
    add_executable(fl_d3d_memory_binding test/fl_d3d_memory_binding.cpp)
    target_link_libraries(fl_d3d_memory_binding PRIVATE framelink_av_playback d3d11 dxgi)
    add_test(NAME d3d_memory_binding COMMAND fl_d3d_memory_binding)
    set_tests_properties(d3d_memory_binding PROPERTIES TIMEOUT 20)
endif()
if(FRAMELINK_BUILD_TESTS AND (TARGET framelink_av_playback OR TARGET framelink_av_session))
    add_executable(fl_gst_policy_generation test/fl_gst_policy_generation.cpp)
    if(ANDROID)
        target_include_directories(fl_gst_policy_generation PRIVATE $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
        target_link_libraries(fl_gst_policy_generation PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)
    else()
        target_link_libraries(fl_gst_policy_generation PRIVATE framelink_av_playback)
    endif()
    add_dependencies(fl_gst_policy_generation gstframelink)
    add_test(NAME gst_policy_generation COMMAND fl_gst_policy_generation $<TARGET_FILE:gstframelink>)
    set_tests_properties(gst_policy_generation PROPERTIES TIMEOUT 20)
endif()
if(FRAMELINK_BUILD_SAMPLES AND FL_GST_D3D11_FOUND)
    foreach(example decoder_probe decode_producer decode_consumer)
        add_executable(fl_d3d_${example} samples/d3d_${example}.cpp)
        target_compile_definitions(fl_d3d_${example} PRIVATE GST_USE_UNSTABLE_API)
        target_include_directories(fl_d3d_${example} PRIVATE
            "${FL_GST_ROOT}/lib/gstreamer-1.0/include"
            "${FL_GST_ROOT}/include/gstreamer-1.0" "${FL_GST_ROOT}/include/glib-2.0"
            "${FL_GST_ROOT}/lib/glib-2.0/include")
        target_link_directories(fl_d3d_${example} PRIVATE "${FL_GST_ROOT}/lib")
        target_link_libraries(fl_d3d_${example} PRIVATE framelink gstreamer-1.0.lib
            gstapp-1.0.lib gstvideo-1.0.lib gstd3d11-1.0.lib gobject-2.0.lib
            glib-2.0.lib dxgi user32)
    endforeach()
endif()
if(FRAMELINK_BUILD_TESTS AND UNIX AND NOT ANDROID AND TARGET fl_external_pool_linux)
    add_executable(fl_native_relay_linux test/fl_native_relay_linux.cpp)
    target_link_libraries(fl_native_relay_linux PRIVATE framelink PkgConfig::GBM PkgConfig::DRM)
    add_test(NAME native_relay_linux COMMAND fl_native_relay_linux)
    set_tests_properties(native_relay_linux PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
endif()
if(FRAMELINK_BUILD_TESTS AND WIN32)
    add_executable(fl_native_relay_win test/fl_native_relay_win.cpp)
    target_link_libraries(fl_native_relay_win PRIVATE framelink d3d11 dxgi)
    add_test(NAME native_relay_win COMMAND fl_native_relay_win)
    set_tests_properties(native_relay_win PROPERTIES TIMEOUT 20)
endif()
if(FRAMELINK_BUILD_TESTS AND ANDROID)
    add_executable(fl_native_relay_android test/fl_native_relay_android.cpp)
    target_link_libraries(fl_native_relay_android PRIVATE framelink dl)
    add_test(NAME native_relay_android COMMAND fl_native_relay_android)
    set_tests_properties(native_relay_android PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
endif()

if(FRAMELINK_BUILD_TESTS)
    add_test(NAME trace_details COMMAND fl_trace_test details)
    set_tests_properties(trace_details PROPERTIES ENVIRONMENT
        "FRAMELINK_TRACE=${CMAKE_CURRENT_BINARY_DIR}/trace-details;FRAMELINK_TRACE_EVENTS=16")
    add_executable(fl_trace_compiled_out test/fl_trace_test.cpp)
    target_include_directories(fl_trace_compiled_out PRIVATE include)
    target_compile_definitions(fl_trace_compiled_out PRIVATE FL_TRACE_COMPILED_OUT)
    if(UNIX AND NOT ANDROID)
        target_link_libraries(fl_trace_compiled_out PRIVATE pthread)
    endif()
    add_test(NAME trace_compiled_out COMMAND fl_trace_compiled_out)
    set_tests_properties(trace_compiled_out PROPERTIES ENVIRONMENT "FRAMELINK_TRACE=${CMAKE_CURRENT_BINARY_DIR}/must-not-exist")
endif()

if(TARGET fl_av_memory_agreement)
    add_executable(fl_av_memory_adversarial test/test_av_memory_adversarial.cpp samples/av_memory_agreement.cpp samples/av_auth.cpp samples/av_rendezvous.cpp)
    target_include_directories(fl_av_memory_adversarial PRIVATE samples)
    if(ANDROID)
        target_include_directories(fl_av_memory_adversarial PRIVATE $<TARGET_PROPERTY:framelink_av_session,INCLUDE_DIRECTORIES>)
        target_link_libraries(fl_av_memory_adversarial PRIVATE $<TARGET_PROPERTY:framelink_av_session,LINK_LIBRARIES>)
    else()
        target_link_libraries(fl_av_memory_adversarial PRIVATE framelink_av_playback)
    endif()
    add_test(NAME av_memory_adversarial COMMAND fl_av_memory_adversarial)
    set_tests_properties(av_memory_adversarial PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND WIN32)
    add_executable(fl_native_relay_process_win test/fl_native_relay_process_win.cpp)
    target_link_libraries(fl_native_relay_process_win PRIVATE framelink d3d11 dxgi)
    add_test(NAME native_relay_process_win COMMAND fl_native_relay_process_win)
    set_tests_properties(native_relay_process_win PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND ANDROID)
    add_executable(fl_native_relay_process_android test/fl_native_relay_process_egl.cpp)
    target_link_libraries(fl_native_relay_process_android PRIVATE framelink EGL GLESv2 dl)
    add_test(NAME native_relay_process_android COMMAND fl_native_relay_process_android)
    set_tests_properties(native_relay_process_android PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endif()

if(FRAMELINK_BUILD_TESTS AND UNIX AND NOT ANDROID AND TARGET PkgConfig::LEASE_EGL AND TARGET PkgConfig::GBM)
    add_executable(fl_native_relay_process_linux test/fl_native_relay_process_egl.cpp)
    target_link_libraries(fl_native_relay_process_linux PRIVATE framelink PkgConfig::LEASE_EGL PkgConfig::GBM PkgConfig::DRM dl)
    add_test(NAME native_relay_process_linux COMMAND fl_native_relay_process_linux)
    set_tests_properties(native_relay_process_linux PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endif()

if(TARGET fl_broker_producer)
    add_test(NAME broker_video_factory COMMAND fl_broker_producer $<TARGET_FILE:gstframelink> factory)
    set_tests_properties(broker_video_factory PROPERTIES TIMEOUT 30)
endif()

if(FRAMELINK_BUILD_TESTS AND TARGET fl_native_relay_process_linux)
    add_executable(fl_native_boundary_benchmark test/fl_native_boundary_benchmark.cpp)
    target_include_directories(fl_native_boundary_benchmark PRIVATE src ${DRM_INCLUDE_DIRS} ${GBM_INCLUDE_DIRS})
    target_link_libraries(fl_native_boundary_benchmark PRIVATE framelink PkgConfig::GBM PkgConfig::DRM EGL GLESv2 dl)
endif()
if(FRAMELINK_BUILD_TESTS AND TARGET fl_native_relay_process_android)
    add_executable(fl_native_boundary_benchmark test/fl_native_boundary_benchmark.cpp)
    target_include_directories(fl_native_boundary_benchmark PRIVATE src)
    target_link_libraries(fl_native_boundary_benchmark PRIVATE framelink EGL GLESv2 dl)
endif()
