#include "render/ScopeOverlay.h"

bool ScopeOverlay::init(const std::string& shaderDir) {
    if (!m_shader.load(shaderDir + "scope.vert", shaderDir + "scope.frag")) return false;
    glGenVertexArrays(1, &m_vao); // fullscreen triangle generated from gl_VertexID
    return true;
}

void ScopeOverlay::shutdown() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_shader.destroy();
}

void ScopeOverlay::draw(const Params& p) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.use();
    m_shader.set("uScreen", p.screen);
    m_shader.set("uCenter", p.center);
    m_shader.set("uRadius", p.radius);
    m_shader.set("uShadow", p.shadow);
    m_shader.set("uPxPerMrad", p.pxPerMrad);
    m_shader.set("uReticle", p.reticle);
    m_shader.set("uOpacity", p.opacity);
    m_shader.set("uImage", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p.image);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
