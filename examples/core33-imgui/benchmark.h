// Render-only matrix: same logical UI at each resolution, glFinish per frame.
// Window previews are outside measurement; no high-refresh scanout claim.
#include "benchmark_timing.h"

#ifndef PS5_IMGUI_BENCHMARK_CASE
#define PS5_IMGUI_BENCHMARK_CASE -1
#endif
static_assert(PS5_IMGUI_BENCHMARK_CASE >= -1 && PS5_IMGUI_BENCHMARK_CASE < 12,
              "benchmark case must be -1 (matrix) or 0..11");

#ifndef PS5_IMGUI_HOST_REFERENCE
// Existing read-only diagnostic; glFinish has already retired the frame.
extern "C" int ps5_egl_current_draw_status(unsigned*);
#endif

static bool bench_draw(GLuint fbo, int width, int height, int target, unsigned frame)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    io.DisplayFramebufferScale = ImVec2(width / 1920.0f, height / 1080.0f);
    io.DeltaTime = 1.0f / target;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(80, 60));
    ImGui::SetNextWindowSize(ImVec2(1760, 960));
    ImGui::Begin("OpenGL performance benchmark", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("Completed offscreen rendering: %d x %d, target %d FPS", width, height, target);
    ImGui::TextUnformatted("30-second samples; TV previews are not part of the measurement.");
    ImGui::TextUnformatted("Fonts, animated geometry and alpha blending; not a game benchmark.");
    ImGui::ProgressBar((frame % 240) / 239.0f, ImVec2(-1, 30));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float x = 960 + std::sin(frame * 0.025f) * 400;
    draw->AddCircleFilled(ImVec2(x, 470), 70, IM_COL32(45, 215, 245, 255), 48);
    draw->AddRectFilled(ImVec2(300, 650), ImVec2(1000, 900), IM_COL32(130, 100, 255, 255));
    draw->AddRectFilled(ImVec2(700, 700), ImVec2(1400, 950), IM_COL32(245, 110, 170, 160));
    ImGui::End();
    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.035f, 0.047f, 0.09f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return check(glGetError() == GL_NO_ERROR, "benchmark draw");
}

static bool bench_probe(unsigned id, const char* phase, int width, int height, unsigned frame)
{
    const float x = 960 + std::sin(frame * 0.025f) * 400;
    const float points[][2] = {{1, 1}, {x, 470}, {800, 800}};
    const int colors[][4] = {{9, 12, 23, 255}, {45, 215, 245, 255}, {202, 106, 202, 255}};
    bool ok = true;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned char p[4] = {};
        glReadPixels(static_cast<int>(points[i][0] * width / 1920),
                     height - 1 - static_cast<int>(points[i][1] * height / 1080),
                     1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        for (unsigned c = 0; c < 4; ++c) ok = abs(p[c] - colors[i][c]) <= 2 && ok;
    }
    ok = check(glGetError() == GL_NO_ERROR && ok, "benchmark pixels") && ok;
    printf("[ps5-imgui-bench] probe case=%u phase=%s samples=3 status=%d\n", id, phase, ok ? 0 : 1);
    return ok;
}

