#pragma once

#include "render/Camera.h"
#include "render/Mesh.h"
#include "render/Pattern.h"
#include "render/Shader.h"

#include <glm/glm.hpp>
#include <vector>

struct OBB;
class StaticScene;



struct DrawItem {
    const Mesh* mesh;
    glm::mat4 model;
    glm::vec3 color;
    Pattern pattern;
    glm::vec4 param;
    bool castShadow;
    bool weapon; // the held weapon: excluded from the view through its own optic
};

// Offscreen colour+depth target (used for the magnified scope image).
struct RenderTarget {
    GLuint fbo = 0, color = 0, depth = 0;
    int width = 0, height = 0;
    bool create(int w, int h);
    void destroy();
};

class Renderer {
public:
    bool init(const std::string& shaderDir);
    void shutdown();

    void begin();
    void draw(const Mesh& mesh, const glm::mat4& model, const glm::vec3& color,
              Pattern pattern = Pattern::Plain, const glm::vec4& param = glm::vec4(0.0f), bool castShadow = true);
    void drawBox(const OBB& box, const glm::vec3& color, Pattern pattern = Pattern::Plain,
                 const glm::vec4& param = glm::vec4(0.0f));
    // Baked static scenery drawn with every view (not owned).
    void setStatic(const StaticScene* scene) { m_static = scene; }
    float staticDrawDistance = 900.0f;
    int lastStaticChunks = 0;
    // Weapon lights for this frame (cleared by begin()).
    struct SpotLight {
        glm::vec3 position, direction;
        float range;
    };
    void addSpot(const glm::vec3& p, const glm::vec3& dir, float range) {
        if (m_spots.size() < 8) m_spots.push_back({p, dir, range});
    }
    // Items submitted while set are tagged as part of the held weapon.
    void setWeaponLayer(bool on) { m_weaponLayer = on; }

    // Once per frame, fitted around the main camera.
    void renderShadows(const Camera& camera);
    // Draws the submitted items from 'camera' into fbo (0 = window).
    void renderView(const Camera& camera, GLuint fbo, int width, int height, bool includeWeapon);

    Mesh box, cylinder, sphere;

    glm::vec3 sunDirection = glm::normalize(glm::vec3(-0.45f, -1.0f, -0.35f)); // direction light travels
    glm::vec3 skyColor{0.55f, 0.70f, 0.88f};

private:
    glm::mat4 computeLightViewProj(const Camera& camera) const;
    void drawItem(const DrawItem& item);
    void drawStatic(const glm::mat4& viewProj, const glm::vec3& eye, float maxDist, bool shadowPass);

    Shader m_lit, m_depth;
    GLuint m_shadowFbo = 0, m_shadowTex = 0;
    int m_shadowSize = 4096;
    glm::mat4 m_lightVP{1.0f};
    bool m_weaponLayer = false;
    const StaticScene* m_static = nullptr;
    std::vector<DrawItem> m_items;
    std::vector<SpotLight> m_spots;
    std::vector<const DrawItem*> m_sorted;
};
