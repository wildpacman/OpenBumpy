in vec2 v_uv;
in float v_shade;
in vec3 v_world;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec2 u_light;        // ball position, GL space
uniform float u_ambient;     // base wall brightness
uniform float u_spot;        // spotlight strength added near the ball
uniform float u_spot_radius; // px
uniform vec2 u_vp_offset;    // viewport origin in window px
uniform vec2 u_vp_size;

// --- PROTOTYPE (#2 + #3): static "display-case" lighting, hardcoded so the look
// can be iterated by hot-reload before promoting any of it to uniforms. Board
// centre is world (0,0); the mural spans ~x[-160,160] y[-100,100], y up. ---

// #2 static warm core glow: a fixed light pool in the middle of the diorama,
// independent of the ball. MUL boosts whatever art is there (silhouettes/specks
// read brighter near centre); ADD is a soft warm haze (same class as the spot).
const vec2  CORE_POS    = vec2(0.0, 6.0);
const float CORE_RADIUS = 115.0;
const float CORE_MUL    = 0.45;
const float CORE_ADD    = 0.10;
const vec3  CORE_TINT   = vec3(0.85, 0.50, 1.00);  // magenta-purple nebula lift

// #3 top softit: a ceiling light placed ABOVE the scene. Its bright core sits
// off the top of the frame, so on-screen we only ever see the downward spill --
// a gradient brightest at the top edge, fading down. No visible band/blob.
// BEAM_TOP is a world-y at/above the top edge where the spill is strongest;
// BEAM_REACH is how far down it reaches before dying out.
const float BEAM_TOP      = 112.0;
const float BEAM_REACH    = 62.0;
const float BEAM_ADD      = 0.22;
const vec3  BEAM_TINT     = vec3(1.00, 0.87, 0.66);

void main() {
    vec4 c = texture(u_tex, v_uv);

    // ball-following spotlight (unchanged)
    float d = distance(v_world.xy, u_light);
    float spot = u_spot * exp(-(d * d) / (u_spot_radius * u_spot_radius));

    // #2 static core glow
    float cd = distance(v_world.xy, CORE_POS);
    float core = exp(-(cd * cd) / (CORE_RADIUS * CORE_RADIUS));

    // #3 top light: one-sided downward spill from an off-screen ceiling source;
    // full above BEAM_TOP (off-frame), ramping to 0 by BEAM_TOP - BEAM_REACH.
    float by = smoothstep(BEAM_TOP - BEAM_REACH, BEAM_TOP, v_world.y);
    float bx = 1.0 - 0.45 * clamp(abs(v_world.x) / 160.0, 0.0, 1.0);
    float beam = by * bx;

    float light = u_ambient + spot + CORE_MUL * core;

    vec2 ndc = ((gl_FragCoord.xy - u_vp_offset) / u_vp_size) * 2.0 - 1.0;
    float vig = 1.0 - 0.35 * dot(ndc, ndc);

    vec3 lit = c.rgb * light * vig * v_shade;
    // additive light effects (haze + softit), tinted; not scaled by the texture so
    // the lit "air" in the display case reads even over the mural's black voids.
    lit += CORE_ADD * core * CORE_TINT * vig;
    lit += BEAM_ADD * beam * BEAM_TINT * vig;

    o_color = vec4(lit, 1.0);
}
