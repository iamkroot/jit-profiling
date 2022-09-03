cmake_minimum_required(VERSION 3.23)
project(Kaleidoscope)

set(CMAKE_CXX_STANDARD 20)

find_package(LLVM 14.0.6 REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs analysis executionengine support core instcombine object irreader passes mcjit runtimedyld native)

find_package(JNI 11 REQUIRED)

add_library(profjit SHARED library.cpp)
target_link_libraries(profjit ${JAVA_JVM_LIBRARY} fmt)
target_include_directories(profjit PUBLIC ${JAVA_INCLUDE_PATH})
target_include_directories(profjit PUBLIC ${JAVA_INCLUDE_PATH2})