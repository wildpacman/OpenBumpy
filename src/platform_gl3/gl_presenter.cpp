#include "platform_gl3/gl_presenter.h"

#include "platform_gl3/gl_util.h"
#include "video/viewport.h"

#include <stdexcept>

namespace {

// Pixel-art scaling: sample on a LINEAR texture but snap UVs so that pixels stay
// crisp except a <=1-screen-pixel ramp at texel boundaries. At integer scales the
// clamp saturates at +-0.5 texel = exact texel centers, i.e. bit-exact nearest.
constexpr const char* kFlatVert = R"GLSL(
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFlatFrag = R"GLSL(
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
void main() {
    vec2 st = v_uv * u_tex_size;
    vec2 seam = floor(st + 0.5);
    vec2 uv = seam + clamp((st - seam) / fwidth(st), -0.5, 0.5);
    o_color = texture(u_tex, uv / u_tex_size);
}
)GLSL";

}  // namespace

namespace bumpy {

GlPresenter::GlPresenter(SDL_Window* window) : window_(window) {
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_GL_MakeCurrent(window_, context_)) {
        SDL_GL_DestroyContext(context_);
        throw std::runtime_error(SDL_GetError());
    }
    // The run loop self-paces on the VGA retrace clock; vsync would fight it.
    SDL_GL_SetSwapInterval(0);
    if (!load_gl33(gl_)) {
        SDL_GL_DestroyContext(context_);
        // Platform-neutral on purpose: this is the text a player sees when
        // SDL_GL_GetProcAddress misses an entry point, and in a browser the thing
        // that is missing is a WebGL2 (GLES 3.0) function, not an OpenGL 3.3 one.
        throw std::runtime_error(
            "required GL functions unavailable (need OpenGL 3.3 core, or WebGL2 in a browser)");
    }
    try {
        program_ = link_program(gl_, kFlatVert, kFlatFrag);
        u_tex_ = gl_.GetUniformLocation(program_, "u_tex");
        u_tex_size_ = gl_.GetUniformLocation(program_, "u_tex_size");

        // Fullscreen quad: pos.xy in NDC, uv with v=0 at the TOP (texture row 0 = frame
        // row 0), as two triangles.
        const float quad[] = {
            // x      y     u     v
            -1.0f,  1.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 1.0f,
        };
        gl_.GenVertexArrays(1, &vao_);
        gl_.BindVertexArray(vao_);
        gl_.GenBuffers(1, &vbo_);
        gl_.BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl_.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        gl_.EnableVertexAttribArray(0);
        gl_.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        gl_.EnableVertexAttribArray(1);
        gl_.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                reinterpret_cast<const void*>(2 * sizeof(float)));

        glGenTextures(1, &frame_tex_);
        glBindTexture(GL_TEXTURE_2D, frame_tex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 320, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        // LINEAR is what makes the pixel-art shader's edge ramp work; the shader's UV
        // snapping keeps interiors exact.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } catch (...) {
        SDL_GL_DestroyContext(context_);
        context_ = nullptr;
        throw;
    }
}

GlPresenter::~GlPresenter() {
    if (context_) {
        SDL_GL_MakeCurrent(window_, context_);
        if (frame_tex_ != 0) {
            glDeleteTextures(1, &frame_tex_);
        }
        if (vbo_ != 0) {
            gl_.DeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0) {
            gl_.DeleteVertexArrays(1, &vao_);
        }
        if (program_ != 0) {
            gl_.DeleteProgram(program_);
        }
        SDL_GL_DestroyContext(context_);
    }
}

void GlPresenter::upload_frame(const IndexedFramebuffer& frame) {
    const auto rgba = frame.to_rgba();  // r|g<<8|b<<16|a<<24 == RGBA bytes (LE)
    glBindTexture(GL_TEXTURE_2D, frame_tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width(), frame.height(), GL_RGBA,
                    GL_UNSIGNED_BYTE, rgba.data());
}

void GlPresenter::draw_flat(const IndexedFramebuffer& frame, int target_w, int target_h,
                            int logical_h) {
    upload_frame(frame);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const Viewport vp = compute_letterbox_viewport(target_w, target_h, 320, logical_h);
    glViewport(vp.x, vp.y, vp.w, vp.h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    gl_.UseProgram(program_);
    gl_.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame_tex_);
    gl_.Uniform1i(u_tex_, 0);
    gl_.Uniform2f(u_tex_size_, static_cast<float>(frame.width()),
                  static_cast<float>(frame.height()));
    gl_.BindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void GlPresenter::present_flat(const IndexedFramebuffer& frame, int logical_h) {
    SDL_GL_MakeCurrent(window_, context_);
    gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    draw_flat(frame, w, h, logical_h);
    SDL_GL_SwapWindow(window_);
}

std::vector<std::uint8_t> GlPresenter::render_flat_offscreen(const IndexedFramebuffer& frame,
                                                             int w, int h, int logical_h) {
    SDL_GL_MakeCurrent(window_, context_);
    OffscreenTarget target = make_offscreen_target(gl_, w, h);
    gl_.BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    draw_flat(frame, w, h, logical_h);
    auto pixels = read_target_rgba(gl_, target);
    gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
    destroy_offscreen_target(gl_, target);
    return pixels;
}

}  // namespace bumpy
