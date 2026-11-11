#version 330 core
layout(location = 0) in vec2 aPos;   // pixels, origin top-left
layout(location = 1) in vec4 aColor;

uniform vec2 uScreen;

out vec4 vColor;

void main() {
    vColor = aColor;
    vec2 ndc = vec2(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
