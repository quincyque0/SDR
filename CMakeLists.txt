cmake_minimum_required(VERSION 3.16)
project(PlutoSDR CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

option(INSTALL_DEPS "Install dependencies" ON)
option(PLUTO_TIMESTAMP "Build pluto with timestamp" ON)

find_package(SoapySDR REQUIRED)

include_directories(include)

set(MAIN_SOURCE_FILES
    main.cpp
    src/config.cpp
    src/sdr_device.cpp
    src/signal_gen.cpp
    src/sdr_stream.cpp
)

add_executable(main ${MAIN_SOURCE_FILES})

target_link_libraries(main ${SoapySDR_LIBRARIES})
