#version 330 core
uniform sampler2D uImage;
uniform vec2 uScreen;
uniform vec2 uCenter;     // px, top-left origin
uniform float uRadius;    // px
uniform vec2 uShadow;     // exit-pupil circle offset (px)
uniform float uPxPerMrad;
uniform int uReticle;     // 0 none, 1 duplex, 2 mil-dot, 3 cross + illuminated dot
uniform float uOpacity;

out vec4 FragColor;

// Anti-aliased "inside" test for a band |x| < halfWidth.
float band(float x, float halfWidth) {
    return 1.0 - smoothstep(halfWidth - 0.5, halfWidth + 0.5, abs(x));
}

void main() {
    vec2 p = vec2(gl_FragCoord.x, uScreen.y - gl_FragCoord.y);
    vec2 d = p - uCenter;
    float r = length(d);
    float R = uRadius;

    vec3 color = vec3(0.0);
    float inDisk = 1.0 - smoothstep(R - 1.5, R, r);
    if (inDisk > 0.0) {
        vec2 uv = d / R * 0.5 + 0.5;
        uv.y = 1.0 - uv.y;
        color = texture(uImage, uv).rgb;

        // Edge darkening of the image circle
        color *= mix(1.0, 0.45, smoothstep(0.82, 1.0, r / R));

        // Reticle (centred on the optical axis = disk centre)
        float ret = 0.0;
        vec3 retColor = vec3(0.02);
        if (uReticle == 1) {
            // Duplex: fine centre cross, heavy posts outside
            float inner = R * 0.33;
            ret = max(ret, band(d.x, 0.6) * step(abs(d.y), inner));
            ret = max(ret, band(d.y, 0.6) * step(abs(d.x), inner));
            ret = max(ret, band(d.x, 2.5) * step(inner, abs(d.y)));
            ret = max(ret, band(d.y, 2.5) * step(inner, abs(d.x)));
        } else if (uReticle == 2) {
            // Mil-dot: fine cross with a dot every milliradian out to 5, heavier posts beyond
            float lim = uPxPerMrad * 5.5;
            ret = max(ret, band(d.x, 0.6) * step(abs(d.y), lim));
            ret = max(ret, band(d.y, 0.6) * step(abs(d.x), lim));
            ret = max(ret, band(d.x, 2.5) * step(lim, abs(d.y)));
            ret = max(ret, band(d.y, 2.5) * step(lim, abs(d.x)));
            float dotR = max(1.2, uPxPerMrad * 0.12);
            for (int i = 1; i <= 5; ++i) {
                float s = float(i) * uPxPerMrad;
                ret = max(ret, 1.0 - smoothstep(dotR - 0.5, dotR + 0.5, length(d - vec2(0.0, s))));
                ret = max(ret, 1.0 - smoothstep(dotR - 0.5, dotR + 0.5, length(d + vec2(0.0, s))));
                ret = max(ret, 1.0 - smoothstep(dotR - 0.5, dotR + 0.5, length(d - vec2(s, 0.0))));
                ret = max(ret, 1.0 - smoothstep(dotR - 0.5, dotR + 0.5, length(d + vec2(s, 0.0))));
            }
        } else if (uReticle == 3) {
            // Short cross with an illuminated centre dot
            float lim = R * 0.25;
            ret = max(ret, band(d.x, 0.7) * step(abs(d.y), lim) * step(4.0, abs(d.y)));
            ret = max(ret, band(d.y, 0.7) * step(abs(d.x), lim) * step(4.0, abs(d.x)));
            float dotMask = 1.0 - smoothstep(1.6, 2.6, r);
            color = mix(color, vec3(1.0, 0.15, 0.1), dotMask);
        }
        color = mix(color, retColor, ret);

        // Scope shadow: only light passing through the exit pupil reaches the eye.
        float pupil = 1.0 - smoothstep(R * 0.80, R * 1.0, length(p - (uCenter + uShadow)));
        color *= pupil;
    }
    // Everything outside the eyepiece is the dark scope body / out-of-focus field.
    FragColor = vec4(color, uOpacity);
}