static bool bench_case(unsigned id, GLuint fbo, int width, int height, int target)
{
    // ponytail: bounded 30-second samples, not an unbounded telemetry buffer.
    // Increase the checked capacity if later benchmarks exceed 120 FPS/30 s.
    const unsigned capacity = 8192;
    double* render_ms = static_cast<double*>(calloc(2 * capacity, sizeof(double)));
    if (!check(render_ms != nullptr, "benchmark samples allocation")) return false;
    double* frame_ms = render_ms + capacity;
#ifdef PS5_IMGUI_HOST_REFERENCE
    const unsigned warmup_limit = 2;
#else
    const unsigned warmup_limit = 30;
#endif
    unsigned frame = 0, count = 0, render_misses = 0, frame_misses = 0;
    unsigned first_draws = 0, last_draws = 0;
    bool ok = true;
    double render_sum = 0, start = 0, end = 0;
    const double budget_ms = 1000.0 / target;
    printf("[ps5-imgui-bench] begin case=%u width=%d height=%d target=%d mode=offscreen-completed\n",
           id, width, height, target);
    const double warmup_start = bench_seconds();
    ok = check(warmup_start >= 0, "benchmark warm-up clock");
    while (frame < warmup_limit && ok) {
        ok = bench_draw(fbo, width, height, target, frame);
        glFinish();
        ok = check(glGetError() == GL_NO_ERROR, "benchmark warm-up completion") && ok;
        ++frame;
        const double now = bench_seconds();
        ok = check(now >= warmup_start, "benchmark warm-up clock") && ok;
        if (frame >= 2 && now - warmup_start >= 1.0) break;
    }
    const unsigned warmup = frame;
    if (ok) ok = bench_probe(id, "warmup", width, height, frame - 1);
#ifndef PS5_IMGUI_HOST_REFERENCE
    if (ok) ok = check(ps5_egl_current_draw_status(&first_draws) == 0, "benchmark initial driver status");
    last_draws = first_draws;
#endif
    if (ok) {
        start = end = bench_seconds();
        ok = check(start >= 0, "benchmark start clock");
    }
    while (ok) {
#ifdef PS5_IMGUI_HOST_REFERENCE
        if (count == 6) break;
#else
        if (end - start >= 30.0) break;
#endif
        if (!check(count < capacity, "benchmark sample capacity")) { ok = false; break; }
        const double begin = bench_seconds(), previous = end;
        ok = check(begin >= previous, "benchmark frame clock") &&
             bench_draw(fbo, width, height, target, frame);
        glFinish(); // Timing includes real GPU completion, never just submission.
#ifndef PS5_IMGUI_HOST_REFERENCE
        unsigned draws = 0;
        ok = check(ps5_egl_current_draw_status(&draws) == 0 && draws >= last_draws &&
                   draws - last_draws >= 2, "benchmark retired driver draws") && ok;
        last_draws = draws;
#endif
        const double ready = bench_seconds();
        ok = check(glGetError() == GL_NO_ERROR && ready >= begin, "benchmark GPU completion") && ok;
#ifndef PS5_IMGUI_HOST_REFERENCE
        if (ok) ok = check(bench_wait(start + (count + 1.0) / target), "benchmark pacing");
#else
        (void)bench_wait;
#endif
        end = bench_seconds();
        ok = check(end >= ready, "benchmark paced clock") && ok;
        if (!ok) break;
        render_ms[count] = (ready - begin) * 1000;
        frame_ms[count] = (end - previous) * 1000;
        render_sum += render_ms[count];
        render_misses += render_ms[count] > budget_ms;
        frame_misses += frame_ms[count] > budget_ms + 0.25; // Report the 0.25 ms pacing tolerance.
        ++count;
        ++frame;
    }
    if (ok) ok = bench_probe(id, "final", width, height, frame - 1);
    if (ok) ok = check(count >= 2 && end > start, "benchmark measured frames");
    if (ok) {
        qsort(render_ms, count, sizeof(double), bench_compare);
        qsort(frame_ms, count, sizeof(double), bench_compare);
        printf("[ps5-imgui-bench] result case=%u warmup=%u frames=%u seconds=%.6f fps=%.6f "
               "render_mean_ms=%.6f render_p50_ms=%.6f render_p95_ms=%.6f render_p99_ms=%.6f "
               "frame_p50_ms=%.6f frame_p95_ms=%.6f frame_p99_ms=%.6f render_misses=%u frame_misses=%u driver_draws=%u status=0\n",
               id, warmup, count, end - start, count / (end - start), render_sum / count,
               bench_percentile(render_ms, count, 50), bench_percentile(render_ms, count, 95),
               bench_percentile(render_ms, count, 99), bench_percentile(frame_ms, count, 50),
               bench_percentile(frame_ms, count, 95), bench_percentile(frame_ms, count, 99),
               render_misses, frame_misses, last_draws - first_draws);
    }
    free(render_ms);
    return ok;
}

static bool render_frames(EGLDisplay display, EGLSurface surface)
{
    EGLint window_width = 0, window_height = 0;
    if (!check(eglQuerySurface(display, surface, EGL_WIDTH, &window_width) &&
               eglQuerySurface(display, surface, EGL_HEIGHT, &window_height) &&
               window_width == 1920 && window_height == 1080, "benchmark preview window")) return false;
    const int sizes[][2] = {{1920, 1080}, {2560, 1440}, {3840, 2160}};
    const int rates[] = {30, 60, 90, 120};
    GLuint fbo = 0, color = 0;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &color);
    unsigned completed = 0, id = 0;
    bool ok = check(fbo && color && glGetError() == GL_NO_ERROR, "benchmark objects");
    for (const auto& size : sizes) {
        if (!ok) break;
        if (PS5_IMGUI_BENCHMARK_CASE >= 0 &&
            static_cast<unsigned>(PS5_IMGUI_BENCHMARK_CASE) / 4 != id / 4) {
            id += 4;
            continue;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindRenderbuffer(GL_RENDERBUFFER, color);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, size[0], size[1]);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
        ok = check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
                   glGetError() == GL_NO_ERROR, "benchmark framebuffer allocation");
        for (int rate : rates) {
            const unsigned current = id++;
            if (PS5_IMGUI_BENCHMARK_CASE >= 0 &&
                current != static_cast<unsigned>(PS5_IMGUI_BENCHMARK_CASE)) continue;
            if (!ok) break;
            // One preview per case, excluded from all measured frame counts/times.
            ok = bench_draw(0, window_width, window_height, rate, 0) &&
                 check(eglSwapBuffers(display, surface), "benchmark preview swap");
            if (ok) ok = bench_case(current, fbo, size[0], size[1], rate);
            if (ok) ++completed;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(1, &color);
    glDeleteFramebuffers(1, &fbo);
    ok = check(glGetError() == GL_NO_ERROR && completed == (PS5_IMGUI_BENCHMARK_CASE < 0 ? 12u : 1u),
               "benchmark cleanup/completeness") && ok;
    printf("[ps5-imgui-bench] finished cases=%u status=%d\n", completed, ok ? 0 : 1);
    return ok;
}
