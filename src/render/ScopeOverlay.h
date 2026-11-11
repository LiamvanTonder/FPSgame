#pragma once

#include "render/Shader.h"

#include <glm/glm.hpp>

// Composites the magnified scope image into the eyepiece on screen, draws the
// reticle, the scope shadow (eye off the optical axis) and blacks out the rest.
class ScopeOverlay {
public:
    struct Params {
        glm::vec2 screen{0.0f};
        glm::vec2 center{0.0f};  // eyepiece centre (px, top-left origin)
        float radius = 0.0f;      // eyepiece radius (px)
        glm::vec2 shadow{0.0f};   // offset of the exit-pupil circle (px)
        float pxPerMrad = 1.0f;   // reticle scale (first focal plane: true mils)
        int reticle = 0;
        float opacity = 1.0f;
        GLuint image = 0;
    };

    bool init(const std::string& shaderDir);
    void shutdown();
    void draw(const Params& p);

private:
    Shader m_shader;
    GLuint m_vao = 0;
};
