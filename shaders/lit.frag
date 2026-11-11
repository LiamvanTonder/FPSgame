#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vLightPos;
in vec3 vColor;
flat in int vPattern;

uniform vec4 uParam;
uniform vec3 uSunDir;     // direction the light travels
uniform vec3 uCamPos;
uniform vec3 uSkyColor;
uniform sampler2DShadow uShadowMap;

// Weapon lights: narrow cones with a soft edge.
struct Spot {
    vec3 position;
    vec3 direction;
    float range;
};
uniform Spot uSpots[8];
uniform int uSpotCount;

out vec4 FragColor;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

// 1 where p is on a grid line of the given spacing/width, anti-aliased and faded when too dense.
float gridLine(vec2 p, float spacing, float width) {
    vec2 c = p / spacing;
    vec2 d = abs(fract(c - 0.5) - 0.5);
    vec2 w = fwidth(c);
    float hw = 0.5 * width / spacing;
    vec2 l = 1.0 - smoothstep(vec2(hw), vec2(hw) + w * 1.5, d);
    float fade = clamp(1.0 - max(w.x, w.y) * 3.0, 0.0, 1.0);
    return max(l.x, l.y) * fade;
}

// Thin circle of radius r (metres) around the origin, anti-aliased.
float circle(float dist, float r, float width) {
    float aa = fwidth(dist) * 1.5;
    return 1.0 - smoothstep(width * 0.5, width * 0.5 + aa, abs(dist - r));
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

float shadowFactor(vec3 n) {
    vec3 p = vLightPos.xyz / vLightPos.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float ndl = max(dot(n, -uSunDir), 0.0);
    float bias = mix(0.0008, 0.00015, ndl);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            lit += texture(uShadowMap, vec3(p.xy + vec2(x, y) * texel, p.z - bias));
    return lit / 9.0;
}

void main() {
    vec3 base = vColor;
    int uPattern = vPattern;

    float camDist = length(uCamPos - vWorldPos);
    if (uPattern == 3) {
        FragColor = vec4(base, 1.0);
        return;
    }
    if (uPattern == 4) {
        // Additive light, dimmed by the atmosphere at range.
        float atten = exp(-camDist * 0.0012);
        FragColor = vec4(base * uParam.w * atten, 1.0);
        return;
    }
    if (uPattern == 1) {
        vec2 g = vWorldPos.xz;
        float fine = gridLine(g, 1.0, 0.03);
        float coarse = gridLine(g, 10.0, 0.12);
        base = mix(base, base * 0.75, fine);
        base = mix(base, base * 0.55, coarse);
    } else if (uPattern == 6) {
        // Ground: patchy grass, darker tufts, fading detail with range.
        vec2 g = vWorldPos.xz;
        float n1 = vnoise(g * 0.08), n2 = vnoise(g * 0.6), n3 = vnoise(g * 3.1);
        float detail = clamp(1.0 - camDist / 120.0, 0.0, 1.0);
        base *= 0.82 + 0.3 * n1 + (0.16 * n2 + 0.10 * n3 - 0.13) * detail;
        float slope = 1.0 - clamp(vNormal.y, 0.0, 1.0);
        base = mix(base, vec3(0.45, 0.38, 0.28), clamp(slope * 3.0 - 0.6, 0.0, 0.8));
    } else if (uPattern == 7) {
        vec3 q = vWorldPos * 1.7;
        float n = vnoise(q.xz + q.y) * 0.6 + vnoise(q.xy * 2.3) * 0.4;
        base *= 0.65 + 0.6 * n;
    } else if (uPattern == 8) {
        // Planks: boards along the longer horizontal axis
        float a = abs(vNormal.y) > 0.5 ? vWorldPos.x : (abs(vNormal.x) > 0.5 ? vWorldPos.y : vWorldPos.y);
        float seam = gridLine(vec2(a, 0.5), 0.18, 0.012);
        base *= 0.9 + 0.15 * vnoise(vec2(a * 6.0, (vWorldPos.x + vWorldPos.z) * 0.7));
        base = mix(base, base * 0.55, seam);
    } else if (uPattern == 9) {
        vec2 p = abs(vNormal.x) > 0.5 ? vWorldPos.zy : vWorldPos.xy;
        float row = floor(p.y / 0.075);
        p.x += mod(row, 2.0) * 0.11;
        vec2 c = vec2(p.x / 0.22, p.y / 0.075);
        vec2 d = abs(fract(c) - 0.5);
        float mortar = step(0.44, max(d.x * 0.97, d.y));
        base *= 0.85 + 0.25 * hash(floor(c));
        base = mix(base, vec3(0.62, 0.60, 0.56), mortar * clamp(1.0 - camDist / 60.0, 0.0, 1.0));
    } else if (uPattern == 2) {
        // Scoring rings (radii in metres), target face perpendicular to Z.
        float r = length((vWorldPos - uParam.xyz).xy);
        vec3 paper = vec3(0.93, 0.91, 0.84);
        vec3 black = vec3(0.06);
        if (r < 0.15) {
            base = black;
            float lines = max(circle(r, 0.05, 0.003), circle(r, 0.10, 0.003));
            base = mix(base, paper, lines);
            if (r < 0.006) base = paper; // aiming dot
        } else {
            base = paper;
            float lines = max(circle(r, 0.20, 0.003), circle(r, 0.25, 0.003));
            base = mix(base, black, lines);
        }
    }

    vec3 albedo = toLinear(base);
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 l = -uSunDir;
    vec3 v = normalize(uCamPos - vWorldPos);
    vec3 h = normalize(l + v);

    float ndl = max(dot(n, l), 0.0);
    float sh = shadowFactor(n);
    vec3 sun = vec3(1.0, 0.95, 0.85) * 2.6;
    vec3 skyAmb = toLinear(uSkyColor) * 0.55;
    vec3 groundAmb = vec3(0.12, 0.11, 0.09);
    vec3 ambient = mix(groundAmb, skyAmb, n.y * 0.5 + 0.5);

    float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.15 * ndl;
    vec3 color = albedo * (ambient + sun * ndl * sh) + sun * spec * sh;

    for (int i = 0; i < uSpotCount; ++i) {
        vec3 toFrag = vWorldPos - uSpots[i].position;
        float d = length(toFrag);
        if (d > uSpots[i].range || d < 1e-3) continue;
        vec3 dirL = toFrag / d;
        float c = dot(dirL, uSpots[i].direction);
        float cone = smoothstep(0.955, 0.992, c) + 0.25 * smoothstep(0.90, 0.955, c);
        float atten = 1.0 / (1.0 + 0.06 * d * d) * (1.0 - smoothstep(uSpots[i].range * 0.75, uSpots[i].range, d));
        float facing = max(dot(n, -dirL), 0.0);
        color += albedo * vec3(1.0, 0.96, 0.88) * 9.0 * cone * atten * facing;
    }

    if (uPattern == 5) {
        // Smoke/dust: mostly ambient, softly sun-lit regardless of facing
        color = albedo * (ambient * 1.3 + sun * (0.25 + 0.35 * ndl) * sh);
    }

    // Exponential distance fog (aerial perspective)
    float fog = 1.0 - exp(-camDist * 0.0018);
    color = mix(color, toLinear(uSkyColor), fog);

    // Simple filmic-ish tonemap
    color = color / (color + vec3(0.8)) * 1.6;
    FragColor = vec4(toSrgb(color), uPattern == 5 ? uParam.w : 1.0);
}
