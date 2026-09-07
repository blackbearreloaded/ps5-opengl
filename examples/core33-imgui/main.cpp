// Bounded renderer integration: unmodified Dear ImGui, public installed GL/EGL only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"

static bool check(bool ok, const char* stage)
{
    if (!ok)
        printf("[ps5-imgui] FAIL %s gl=0x%x egl=0x%x\n", stage, glGetError(), eglGetError());
    return ok;
}

#ifdef PS5_IMGUI_BENCHMARK
#include "benchmark.h"
#elif defined(PS5_IMGUI_TV_DEMO)
#include "tv_demo.h"
#else
static bool pixel(const unsigned char* pixels, int width, int height, int scale,
                  int x, int y, int r, int g, int b)
{
    const unsigned char* p = pixels + ((height - 1 - y * scale) * width + x * scale) * 4;
    const bool ok = abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 &&
                    abs(p[2] - b) <= 2 && p[3] == 255;
    if (!ok)
        printf("[ps5-imgui] pixel %d,%d got=%u,%u,%u,%u expected=%d,%d,%d,255\n",
               x, y, p[0], p[1], p[2], p[3], r, g, b);
    return ok;
}

static bool render_frames(EGLDisplay display, EGLSurface surface)
{
    GLuint fbo = 0, color = 0, renderbuffer = 0, image = 0, vao = 0, buffer = 0, sampler = 0;
    static const unsigned char texels[] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255
    };
    // These bindings/enables are intentionally awkward for an overlay renderer.
    static const GLenum states[] = {
        GL_CURRENT_PROGRAM, GL_ACTIVE_TEXTURE, GL_VERTEX_ARRAY_BINDING,
        GL_ARRAY_BUFFER_BINDING, GL_ELEMENT_ARRAY_BUFFER_BINDING,
        GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA,
        GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA, GL_DRAW_FRAMEBUFFER_BINDING
    };
    static const GLenum enables[] = {
        GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_STENCIL_TEST,
        GL_SCISSOR_TEST, GL_PRIMITIVE_RESTART
    };
    unsigned char* pixels = static_cast<unsigned char*>(malloc(640 * 480 * 4));
    if (!check(pixels != nullptr, "readback allocation"))
        return false;
    bool ok = true, all_frames_ok = true;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &color);
    glGenRenderbuffers(1, &renderbuffer);
    glGenTextures(1, &image);
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &buffer);
    glGenSamplers(1, &sampler);
    glBindTexture(GL_TEXTURE_2D, image);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    for (int frame = 0; frame < 6 && ok; ++frame) {
        const int scale = frame < 3 ? 1 : 2;
        const int width = 320 * scale, height = 240 * scale;
        // Exercise resource destruction/recreation, not just warmed-up rendering.
        if (frame == 3)
            ImGui_ImplOpenGL3_DestroyDeviceObjects();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(320, 240);
        io.DisplayFramebufferScale = ImVec2(static_cast<float>(scale), static_cast<float>(scale));
        io.DeltaTime = 1.0f / 60.0f;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        draw->AddRectFilled(ImVec2(8, 8), ImVec2(72, 56), IM_COL32(0, 0, 255, 255));
        if (frame % 3 == 1)
            draw->AddDrawCmd(); // Compare merged geometry with a draw boundary.
        draw->AddRectFilled(ImVec2(24, 16), ImVec2(56, 48), IM_COL32(255, 0, 0, 128));
        draw->PushClipRect(ImVec2(96, 16), ImVec2(128, 48), true);
        draw->AddRectFilled(ImVec2(80, 8), ImVec2(144, 56), IM_COL32(0, 255, 0, 255));
        draw->PopClipRect();
        const int shift = (frame % 3) * 4;
        draw->AddImage(static_cast<ImTextureID>(image), ImVec2(160.0f + shift, 8), ImVec2(224.0f + shift, 72));
        draw->AddText(ImVec2(8, 80), IM_COL32_WHITE, "OpenGL 3.3 / ImGui");
        ImGui::SetNextWindowPos(ImVec2(8, 120));
        ImGui::SetNextWindowSize(ImVec2(280, 105));
        ImGui::Begin("Installed SDK", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
        ImGui::Text("Frame %d, scale %d", frame, scale);
        ImGui::ProgressBar((frame + 1) / 6.0f);
        ImGui::Button("Upstream renderer");
        ImGui::End();
        ImGui::Render();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (frame % 3 == 2) {
            glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        }
        if (!check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
                   glGetError() == GL_NO_ERROR, "complete framebuffer / setup")) {
            ok = false;
            break;
        }
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, image);
        glBindSampler(0, sampler);
        glActiveTexture(GL_TEXTURE3);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
        glUseProgram(0);
        glViewport(3, 5, 71, 73);
        glScissor(7, 9, 31, 33);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
        for (GLenum enable : enables)
            glEnable(enable);
        glDisable(GL_BLEND);
        GLint before[IM_ARRAYSIZE(states)] = {}, after = 0;
        for (int i = 0; i < IM_ARRAYSIZE(states); ++i)
            glGetIntegerv(states[i], &before[i]);
        ImDrawData* data = ImGui::GetDrawData();
        if (!check(data && data->TotalVtxCount > 0 && data->TotalIdxCount > 0, "nonempty draw data")) {
            ok = false;
            break;
        }
        ImGui_ImplOpenGL3_RenderDrawData(data);
        glFinish();
        ok = check(glGetError() == GL_NO_ERROR, "render");
        for (int i = 0; i < IM_ARRAYSIZE(states); ++i) {
            glGetIntegerv(states[i], &after);
            ok = check(after == before[i], "restored binding/blend state") && ok;
        }
        for (GLenum enable : enables)
            ok = check(glIsEnabled(enable) == (enable != GL_BLEND), "restored enable") && ok;
        GLint viewport[4] = {}, scissor[4] = {}, mode[2] = {};
        const GLint expected_viewport[] = {3, 5, 71, 73}, expected_scissor[] = {7, 9, 31, 33};
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissor);
        glGetIntegerv(GL_POLYGON_MODE, mode);
        ok = check(!memcmp(viewport, expected_viewport, sizeof(viewport)) &&
                   !memcmp(scissor, expected_scissor, sizeof(scissor)) &&
                   mode[0] == GL_LINE && mode[1] == GL_LINE, "restored rectangles/polygon mode") && ok;
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &after);
        ok = check(after == static_cast<GLint>(image), "restored texture") && ok;
        glGetIntegerv(GL_SAMPLER_BINDING, &after);
        ok = check(after == static_cast<GLint>(sampler), "restored sampler") && ok;
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        ok = check(glGetError() == GL_NO_ERROR, "readback") && ok;
        if (!ok)
            break; // Continue pixel mismatches only, never GL/setup errors.
        ok = pixel(pixels, width, height, scale, 4, 4, 0, 0, 0) && ok;
        ok = pixel(pixels, width, height, scale, 16, 24, 0, 0, 255) && ok;
        ok = pixel(pixels, width, height, scale, 40, 24, 128, 0, 127) && ok;
        ok = pixel(pixels, width, height, scale, 88, 24, 0, 0, 0) && ok;
        ok = pixel(pixels, width, height, scale, 112, 24, 0, 255, 0) && ok;
        ok = pixel(pixels, width, height, scale, 136, 24, 0, 0, 0) && ok;
        ok = pixel(pixels, width, height, scale, 176 + shift, 24, 255, 0, 0) && ok;
        ok = pixel(pixels, width, height, scale, 208 + shift, 24, 0, 255, 0) && ok;
        ok = pixel(pixels, width, height, scale, 176 + shift, 56, 0, 0, 255) && ok;
        ok = pixel(pixels, width, height, scale, 208 + shift, 56, 255, 255, 255) && ok;
        unsigned ink = 0, partial = 0;
        bool gray = true;
        for (int y = 80 * scale; y < 96 * scale; ++y)
            for (int x = 8 * scale; x < 144 * scale; ++x) {
                const unsigned char* p = pixels + ((height - 1 - y) * width + x) * 4;
                ink += p[0] != 0;
                partial += p[0] > 0 && p[0] < 255;
                const bool pixel_gray = p[0] == p[1] && p[1] == p[2] && p[3] == 255;
                if (gray && !pixel_gray)
                    printf("[ps5-imgui] first nongray font pixel %d,%d rgba=%u,%u,%u,%u\n",
                           x, y, p[0], p[1], p[2], p[3]);
                gray = gray && pixel_gray;
            }
        // The default bitmap font is binary at native scale; filtering at 2x adds coverage.
        ok = check(gray && ink > 30u * scale * scale && ink < 1800u * scale * scale &&
                   (scale == 1 || partial > 0),
                   "font atlas alpha coverage") && ok;
        // Present the validated image; no screenshots are part of the oracle.
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width, height, 0, 0, 1280, 960, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        const bool presented = check(glGetError() == GL_NO_ERROR && eglSwapBuffers(display, surface), "present");
        printf("[ps5-imgui] frame=%d scale=%d split=%d renderbuffer=%d vertices=%d indices=%d font-ink=%u partial=%u %s\n",
               frame, scale, frame % 3 == 1, frame % 3 == 2, data->TotalVtxCount, data->TotalIdxCount, ink, partial,
               ok && presented ? "PASS" : "FAIL");
        all_frames_ok = all_frames_ok && ok;
        ok = presented;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glBindSampler(0, 0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteSamplers(1, &sampler);
    glDeleteBuffers(1, &buffer);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &image);
    glDeleteTextures(1, &color);
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteFramebuffers(1, &fbo);
    free(pixels);
    return check(glGetError() == GL_NO_ERROR, "resource cleanup") && ok && all_frames_ok;
}
#endif

