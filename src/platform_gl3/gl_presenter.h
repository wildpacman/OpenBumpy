#pragma once

#include <SDL3/SDL.h>

#include "core/indexed_framebuffer.h"
#include "platform_gl3/gl33.h"

#include <cstdint>
#include <vector>

namespace bumpy {

// OpenGL 3.3 presentation of the composed 320x200 indexed frame: one letterboxed
// quad with a pixel-art scaling shader (crisp interior, <=1px anti-aliased pixel
// edges -- the GL twin of SDL_SCALEMODE_PIXELART). Also owns the GL context that
// the 3D scene renderer (Task 12) draws with.
// Size of the actual GL framebuffer, which is what glViewport must match.
//
// This is deliberately NOT SDL_GetWindowSizeInPixels. On the web the framebuffer is the
// canvas backing store, and SDL's window size is a derived number that has been seen to
// disagree with it -- it did so on every fullscreen entry until the web build stopped
// routing fullscreen through SDL (platform_set_fullscreen in
// src/platform_sdl3/sdl_app.cpp). A viewport smaller than the framebuffer lands
// bottom-left, because that is where GL's origin is, which is a quiet and confusing way
// to fail. Asking the canvas costs nothing and cannot be wrong by construction, so it
// stays even though the divergence that motivated it is gone.
void gl_drawable_size(SDL_Window* window, int* w, int* h);

class GlPresenter {
public:
    // Creates the GL 3.3 core context on `window` (created with SDL_WINDOW_OPENGL
    // and the 3.3 core attributes) and compiles the flat-path shader. Throws
    // std::runtime_error on any failure; the caller falls back to SDL_Renderer.
    explicit GlPresenter(SDL_Window* window);
    ~GlPresenter();
    GlPresenter(const GlPresenter&) = delete;
    GlPresenter& operator=(const GlPresenter&) = delete;

    [[nodiscard]] const Gl33& gl() const noexcept { return gl_; }

    // Upload `frame` and draw it letterboxed at logical 320 x logical_h
    // (200 = 16:10 square pixels, 240 = 4:3 CRT) into the backbuffer + swap.
    void present_flat(const IndexedFramebuffer& frame, int logical_h);

    // The same flat draw into an offscreen (w x h) target; returns RGBA rows
    // top-to-bottom. For --present-parity and headless dumps.
    [[nodiscard]] std::vector<std::uint8_t> render_flat_offscreen(
        const IndexedFramebuffer& frame, int w, int h, int logical_h);

private:
    void upload_frame(const IndexedFramebuffer& frame);
    void draw_flat(const IndexedFramebuffer& frame, int target_w, int target_h,
                   int logical_h);

    SDL_Window* window_{};
    SDL_GLContext context_{};
    Gl33 gl_{};
    GLuint program_{};
    GLuint vao_{};
    GLuint vbo_{};
    GLuint frame_tex_{};
    GLint u_tex_{};
    GLint u_tex_size_{};
};

}  // namespace bumpy
