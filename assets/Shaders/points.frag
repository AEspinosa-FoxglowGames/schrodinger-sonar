#version 330 core

in vec4  vColor;
in float vA;
in float vTime;

uniform int uMode;

out vec4 FragColor;

// Soft disc alpha (core + halo)
float discAlpha(vec2 p) {
    float d = length(p);
    float core = 1.0 - smoothstep(0.78, 1.0, d);
    float halo = 1.0 - smoothstep(0.35, 1.0, d);
    return core + 0.35 * halo;
}

// Soft box alpha (core + halo)
float boxAlpha(vec2 p) {
    float d = max(abs(p.x), abs(p.y));
    float core = 1.0 - smoothstep(0.88, 1.0, d);
    float halo = 1.0 - smoothstep(0.55, 1.0, d);
    return core + 0.28 * halo;
}

void main() {
    // gl_PointCoord is [0,1] within the point sprite
    vec2 uv = gl_PointCoord * 2.0 - 1.0; // [-1,1]

    float a;
    if (uMode == 3) {
        a = boxAlpha(uv);
    } else {
        a = discAlpha(uv);
    }

    // Tiny scanline shimmer (subtle sci-fi vibe)
    float scan = 0.96 + 0.04 * sin(vTime * 3.0 + gl_FragCoord.y * 0.06);

    vec4 c = vColor;
    c.rgb *= scan;
    c.a *= a * vA;

    // Kill very small fragments to keep it crisp
    if (c.a < 0.01) discard;

    FragColor = c;
}
