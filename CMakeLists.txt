cmake_minimum_required(VERSION 3.30)
project(graph_generator VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)

# Optional Flags for later:  -fwrapv -ffast-math -g -Wstrict-overflow=5
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -fno-omit-frame-pointer -march=native -fopenmp")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Werror -Wall -Wextra -Wundef -Wno-unused -pedantic-errors ")


add_executable(graph_generator main.cpp)

# Optional inclusion of OpenMP to enable multithreading.
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(graph_generator PUBLIC OpenMP::OpenMP_CXX)
endif()
