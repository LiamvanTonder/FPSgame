#pragma once

#include "render/Shader.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

// 2D overlay in pixel coordinates (origin top-left). Text uses stb_easy_font.
class Hud {
public:
    bool init(const std::string& shaderDir);
    void shutdown();

    void rect(float x, float y, float w, float h, const glm::vec4& color);
    void circle(float cx, float cy, float radius, const glm::vec4& color, int segments = 20);
    // Draws text with a drop shadow. Returns the height used.
    float text(float x, float y, const std::string& str, const glm::vec4& color, float scale = 2.0f);
    static float textWidth(const std::string& str, float scale = 2.0f);

    void render(int width, int height);

private:
    struct HudVertex {
        glm::vec2 pos;
        glm::vec4 color;
    };
    void rawText(float x, float y, const std::string& str, const glm::vec4& color, float scale);

    Shader m_shader;
    GLuint m_vao = 0, m_vbo = 0;
    std::vector<HudVertex> m_vertices;
};
