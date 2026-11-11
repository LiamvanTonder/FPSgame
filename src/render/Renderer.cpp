#include "render/Renderer.h"

#include "physics/Collision.h"
#include "render/StaticScene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

bool RenderTarget::create(int w, int h) {
    width = w;
    height = h;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) std::fprintf(stderr, "Render target incomplete\n");
    return ok;
}

void RenderTarget::destroy() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (color) glDeleteTextures(1, &color);
    if (depth) glDeleteRenderbuffers(1, &depth);
    fbo = color = depth = 0;
}

bool Renderer::init(const std::string& shaderDir) {
    if (!m_lit.load(shaderDir + "lit.vert", shaderDir + "lit.frag")) return false;
    if (!m_depth.load(shaderDir + "depth.vert", shaderDir + "depth.frag")) return false;

    box = Mesh::makeBox();
    cylinder = Mesh::makeCylinder(24);
    sphere = Mesh::makeSphere(12, 16);

    glGenTextures(1, &m_shadowTex);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_shadowSize, m_shadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Hardware depth comparison gives cheap 2x2 PCF per tap.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glGenFramebuffers(1, &m_shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        std::fprintf(stderr, "Shadow framebuffer incomplete\n");
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    return true;
}

void Renderer::shutdown() {
    box.destroy();
    cylinder.destroy();
    sphere.destroy();
    m_lit.destroy();
    m_depth.destroy();
    if (m_shadowFbo) glDeleteFramebuffers(1, &m_shadowFbo);
    if (m_shadowTex) glDeleteTextures(1, &m_shadowTex);
}

void Renderer::begin() { m_items.clear(); }

void Renderer::draw(const Mesh& mesh, const glm::mat4& model, const glm::vec3& color, Pattern pattern,
                    const glm::vec4& param, bool castShadow) {
    m_items.push_back({&mesh, model, color, pattern, param, castShadow, m_weaponLayer});
}

void Renderer::drawBox(const OBB& b, const glm::vec3& color, Pattern pattern, const glm::vec4& param) {
    draw(box, b.modelMatrix(), color, pattern, param, true);
}

glm::mat4 Renderer::computeLightViewProj(const Camera& camera) const {
    // Fit a fixed-size ortho box around the area in front of the camera.
    const float halfExtent = 70.0f;
    glm::vec3 flatFwd = glm::normalize(glm::vec3(camera.forward().x, 0.0f, camera.forward().z) + glm::vec3(0, 0, 1e-4f));
    glm::vec3 centre = camera.position + flatFwd * (halfExtent * 0.6f);
    centre.y = 0.0f;

    glm::vec3 up = std::abs(sunDirection.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::mat4 lightView = glm::lookAt(-sunDirection * 200.0f, glm::vec3(0.0f), up);

    // Snap the centre to shadow-map texels to stop shimmering as the player moves.
    glm::vec4 lc = lightView * glm::vec4(centre, 1.0f);
    float texel = (2.0f * halfExtent) / float(m_shadowSize);
    lc.x = std::floor(lc.x / texel) * texel;
    lc.y = std::floor(lc.y / texel) * texel;

    glm::mat4 proj = glm::ortho(lc.x - halfExtent, lc.x + halfExtent, lc.y - halfExtent, lc.y + halfExtent,
                                -lc.z - 250.0f, -lc.z + 250.0f);
    return proj * lightView;
}

void Renderer::renderShadows(const Camera& camera) {
    m_lightVP = computeLightViewProj(camera);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glViewport(0, 0, m_shadowSize, m_shadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glDisable(GL_CULL_FACE); // thin geometry (paper, plates) must cast from both sides
    m_depth.use();
    m_depth.set("uLightVP", m_lightVP);
    m_depth.set("uModel", glm::mat4(1.0f));
    drawStatic(m_lightVP, camera.position, 160.0f, true);
    for (const DrawItem& item : m_items) {
        if (!item.castShadow || item.pattern == Pattern::Unlit || item.pattern == Pattern::Glow ||
            item.pattern == Pattern::Smoke)
            continue;
        m_depth.set("uModel", item.model);
        item.mesh->draw();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

namespace {
// Frustum planes (inward normals) from a view-projection matrix.
void frustumPlanes(const glm::mat4& m, glm::vec4 planes[6]) {
    glm::mat4 t = glm::transpose(m);
    planes[0] = t[3] + t[0];
    planes[1] = t[3] - t[0];
    planes[2] = t[3] + t[1];
    planes[3] = t[3] - t[1];
    planes[4] = t[3] + t[2];
    planes[5] = t[3] - t[2];
}
bool boxVisible(const glm::vec4 planes[6], const glm::vec3& mn, const glm::vec3& mx) {
    for (int i = 0; i < 6; ++i) {
        glm::vec3 n(planes[i]);
        glm::vec3 p(n.x >= 0 ? mx.x : mn.x, n.y >= 0 ? mx.y : mn.y, n.z >= 0 ? mx.z : mn.z);
        if (glm::dot(n, p) + planes[i].w < 0.0f) return false;
    }
    return true;
}
} // namespace

void Renderer::drawStatic(const glm::mat4& viewProj, const glm::vec3& eye, float maxDist, bool shadowPass) {
    if (!m_static) return;
    glm::vec4 planes[6];
    frustumPlanes(viewProj, planes);
    int drawn = 0;
    for (const StaticScene::Chunk& c : m_static->chunks()) {
        if (!c.vao || c.count == 0) continue;
        glm::vec3 closest = glm::clamp(eye, c.min, c.max);
        if (glm::length(closest - eye) > maxDist) continue;
        if (!boxVisible(planes, c.min, c.max)) continue;
        glBindVertexArray(c.vao);
        glDrawElements(GL_TRIANGLES, c.count, GL_UNSIGNED_INT, nullptr);
        ++drawn;
    }
    if (!shadowPass) lastStaticChunks = drawn;
}

void Renderer::drawItem(const DrawItem& item) {
    m_lit.set("uModel", item.model);
    m_lit.set("uNormalMat", glm::mat3(glm::transpose(glm::inverse(item.model))));
    m_lit.set("uColor", item.color);
    m_lit.set("uPattern", int(item.pattern));
    m_lit.set("uParam", item.param);
    item.mesh->draw();
}

void Renderer::renderView(const Camera& camera, GLuint fbo, int width, int height, bool includeWeapon) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_lit.use();
    m_lit.set("uViewProj", camera.viewProjection());
    m_lit.set("uLightVP", m_lightVP);
    m_lit.set("uSunDir", sunDirection);
    m_lit.set("uCamPos", camera.position);
    m_lit.set("uSkyColor", skyColor);
    m_lit.set("uShadowMap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);

    auto visible = [&](const DrawItem& i) { return includeWeapon || !i.weapon; };

    // Baked scenery
    m_lit.set("uBatched", 1);
    m_lit.set("uModel", glm::mat4(1.0f));
    m_lit.set("uNormalMat", glm::mat3(1.0f));
    m_lit.set("uParam", glm::vec4(0.0f));
    drawStatic(camera.viewProjection(), camera.position, std::min(staticDrawDistance, camera.farPlane), false);
    m_lit.set("uBatched", 0);

    // Opaque
    for (const DrawItem& item : m_items)
        if (visible(item) && item.pattern != Pattern::Glow && item.pattern != Pattern::Smoke) drawItem(item);

    // Smoke: alpha blended, back to front, no depth writes
    m_sorted.clear();
    for (const DrawItem& item : m_items)
        if (visible(item) && item.pattern == Pattern::Smoke) m_sorted.push_back(&item);
    if (!m_sorted.empty()) {
        auto dist = [&](const DrawItem* i) { return glm::length(glm::vec3(i->model[3]) - camera.position); };
        std::sort(m_sorted.begin(), m_sorted.end(), [&](const DrawItem* a, const DrawItem* b) { return dist(a) > dist(b); });
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const DrawItem* item : m_sorted) drawItem(*item);
    }

    // Glow: additive
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    for (const DrawItem& item : m_items)
        if (visible(item) && item.pattern == Pattern::Glow) drawItem(item);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