int main()
{
#ifdef PS5_IMGUI_HOST_REFERENCE
    const EGLint surface_type = EGL_PBUFFER_BIT;
#else
    const EGLint surface_type = EGL_WINDOW_BIT;
#endif
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, surface_type, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    const EGLint context_attributes[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE
    };
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig config = nullptr;
    EGLint count = 0;
    int result = 1;
    if (check(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr) &&
              eglBindAPI(EGL_OPENGL_API) && eglChooseConfig(display, config_attributes, &config, 1, &count) &&
              count == 1, "EGL initialization")) {
#ifdef PS5_IMGUI_HOST_REFERENCE
        const EGLint pbuffer_attributes[] = {EGL_WIDTH, 1920, EGL_HEIGHT, 1080, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, pbuffer_attributes);
#else
        surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, nullptr);
#endif
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        if (check(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
                  eglMakeCurrent(display, surface, surface, context), "EGL context")) {
            printf("[ps5-imgui] ImGui %s upstream=f5befd2d29e66809cd1110a152e375a7f1981f06 GL=%s\n",
                   IMGUI_VERSION, glGetString(GL_VERSION));
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().LogFilename = nullptr;
#ifdef PS5_IMGUI_TV_DEMO
            ImFontConfig font;
            font.SizePixels = 28.0f;
            ImGui::GetIO().Fonts->AddFontDefault(&font);
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            ImGui::GetIO().ConfigNavCursorVisibleAlways = true;
            ImGui::StyleColorsDark();
            ImGui::GetStyle().ScaleAllSizes(1.7f);
#endif
            if (check(ImGui_ImplOpenGL3_Init("#version 330 core"), "renderer initialization")) {
                result = render_frames(display, surface) ? 0 : 1;
                ImGui_ImplOpenGL3_Shutdown();
            }
            ImGui::DestroyContext();
        }
    }
    EGLBoolean cleanup = EGL_TRUE;
    if (display != EGL_NO_DISPLAY) {
        cleanup &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) cleanup &= eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) cleanup &= eglDestroySurface(display, surface);
        cleanup &= eglTerminate(display);
    }
    if (!check(cleanup && eglGetError() == EGL_SUCCESS, "EGL cleanup")) result = 1;
    printf("[ps5-imgui] finished status=%d\n", result);
    return result;
}
