layout(location = 0) in vec3 a_pos;    // GL space: x right, y up, board centre origin
layout(location = 1) in vec2 a_uv;
layout(location = 2) in float a_shade;
uniform mat4 u_mvp;
out vec2 v_uv;
out float v_shade;
out vec3 v_world;
void main() {
    v_uv = a_uv;
    v_shade = a_shade;
    v_world = a_pos;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
