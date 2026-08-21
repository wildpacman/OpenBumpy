#include "platform_gl3/gl_util.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace {

// The GLSL version line lives here rather than in the shader sources. WebGL2 speaks
// GLSL ES 3.00, which rejects "#version 330 core" and gives fragment shaders no
// default float precision; desktop GL 3.3 rejects "#version 300 es". The bodies are
// otherwise identical, so keeping one copy of each and supplying the dialect here
// beats forking seven shaders into fourteen -- the diorama's look was tuned once and
// should not have to be tuned twice.
#ifdef __EMSCRIPTEN__
constexpr const char* kGlslPreamble = "#version 300 es\nprecision highp float;\n";
#else
constexpr const char* kGlslPreamble = "#version 330 core\n";
#endif
constexpr GLint kGlslPreambleLen =
    static_cast<GLint>(std::char_traits<char>::length(kGlslPreamble));

}  // namespace

namespace bumpy {

GLuint compile_shader(const Gl33& gl, GLenum type, std::string_view source) {
    const GLuint shader = gl.CreateShader(type);
    // #version must be the first thing in a shader, so the preamble is element 0 of
    // the source array rather than a concatenation -- ShaderSource already takes one.
    const GLchar* srcs[2] = {kGlslPreamble, source.data()};
    const GLint lens[2] = {kGlslPreambleLen, static_cast<GLint>(source.size())};
    gl.ShaderSource(shader, 2, srcs, lens);
    gl.CompileShader(shader);
    GLint ok = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLchar log[2048] = {};
        GLsizei written = 0;
        gl.GetShaderInfoLog(shader, sizeof(log) - 1, &written, log);
        gl.DeleteShader(shader);
        throw std::runtime_error("shader compile failed: " + std::string(log, written));
    }
    return shader;
}

GLuint link_program(const Gl33& gl, std::string_view vert_src, std::string_view frag_src) {
    const GLuint vs = compile_shader(gl, GL_VERTEX_SHADER, vert_src);
    GLuint fs = 0;
    try {
        fs = compile_shader(gl, GL_FRAGMENT_SHADER, frag_src);
    } catch (...) {
        gl.DeleteShader(vs);
        throw;
    }
    const GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vs);
    gl.AttachShader(program, fs);
    gl.LinkProgram(program);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = GL_FALSE;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLchar log[2048] = {};
        GLsizei written = 0;
        gl.GetProgramInfoLog(program, sizeof(log) - 1, &written, log);
        gl.DeleteProgram(program);
        throw std::runtime_error("program link failed: " + std::string(log, written));
    }
    return program;
}

GLuint make_rgba_texture(std::span<const std::uint8_t> rgba, int w, int h, bool linear_filter) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    const GLint filter = linear_filter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

OffscreenTarget make_offscreen_target(const Gl33& gl, int w, int h) {
    OffscreenTarget target;
    target.w = w;
    target.h = h;
    glGenTextures(1, &target.color);
    glBindTexture(GL_TEXTURE_2D, target.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.GenRenderbuffers(1, &target.depth);
    gl.BindRenderbuffer(GL_RENDERBUFFER, target.depth);
    gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    gl.GenFramebuffers(1, &target.fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            target.color, 0);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                               target.depth);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        destroy_offscreen_target(gl, target);
        throw std::runtime_error("offscreen framebuffer incomplete");
    }
    return target;
}

void destroy_offscreen_target(const Gl33& gl, OffscreenTarget& target) {
    if (target.fbo != 0) {
        gl.DeleteFramebuffers(1, &target.fbo);
    }
    if (target.depth != 0) {
        gl.DeleteRenderbuffers(1, &target.depth);
    }
    if (target.color != 0) {
        glDeleteTextures(1, &target.color);
    }
    target = {};
}

std::vector<std::uint8_t> read_target_rgba(const Gl33& gl, const OffscreenTarget& target) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    std::vector<std::uint8_t> bottom_up(
        static_cast<std::size_t>(target.w) * target.h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, target.w, target.h, GL_RGBA, GL_UNSIGNED_BYTE, bottom_up.data());
    std::vector<std::uint8_t> top_down(bottom_up.size());
    const std::size_t stride = static_cast<std::size_t>(target.w) * 4;
    for (int y = 0; y < target.h; ++y) {
        const std::size_t src = static_cast<std::size_t>(target.h - 1 - y) * stride;
        std::copy_n(bottom_up.data() + src, stride, top_down.data() + y * stride);
    }
    return top_down;
}

}  // namespace bumpy
