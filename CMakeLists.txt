cmake_minimum_required(VERSION 3.23)
project(Kaleidoscope)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
find_package(LLVM 14.0.6 REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs analysis executionengine PerfJITEvents target X86 support core instcombine object irreader passes mcjit runtimedyld native)
include(elfin)
find_package(JNI 11 REQUIRED)

add_library(profjit SHARED library.cpp)
target_compile_options(profjit PUBLIC -fno-omit-frame-pointer)
target_link_libraries(profjit ${JAVA_JVM_LIBRARY} fmt ${llvm_libs} ${elfin_STATIC} ${dwarf_STATIC})
target_include_directories(profjit PUBLIC ${JAVA_INCLUDE_PATH})
target_include_directories(profjit PUBLIC ${JAVA_INCLUDE_PATH2})
target_include_directories(profjit PRIVATE ${elfin_INCLUDE_DIRS}/elfin)

add_library(fib OBJECT fib.c)
target_compile_options(fib PUBLIC -emit-llvm -S)
target_compile_options(fib PUBLIC -O3)
target_compile_options(fib PUBLIC -g -fdebug-default-version=4)
#target_compile_options(fib PUBLIC -fno-omit-frame-pointer)
target_compile_definitions(profjit PUBLIC FIB_LL_PATH="$<TARGET_OBJECTS:fib>")
#
#

#add_custom_command(COMMAND ${CMAKE_C_COMPILER} -O3 -g -emit-llvm -S -fno-omit-frame-pointer ${CMAKE_SOURCE_DIR}/fib.c BYPRODUCTS fib.ll TARGET profjit)
#
#target_compile_definitions(profjit PUBLIC FIB_LL_PATH="${CMAKE_BINARY_DIR}/fib.ll")
add_dependencies(profjit fib)

add_executable(inotify_test inotify_test.cpp)
target_link_libraries(inotify_test inotify-cpp fmt)

