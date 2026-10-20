cmake_minimum_required(VERSION 3.16)
project(PlutoSDR CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SoapySDR REQUIRED)

# Совмещённый RX/TX с timestamp.
add_executable(lab_06 pluto_sdr_timestamp/main.cpp)
target_link_libraries(lab_06 PRIVATE SoapySDR)

# Один исполняемый файл; каждый узел остаётся в собственном исходном файле.
add_executable(pluto_sdr
    pluto_tx_rx_link/main.cpp
    pluto_tx_rx_link/sdr_runner.cpp)
target_link_libraries(pluto_sdr PRIVATE SoapySDR)
