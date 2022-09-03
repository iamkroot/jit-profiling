cmake_minimum_required(VERSION 3.23)
project(Kaleidoscope)

set(CMAKE_CXX_STANDARD 20)

find_package(LLVM 14.0.6 REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs analysis executionengine target X86 support core instcombine object irreader passes mcjit runtimedyld native)

find_package(JNI 11 REQUIRED)

add_library(profjit SHARED library.cpp)
target_compile_options(profjit PUBLIC -fno-omit-frame-pointer)
target_link_libraries(profjit ${JAVA_JVM_LIBRARY} fmt ${llvm_libs})
target_include_directories(profjit PUBLIC ${JAVA_INCLUDE_PATH})
target_include_directories(profjit PUBLIC ${JAVA_INCLUDE_PATH2})

add_library(fib OBJECT fib.c)
target_compile_options(fib PUBLIC -emit-llvm -S -fno-omit-frame-pointer)
set_target_properties(fib PROPERTIES SUFFIX ".ll")

add_dependencies(profjit fib)
