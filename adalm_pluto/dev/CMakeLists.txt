cmake_minimum_required(VERSION 3.16)
project(PlutoSDR CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SoapySDR REQUIRED)

# Совмещённый RX/TX с timestamp.
add_executable(lab_06 pluto_sdr_timestamp/main.cpp)
target_link_libraries(lab_06 PRIVATE SoapySDR)

# Два независимых узла.
add_executable(tx_node pluto_tx_rx_link/tx_main.cpp)
target_link_libraries(tx_node PRIVATE SoapySDR)

add_executable(rx_node pluto_tx_rx_link/rx_main.cpp)
target_link_libraries(rx_node PRIVATE SoapySDR)
