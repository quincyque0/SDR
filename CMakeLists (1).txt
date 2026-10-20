cmake_minimum_required(VERSION 3.16)
project(PlutoSDR C)

set(CMAKE_BUILD_TYPE Debug)

# Option to enable or disable dependency installation
option(INSTALL_DEPS "Install dependencies" ON)
option(PLUTO_TIMESTAMP "Build pluto with timestamp" ON)

# RF frontend driver
find_package(SoapySDR REQUIRED)

set(MAIN_SOURCE_FILES
    main.c
)

# Add executable
add_executable(main ${MAIN_SOURCE_FILES})

# Link against the imported target provided by SoapySDR's CMake config
target_link_libraries(main PRIVATE SoapySDR)