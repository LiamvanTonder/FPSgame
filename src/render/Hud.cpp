#include "render/Hud.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4996)
#endif
#include "stb_easy_font.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>

bool Hud::init(const std::string& shaderDir) {
    if (!m_shader.load(shaderDir + "hud.vert", shaderDir + "hud.frag")) return false;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex), (void*)offsetof(HudVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(HudVertex), (void*)offsetof(HudVertex, color));
    glBindVertexArray(0);
    return true;
}

void Hud::shutdown() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_shader.destroy();
}

void Hud::rect(float x, float y, float w, float h, const glm::vec4& c) {
    HudVertex a{{x, y}, c}, b{{x + w, y}, c}, d{{x, y + h}, c}, e{{x + w, y + h}, c};
    m_vertices.insert(m_vertices.end(), {a, b, e, a, e, d});
}

void Hud::circle(float cx, float cy, float radius, const glm::vec4& c, int segments) {
    for (int i = 0; i < segments; ++i) {
        float a0 = float(i) / segments * 6.2831853f, a1 = float(i + 1) / segments * 6.2831853f;
        m_vertices.push_back({{cx, cy}, c});
        m_vertices.push_back({{cx + std::cos(a0) * radius, cy + std::sin(a0) * radius}, c});
        m_vertices.push_back({{cx + std::cos(a1) * radius, cy + std::sin(a1) * radius}, c});
    }
}

void Hud::rawText(float x, float y, const std::string& str, const glm::vec4& color, float scale) {
    // stb_easy_font emits quads: 4 vertices of {float x, y, z; uint8 rgba[4]} = 16 bytes.
    static std::vector<char> buffer(1 << 18);
    int quads = stb_easy_font_print(0.0f, 0.0f, const_cast<char*>(str.c_str()), nullptr, buffer.data(), int(buffer.size()));
    const float* f = reinterpret_cast<const float*>(buffer.data());
    for (int q = 0; q < quads; ++q) {
        HudVertex v[4];
        for (int i = 0; i < 4; ++i) {
            const float* p = f + (q * 4 + i) * 4;
            v[i] = {{x + p[0] * scale, y + p[1] * scale}, color};
        }
        m_vertices.insert(m_vertices.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
    }
}

float Hud::text(float x, float y, const std::string& str, const glm::vec4& color, float scale) {
    rawText(x + scale * 0.75f, y + scale * 0.75f, str, glm::vec4(0, 0, 0, color.a * 0.8f), scale);
    rawText(x, y, str, color, scale);
    int lines = 1 + int(std::count(str.begin(), str.end(), '\n'));
    return lines * 12.0f * scale;
}

float Hud::textWidth(const std::string& str, float scale) {
    return stb_easy_font_width(const_cast<char*>(str.c_str())) * scale;
}

void Hud::render(int width, int height) {
    if (m_vertices.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.use();
    m_shader.set("uScreen", glm::vec2(float(width), float(height)));
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m_vertices.size() * sizeof(HudVertex)), m_vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(m_vertices.size()));
    glBindVertexArray(0);
    m_vertices.clear();

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
