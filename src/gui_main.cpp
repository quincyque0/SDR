#include "sdr_visualizer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

static void glfw_error_callback(int error, const char *description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int argc, char **argv) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    // GL 3.3 + GLSL 150 for macOS
    const char *glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow *window = glfwCreateWindow(1600, 960, "SDR Studio Pro — Real-Time Signal & Modulation Visualizer", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable V-Sync (60+ FPS lock)

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    sdrviz::App app;
    app.LoadFonts(io);
    app.Init();

    // Auto-load PCM file if available or passed via command line
    if (argc > 1) {
        app.OpenFile(argv[1], sdrviz::FileFmt::CS16);
    } else if (sdrviz::FileExists("Rxdata.pcm")) {
        app.OpenFile("Rxdata.pcm", sdrviz::FileFmt::CS16);
    } else if (sdrviz::FileExists("../Rxdata.pcm")) {
        app.OpenFile("../Rxdata.pcm", sdrviz::FileFmt::CS16);
    }

    app.Start();

    const ImVec4 clear_color = ImVec4(0.071f, 0.075f, 0.086f, 1.00f); // #121316 Studio Dark

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.Render();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    app.Stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
