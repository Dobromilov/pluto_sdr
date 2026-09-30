cmake_minimum_required(VERSION 3.16)

project(PlutoSDRExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SoapySDR CONFIG REQUIRED)

add_executable(pluto_test main.cpp)

target_link_libraries(pluto_test PRIVATE SoapySDR)