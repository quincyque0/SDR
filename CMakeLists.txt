cmake_minimum_required(VERSION 3.16)
project(PlutoSDR CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

option(INSTALL_DEPS "Install dependencies" ON)
option(PLUTO_TIMESTAMP "Build pluto with timestamp" ON)
option(BUILD_GUI "Build Dear ImGui SDR Visualizer" ON)

# SoapySDR configuration
set(SoapySDR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SoapySDR/dist/share/cmake/SoapySDR" CACHE PATH "Path to SoapySDRConfig.cmake" FORCE)
find_package(SoapySDR REQUIRED)

include_directories(include)

# CLI Streaming Tool
set(MAIN_SOURCE_FILES
    main.cpp
    src/config.cpp
    src/sdr_device.cpp
    src/signal_gen.cpp
    src/sdr_stream.cpp
)

add_executable(main ${MAIN_SOURCE_FILES})
target_link_libraries(main ${SoapySDR_LIBRARIES})

# Dear ImGui + ImPlot Visualizer
if(BUILD_GUI)
    include(FetchContent)

    # ImGui (Docking branch)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        docking
    )

    # ImPlot (v0.17)
    FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG        v0.17
    )

    FetchContent_MakeAvailable(imgui implot)

    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 QUIET)
    if(NOT glfw3_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW REQUIRED glfw3)
    endif()

    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    set(IMPLOT_SOURCES
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
    )

    set(GUI_SOURCES
        src/gui_main.cpp
        ${IMGUI_SOURCES}
        ${IMPLOT_SOURCES}
    )

    add_executable(sdr_gui ${GUI_SOURCES})

    target_include_directories(sdr_gui PRIVATE
        include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
        ${OPENGL_INCLUDE_DIR}
    )

    if(TARGET glfw)
        target_link_libraries(sdr_gui PRIVATE glfw)
    elseif(TARGET glfw3::glfw3)
        target_link_libraries(sdr_gui PRIVATE glfw3::glfw3)
    else()
        target_link_libraries(sdr_gui PRIVATE ${GLFW_LIBRARIES})
        target_include_directories(sdr_gui PRIVATE ${GLFW_INCLUDE_DIRS})
    endif()

    if(APPLE)
        target_link_libraries(sdr_gui PRIVATE ${SoapySDR_LIBRARIES} "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
    else()
        target_link_libraries(sdr_gui PRIVATE ${SoapySDR_LIBRARIES} ${OPENGL_LIBRARIES})
    endif()
endif()
