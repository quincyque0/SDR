cmake_minimum_required(VERSION 3.16)
project(PlutoSDR C)

set(CMAKE_BUILD_TYPE Debug)

# Опция для включения или отключения установки зависимостей
option(INSTALL_DEPS "Установить зависимости" ON)
option(PLUTO_TIMESTAMP "Build pluto with timestamp" ON)

# RF frontend driver
find_package(SoapySDR REQUIRED)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")
    set(MAIN_SOURCE_FILES src/main.c)
else()
    set(MAIN_SOURCE_FILES main.c)
endif()

# Добавляем исполняемый файл
add_executable(main ${MAIN_SOURCE_FILES})

target_link_libraries(main ${SoapySDR_LIBRARIES} m)

