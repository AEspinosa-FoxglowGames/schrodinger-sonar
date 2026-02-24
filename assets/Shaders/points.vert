#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in float aA; // only provided for PointA buffer

uniform vec2  uWorldMin;
uniform vec2  uWorldMax;
uniform vec4  uColor;
uniform float uPointSize;
uniform float uTime;
uniform int   uMode;

out vec4  vColor;
out float vA;
out float vTime;

void main() {
    vec2 t = (aPos - uWorldMin) / (uWorldMax - uWorldMin);
    vec2 ndc = t * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    gl_PointSize = uPointSize;

    vColor = uColor;
    vTime = uTime;

    // Only mode 2 uses per-point alpha.
    vA = (uMode == 2) ? aA : 1.0;
}
