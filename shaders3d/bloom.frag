in vec2 v_uv;
in float v_shade;
in vec3 v_world;
out vec4 o_color;
uniform sampler2D u_tex;      // baked bright-pass of the mural, blurred wide
uniform vec2 u_vp_offset;
uniform vec2 u_vp_size;

// Additive wall bloom (#1): only the mural's own bright pixels (star specks,
// silhouette highlights) are in u_tex. Drawn with additive blend over the wall.
// STRENGTH is a const so Alt+R tunes the glow live without a rebuild.
const float STRENGTH = 1.80;

void main() {
    vec3 b = texture(u_tex, v_uv).rgb;
    vec2 ndc = ((gl_FragCoord.xy - u_vp_offset) / u_vp_size) * 2.0 - 1.0;
    float vig = 1.0 - 0.35 * dot(ndc, ndc);
    o_color = vec4(b * STRENGTH * vig, 1.0);
}
