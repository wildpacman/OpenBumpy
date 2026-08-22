in vec2 v_uv;
in float v_shade;
in vec3 v_world;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec2 u_vp_offset;
uniform vec2 u_vp_size;
void main() {
    vec4 c = texture(u_tex, v_uv);
    if (c.a < 0.5) {
        discard;
    }
    vec2 ndc = ((gl_FragCoord.xy - u_vp_offset) / u_vp_size) * 2.0 - 1.0;
    float vig = 1.0 - 0.25 * dot(ndc, ndc);
    o_color = vec4(c.rgb * v_shade * vig, 1.0);
}
