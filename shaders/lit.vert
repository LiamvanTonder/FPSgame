#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;   // baked scenery only
layout(location = 3) in float aPattern;

uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform mat4 uViewProj;
uniform mat4 uLightVP;
uniform int uBatched;
uniform vec3 uColor;
uniform int uPattern;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vLightPos;
out vec3 vColor;
flat out int vPattern;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(uNormalMat * aNormal);
    vLightPos = uLightVP * world;
    if (uBatched == 1) {
        vColor = aColor;
        vPattern = int(aPattern + 0.5);
    } else {
        vColor = uColor;
        vPattern = uPattern;
    }
    gl_Position = uViewProj * world;
}
