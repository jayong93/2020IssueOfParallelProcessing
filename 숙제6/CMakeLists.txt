cmake_minimum_required(VERSION 3.10)

project("Elimination_Stack")
set(CMAKE_VERBOSE_MAKEFILE true)

set(OUTPUT_NAME "${CMAKE_PROJECT_NAME}")
set(SRC_FILES
    NonBlockingStack.cpp
    )

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-g -ggdb -std=c++17)
link_libraries(pthread numa)
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY bin)

set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -Ofast")
add_executable(${OUTPUT_NAME} ${SRC_FILES})
