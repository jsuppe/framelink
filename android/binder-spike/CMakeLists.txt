cmake_minimum_required(VERSION 3.22)
project(fl_binder_spike LANGUAGES CXX)
add_library(fl_binder_spike SHARED bridge.cpp)
target_link_libraries(fl_binder_spike PRIVATE android log)
