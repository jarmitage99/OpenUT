#pragma once

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include <SDL.h>
#include <glad/gl.h>

namespace testgl {
    struct OffscreenContext {
        SDL_Window* window = nullptr;
        SDL_GLContext context = nullptr;

        OffscreenContext(int width = 640, int height = 480)
        {
            SDL_SetMainReady();
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
                return;
            }
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
            window = SDL_CreateWindow("test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (!window) {
                return;
            }
            context = SDL_GL_CreateContext(window);
            if (context && !gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
                SDL_GL_DeleteContext(context);
                context = nullptr;
            }
        }

        ~OffscreenContext()
        {
            if (context) {
                SDL_GL_DeleteContext(context);
            }
            if (window) {
                SDL_DestroyWindow(window);
            }
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }

        OffscreenContext(const OffscreenContext&) = delete;
        OffscreenContext& operator=(const OffscreenContext&) = delete;
    };
}
