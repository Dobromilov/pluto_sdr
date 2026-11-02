cmake_minimum_required(VERSION 3.16)
project(PlutoSDR CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SoapySDR REQUIRED)

add_executable(pluto_sdr
    main.cpp
    sdr_runner.cpp)
target_include_directories(pluto_sdr PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(pluto_sdr PRIVATE SoapySDR)
