#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "log.hpp"
#include "shorthand.hpp"

#include "app.hpp"
#include "db.hpp"

#include <cstdio>

#include "windows.h"

#include <GLFW/glfw3.h> // Will drag system OpenGL headers

static void glfw_error_callback(int error, const char *description)
{
    LOG_ERROR("GLFW Error {}: {}\n", error, description);
}

static GLFWwindow *gWindow;
static constexpr char kDefaultWindowName[] = "Controller App";
void draw_ui(App *, Comms *);

void set_window_title(std::string_view title)
{
    auto new_title = std::format("{} - {}", kDefaultWindowName, title);
    glfwSetWindowTitle(gWindow, new_title.c_str());
}

int app_main()
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    if (!db_open()) {
        return -1;
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return 1;
    }

    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+
    // only glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // 3.0+ only

    gWindow = glfwCreateWindow(1920, 1080, kDefaultWindowName, nullptr, nullptr);
    if (gWindow == nullptr) {
        return 1;
    }

    glfwMakeContextCurrent(gWindow);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    ImPlot::CreateContext();

    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    ImGui_ImplGlfw_InitForOpenGL(gWindow, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    App app;
    Comms comms;

    enumerate_com_ports(&comms.enumerated_ports);
    comms.read_data_buffer = (u8 *)calloc(1, Comms::kReadDataMaxSize);

    db_ccd_result_get_all(&app.ccd_operations);

    while (!glfwWindowShouldClose(gWindow)) {
        glfwPollEvents();

        if (glfwGetWindowAttrib(gWindow, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        comms.read_data_size += handle_incomming_data(
            comms.com_connection, comms.read_data_buffer, comms.read_data_size, Comms::kReadDataMaxSize);

        handle_commands(&app, &comms);
        draw_ui(&app, &comms);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(gWindow, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(gWindow);
    }

    ImPlot::DestroyContext();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(gWindow);
    glfwTerminate();

    db_close();

    return 0;
}

#if _WIN32
////////////////////////////////////////////////////////////////
//// Windows entrypoint
////////////////////////////////////////////////////////////////
int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    app_main();
}
#else
#endif
