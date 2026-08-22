in vec2 v_uv;
in float v_shade;   // carries the shadow strength (kShadowAlpha)
in vec3 v_world;
out vec4 o_color;
uniform sampler2D u_tex;  // single-channel blurred silhouette in .r
void main() {
    o_color = vec4(0.0, 0.0, 0.0, texture(u_tex, v_uv).r * v_shade);
}
