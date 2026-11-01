// nuttool-gui entry point: SDL3 window + OpenGL 3.3 + Dear ImGui.
#include "app.hpp"

#include <SDL3/SDL_main.h>
#include <glad/glad.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <memory>

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NutTool", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Open at 80% of the primary display's usable area.
    SDL_Rect usable{0, 0, 1440, 900};
    SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    int winW = std::max(1024, usable.w * 4 / 5), winH = std::max(700, usable.h * 4 / 5);
    SDL_Window* window = SDL_CreateWindow("NutTool - Banjo-Kazooie: Nuts & Bolts asset editor", winW, winH,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!gl || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NutTool", "OpenGL 3.3 is required.", nullptr);
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    static std::string iniPath;
    if (char* pref = SDL_GetPrefPath("NutTool", "NutTool")) {
        iniPath = std::string(pref) + "imgui.ini";
        SDL_free(pref);
        io.IniFilename = iniPath.c_str();
    }
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    {
        auto app = std::make_unique<nut::gui::App>(window);
        // SDL3 hands main() UTF-8 arguments on Windows.
        // --model <name> / --texture <name> open catalog items once indexing finishes.
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if ((a == "--model" || a == "--texture") && i + 1 < argc) app->openWhenIndexed((a == "--model" ? "model:" : "tex:") + std::string(argv[++i]));
            else if (a == "--anim" && i + 1 < argc) app->setStartupAnimation(argv[++i]);
            else app->openAny(nut::gui::fromUtf8(a));
        }
        while (!app->quitRequested()) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) app->openAny(nut::gui::fromUtf8(e.drop.data));
                if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) app->updateScale();
                if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window)))
                    app->requestQuit();
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(20);
                continue;
            }
            app->beforeNewFrame();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app->frame();
            ImGui::Render();
            int fbw = 0, fbh = 0;
            SDL_GetWindowSizeInPixels(window, &fbw, &fbh);
            glViewport(0, 0, fbw, fbh);
            glClearColor(0.12f, 0.12f, 0.13f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(window);
        }
        // Documents own GL objects, so they go before the context.
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
