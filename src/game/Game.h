#pragma once

#include "audio/Audio.h"
#include "core/Input.h"
#include "core/Time.h"
#include "game/Simulation.h"
#include "items/Profile.h"
#include "render/Camera.h"
#include "render/DebugDraw.h"
#include "render/Hud.h"
#include "render/Renderer.h"
#include "render/ScopeOverlay.h"
#include "render/StaticScene.h"
#include "ui/InventoryUi.h"

#include <string>
#include <vector>

class Window;

class Game {
public:
    // profileName: save file under save/ (default "profile"; tests use their own).
    bool init(Window& window, const std::string& shaderDir, const std::string& profileName = "profile");
    void frame(double frameTime);
    void shutdown();
    void startRaidNow(); // dev: --raid

private:
    enum class Screen { None, Inventory, Stash, Store, Loot, Map, Results, Pause, Deploy };

    void readInput();
    void openScreen(Screen s);
    void closeScreen();
    void deploy(bool pistolOnly);
    void requestDeploy();
    void drawDeploy(float W, float H);
    bool loadoutHasWeapon() const;
    void endRaid(bool extracted);
    void returnToBase();
    void rebuildScene();
    void quickMed(int key);
    void saveProfile();
    void updateAudio(float frameTime);

    void render(float alpha);
    void renderOptic(float alpha, int w, int h);
    void drawHud();
    void drawCompass(float W);
    void drawBodyStatus(float x, float y, float s);
    void drawMap(float W, float H);
    void drawResults(float W, float H);
    void drawPause(float W, float H);
    bool project(const glm::vec3& world, glm::vec2& screen) const;
    bool button(float x, float y, float w, float h, const std::string& label);

    Window* m_window = nullptr;
    Input m_input;
    Renderer m_renderer;
    DebugDraw m_debug;
    Hud m_hud;
    ScopeOverlay m_scopeOverlay;
    RenderTarget m_scopeTarget;
    Camera m_camera;
    FixedStep m_step;
    StaticScene m_scene;
    Audio m_audio;
    bool m_audioOk = false;

    Simulation m_sim;
    Profile m_profile;
    Config m_config;
    std::string m_profilePath, m_configPath;
    InventoryUi m_inv;
    Screen m_screen = Screen::None;
    Interaction m_lootTarget;

    // Controls
    CharacterInput m_in;
    bool m_interactHeld = false;
    float m_lookYaw = 0.0f, m_lookPitch = 0.0f;
    bool m_ignoreClick = false;
    UiInput m_ui;

    // Raid results
    bool m_raidOver = false;
    bool m_extracted = false;
    RaidStats m_results;
    std::vector<std::string> m_resultLines;
    Equipment m_pendingLoadout;
    Equipment m_home;        // the real loadout, kept safe during a pistol-only raid
    bool m_pistolRun = false;

    float m_heartPhase = 0.0f, m_breathTimer = 0.0f;
    bool m_showDebug = false, m_showTracers = false, m_showColliders = false, m_showHelp = true;
    double m_fpsTimer = 0.0;
    int m_fpsFrames = 0;
    float m_fps = 0.0f;
};
