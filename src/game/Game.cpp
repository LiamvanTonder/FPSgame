#include "game/Game.h"

#include "core/Paths.h"
#include "core/Window.h"
#include "game/WeaponDefs.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {
std::string format(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}
const glm::vec4 kWhite(1.0f, 1.0f, 1.0f, 0.95f), kDim(0.85f, 0.88f, 0.9f, 0.8f), kAccent(1.0f, 0.85f, 0.3f, 1.0f),
    kRed(1.0f, 0.4f, 0.35f, 1.0f), kGreen(0.5f, 1.0f, 0.5f, 1.0f);
} // namespace

bool Game::init(Window& window, const std::string& shaderDir, const std::string& profileName) {
    m_window = &window;
    m_input.attach(window.handle());
    if (!m_renderer.init(shaderDir) || !m_debug.init(shaderDir) || !m_hud.init(shaderDir) || !m_scopeOverlay.init(shaderDir) ||
        !m_scopeTarget.create(1024, 1024))
        return false;

    m_configPath = executableDir() + "config.txt";
    m_profilePath = executableDir() + "save/" + profileName + ".txt";
    if (!m_config.load(m_configPath)) m_config.save(m_configPath);

    Equipment loadout;
    if (!m_profile.load(m_profilePath, loadout)) {
        m_profile.starter(loadout);
        m_profile.save(m_profilePath, loadout);
    }
    m_sim.setupBase(loadout);
    m_lookYaw = m_sim.player().yaw;
    rebuildScene();

    m_audioOk = m_audio.init();
    if (!m_audioOk) std::fprintf(stderr, "No audio device: running silent\n");
    m_audio.setMasterVolume(m_config.volume);
    return true;
}

void Game::shutdown() {
    if (m_sim.mode == Simulation::Mode::Raid && !m_raidOver) {
        // Leaving mid-raid: the gear is lost, anything already sent home is kept.
        for (ItemPtr& it : m_sim.sentHome) m_profile.addToStash(std::move(it));
        ++m_profile.deaths;
        Equipment none;
        m_profile.save(m_profilePath, m_pistolRun ? m_home : none);
    } else if (m_raidOver) {
        m_profile.save(m_profilePath, m_pendingLoadout);
    } else {
        saveProfile();
    }
    m_audio.shutdown();
    m_scene.destroy();
    m_scopeTarget.destroy();
    m_scopeOverlay.shutdown();
    m_hud.shutdown();
    m_debug.shutdown();
    m_renderer.shutdown();
}

void Game::saveProfile() {
    if (m_sim.mode == Simulation::Mode::Base) {
        m_inv.cancelDrag();
        m_profile.save(m_profilePath, m_sim.player().equip);
    }
}

void Game::rebuildScene() {
    m_scene.clear();
    for (const SceneBlock& b : m_sim.map.blocks) m_scene.add(b);
    if (m_sim.map.hasTerrain) m_scene.addTerrain(m_sim.terrain, m_sim.terrainColors);
    m_scene.upload();
    m_renderer.setStatic(&m_scene);
    if (m_audioOk) m_audio.reset();
}

// ------------------------------------------------------------------ flow

bool Game::loadoutHasWeapon() const {
    const Equipment& eq = m_sim.player().equip;
    for (int s : {ES_Hand0, ES_Hand1, ES_Back0, ES_Back1})
        if (eq.slots[s] && eq.slots[s]->d().cat == ItemCat::Weapon) return true;
    return false;
}

void Game::startRaidNow() { deploy(!loadoutHasWeapon()); }

void Game::requestDeploy() {
    // Nothing to fight with: straight in with the free pistol.
    if (!loadoutHasWeapon()) deploy(true);
    else openScreen(Screen::Deploy);
}

void Game::deploy(bool pistolOnly) {
    closeScreen();
    m_inv.cancelDrag();
    ++m_profile.raids;
    Equipment loadout;
    m_pistolRun = pistolOnly;
    if (pistolOnly) {
        // The real kit stays home, untouched; the raid gets a free pistol kit.
        m_home = std::move(m_sim.player().equip);
        makeFreeKit(loadout);
        m_profile.save(m_profilePath, m_home);
    } else {
        Equipment none;
        m_profile.save(m_profilePath, none); // from here on, the gear is at risk
        loadout = std::move(m_sim.player().equip);
    }
    unsigned seed = m_config.seed ? m_config.seed : unsigned(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff);
    m_sim.setupRaid(seed, m_config.bots, loadout);
    m_lookYaw = m_sim.player().yaw;
    m_lookPitch = 0.0f;
    m_raidOver = false;
    rebuildScene();
    m_sim.message(pistolOnly ? "Pistol run: your loadout is safe at home. Map: M"
                             : "Find loot, take what others carry, and get out alive. Map: M");
}

void Game::endRaid(bool extracted) {
    m_raidOver = true;
    m_extracted = extracted;
    m_results = m_sim.stats;
    m_inv.cancelDrag();
    Equipment loadout;
    if (extracted) {
        loadout = std::move(m_sim.player().equip);
        ++m_profile.extractions;
    } else {
        ++m_profile.deaths;
    }
    int broughtOut = loadout.totalValue();
    bool wasPistolRun = m_pistolRun;
    if (m_pistolRun) {
        // Whatever came out of a pistol run goes to the stash; the home kit comes back as it was.
        for (ItemPtr& s : loadout.slots)
            if (s) m_profile.addToStash(std::move(s));
        for (Grid::Entry& e : loadout.pockets.items) m_profile.addToStash(std::move(e.item));
        loadout = std::move(m_home);
        m_home.clear();
        m_pistolRun = false;
    }
    m_profile.kills += m_results.kills;
    int home = 0;
    for (ItemPtr& it : m_sim.sentHome) {
        m_profile.addToStash(std::move(it));
        ++home;
    }
    m_sim.sentHome.clear();
    m_resultLines.clear();
    int mins = int(m_results.duration / 60.0), secs = int(m_results.duration) % 60;
    m_resultLines.push_back(format("Time in raid: %d:%02d", mins, secs));
    m_resultLines.push_back(format("Kills: %d", m_results.kills));
    m_resultLines.push_back(format("Damage taken: %.0f", m_results.damageTaken));
    m_resultLines.push_back(format("Items picked up: %d", m_results.itemsLooted));
    m_resultLines.push_back(format("Weapons sent home: %d", home));
    if (extracted) m_resultLines.push_back(format("Gear brought out: value %d%s", broughtOut, wasPistolRun ? " (now in your stash)" : ""));
    else {
        m_resultLines.push_back("Cause of death: " + m_results.causeOfDeath + (m_results.killedBy.empty() ? "" : " (" + m_results.killedBy + ")"));
        m_resultLines.push_back(wasPistolRun ? "Everything you carried is lost. Your home loadout was safe." : "Everything you carried is lost.");
    }
    m_resultLines.push_back(format("Raiders killed in this raid: %d   extracted: %d   new arrivals: %d", m_results.botsKilled,
                                   m_results.botExtracts, m_results.botRespawns));
    m_profile.save(m_profilePath, loadout);
    m_pendingLoadout = std::move(loadout);
    openScreen(Screen::Results);
}

void Game::returnToBase() {
    m_sim.setupBase(m_pendingLoadout);
    m_pendingLoadout.clear();
    m_raidOver = false;
    m_lookYaw = m_sim.player().yaw;
    m_lookPitch = 0.0f;
    rebuildScene();
    closeScreen();
    saveProfile();
}

void Game::openScreen(Screen s) {
    m_screen = s;
    m_inv.reset();
    if (s != Screen::None) {
        m_window->setMouseCaptured(false);
    }
}

void Game::closeScreen() {
    m_inv.cancelDrag();
    if (m_screen == Screen::Stash || m_screen == Screen::Store) saveProfile();
    m_screen = Screen::None;
    if (m_window->isFocused()) {
        m_window->setMouseCaptured(true);
        m_input.resetMouse();
        m_ignoreClick = true;
    }
}

void Game::quickMed(int key) {
    Character& p = m_sim.player();
    // 4: stop bleeding, 5: splint, 6: field kit, 7: painkillers
    std::vector<MedKind> kinds;
    switch (key) {
    case 4: kinds = {MedKind::Tourniquet, MedKind::Hemostatic, MedKind::Bandage, MedKind::FieldKit}; break;
    case 5: kinds = {MedKind::Splint}; break;
    case 6: kinds = {MedKind::FieldKit, MedKind::Surgical}; break;
    default: kinds = {MedKind::Painkillers}; break;
    }
    for (MedKind k : kinds) {
        int zone = p.health.bestZoneFor(k);
        if (zone < 0) continue;
        for (Grid* g : p.equip.grids())
            for (Grid::Entry& e : g->items)
                if (e.item->d().cat == ItemCat::Med && e.item->d().med == k) {
                    m_in.useMed = e.item->uid;
                    m_in.useMedZone = zone;
                    return;
                }
    }
    m_sim.message("Nothing to use for that");
}

// ------------------------------------------------------------------ input

void Game::readInput() {
    m_window->pollEvents();
    m_input.update();
    bool captured = m_window->isMouseCaptured();
    bool uiOpen = m_screen != Screen::None;

    if (m_input.pressed(GLFW_KEY_ESCAPE)) {
        if (uiOpen && m_screen != Screen::Results) closeScreen();
        else if (!uiOpen) openScreen(Screen::Pause);
    }
    if (captured && !m_window->isFocused()) m_window->setMouseCaptured(false);
    if (!uiOpen && !captured && m_input.mousePressed(GLFW_MOUSE_BUTTON_LEFT) && m_window->isFocused()) {
        m_window->setMouseCaptured(true);
        m_input.resetMouse();
        m_ignoreClick = true;
    }
    captured = m_window->isMouseCaptured();
    if (!m_input.mouseDown(GLFW_MOUSE_BUTTON_LEFT)) m_ignoreClick = false;

    // UI mouse
    double mx, my;
    glfwGetCursorPos(m_window->handle(), &mx, &my);
    int winW, winH;
    glfwGetWindowSize(m_window->handle(), &winW, &winH);
    float sx = winW > 0 ? float(m_window->width()) / winW : 1.0f, sy = winH > 0 ? float(m_window->height()) / winH : 1.0f;
    m_ui.mouse = glm::vec2(float(mx) * sx, float(my) * sy);
    m_ui.lmbPressed = m_input.mousePressed(GLFW_MOUSE_BUTTON_LEFT);
    m_ui.lmbReleased = !m_input.mouseDown(GLFW_MOUSE_BUTTON_LEFT) && m_ui.lmbDown;
    m_ui.lmbDown = m_input.mouseDown(GLFW_MOUSE_BUTTON_LEFT);
    m_ui.rmbPressed = m_input.mousePressed(GLFW_MOUSE_BUTTON_RIGHT);
    m_ui.scroll = m_input.scrollDelta();
    m_ui.rotate = m_input.pressed(GLFW_KEY_R);
    m_ui.shift = m_input.down(GLFW_KEY_LEFT_SHIFT) || m_input.down(GLFW_KEY_RIGHT_SHIFT);

    bool alive = m_sim.player().alive() && !m_raidOver;
    if (m_input.pressed(GLFW_KEY_TAB) && alive) {
        if (m_screen == Screen::Inventory || m_screen == Screen::Loot || m_screen == Screen::Stash || m_screen == Screen::Store) closeScreen();
        else if (!uiOpen) openScreen(Screen::Inventory);
    }
    if (m_input.pressed(GLFW_KEY_M) && alive && m_sim.mode == Simulation::Mode::Raid) {
        if (m_screen == Screen::Map) closeScreen();
        else if (!uiOpen) openScreen(Screen::Map);
    }
    uiOpen = m_screen != Screen::None;
    bool gearUi = m_screen == Screen::Inventory || m_screen == Screen::Loot || m_screen == Screen::Stash || m_screen == Screen::Store;

    if (m_input.pressed(GLFW_KEY_F1)) m_showColliders = !m_showColliders;
    if (m_input.pressed(GLFW_KEY_F2)) m_showDebug = !m_showDebug;
    if (m_input.pressed(GLFW_KEY_F3)) m_showTracers = !m_showTracers;
    if (m_input.pressed(GLFW_KEY_H) && !uiOpen) m_showHelp = !m_showHelp;
    if (m_sim.mode == Simulation::Mode::Base && !uiOpen) {
        if (m_input.pressed(GLFW_KEY_N)) m_sim.spawnBot(int(m_sim.rng() % 3));
        if (m_input.pressed(GLFW_KEY_K))
            for (size_t i = 1; i < m_sim.chars.size(); ++i) {
                m_sim.chars[i]->despawn(m_sim.world);
                m_sim.bots.remove(int(i));
            }
        if (m_input.pressed(GLFW_KEY_T)) {
            m_sim.targets.clearPaper();
            m_sim.ballistics.clearTrails();
            m_sim.fx.clearDecals();
        }
    }

    // Head turns directly with the mouse; the weapon has to follow physically.
    Character& p = m_sim.player();
    if (captured && !uiOpen && alive) {
        glm::vec2 d = m_input.mouseDelta();
        float sens = m_config.sensitivity * glm::mix(1.0f, 0.8f, p.weapon.adsBlend());
        if (p.weapon.optic().type == OpticType::Magnified) sens *= glm::mix(1.0f, 1.0f / p.weapon.magnification(), p.weapon.adsBlend());
        m_lookYaw -= d.x * sens;
        m_lookPitch = std::clamp(m_lookPitch - d.y * sens, -1.50f, 1.50f);
    }
    m_in.yaw = m_lookYaw;
    m_in.pitch = m_lookPitch;

    bool control = alive && (!uiOpen || gearUi);
    bool aimControl = control && !uiOpen && captured;
    bool rmb = aimControl && m_input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT);
    bool shift = m_input.down(GLFW_KEY_LEFT_SHIFT);
    m_in.move = control ? glm::vec2(float(m_input.down(GLFW_KEY_D)) - float(m_input.down(GLFW_KEY_A)),
                                    float(m_input.down(GLFW_KEY_W)) - float(m_input.down(GLFW_KEY_S)))
                        : glm::vec2(0.0f);
    m_in.sprint = control && shift && !rmb && !uiOpen;
    m_in.crouch = control && (m_input.down(GLFW_KEY_LEFT_CONTROL) || m_input.down(GLFW_KEY_C));
    m_in.jump |= aimControl && m_input.pressed(GLFW_KEY_SPACE);
    m_in.lean = aimControl ? float(m_input.down(GLFW_KEY_E)) - float(m_input.down(GLFW_KEY_Q)) : 0.0f;
    m_interactHeld = aimControl && m_input.down(GLFW_KEY_F);

    WeaponInput& w = m_in.weapon;
    w.adsHeld = rmb;
    w.holdBreath = shift && rmb;
    w.fireHeld = aimControl && !m_ignoreClick && m_input.mouseDown(GLFW_MOUSE_BUTTON_LEFT);
    w.firePressed |= aimControl && !m_ignoreClick && m_input.mousePressed(GLFW_MOUSE_BUTTON_LEFT);
    w.reloadPressed |= aimControl && m_input.pressed(GLFW_KEY_R);
    w.toggleFireMode |= aimControl && m_input.pressed(GLFW_KEY_B);
    if (aimControl) {
        for (int i = 0; i < 2; ++i)
            if (m_input.pressed(GLFW_KEY_1 + i)) w.selectWeapon = i;
        for (int k = 4; k <= 7; ++k)
            if (m_input.pressed(GLFW_KEY_0 + k)) quickMed(k);
        w.zoomSteps += m_input.scrollDelta();
        if (m_input.pressed(GLFW_KEY_RIGHT_BRACKET) || m_input.pressed(GLFW_KEY_PAGE_UP)) w.zeroSteps += 1;
        if (m_input.pressed(GLFW_KEY_LEFT_BRACKET) || m_input.pressed(GLFW_KEY_PAGE_DOWN)) w.zeroSteps -= 1;
        if (m_input.pressed(GLFW_KEY_X)) m_in.cancelAction = true;
    }
}

void Game::frame(double frameTime) {
    readInput();

    if (!m_raidOver || m_screen != Screen::Results) {
        int steps = m_step.advance(frameTime);
        for (int i = 0; i < steps; ++i) {
            m_sim.tick(float(m_step.dt), m_in, m_interactHeld);
            m_in.jump = false;
            m_in.useMed = 0;
            m_in.cancelAction = false;
            WeaponInput& w = m_in.weapon;
            w.firePressed = w.reloadPressed = w.toggleFireMode = false;
            w.selectWeapon = -1;
            w.zoomSteps = 0.0f;
            w.zeroSteps = 0;

            if (m_sim.openStash) {
                m_sim.openStash = false;
                openScreen(Screen::Stash);
            }
            if (m_sim.requestDeploy) {
                m_sim.requestDeploy = false;
                requestDeploy();
                if (m_sim.mode == Simulation::Mode::Raid) break;
            }
            if (m_sim.openStore) {
                m_sim.openStore = false;
                openScreen(Screen::Store);
            }
            if (m_sim.openLoot.type != Interaction::None) {
                m_lootTarget = m_sim.openLoot;
                openScreen(Screen::Loot);
            }
            if (m_sim.mode == Simulation::Mode::Base && m_sim.playerDeathTime >= 0.0 && m_sim.time - m_sim.playerDeathTime > 2.5) {
                // Shot on the range: no stakes at home. Back on your feet with your kit.
                Equipment eq = std::move(m_sim.player().equip);
                for (WorldItem& w : m_sim.loot.items) {
                    ItemPtr it = std::move(w.item);
                    bool placed = false;
                    if (it->d().cat == ItemCat::Weapon)
                        for (int s : {ES_Hand0, ES_Hand1, ES_Back0, ES_Back1})
                            if (!placed && !eq.slots[s]) {
                                eq.slots[s] = std::move(it);
                                placed = true;
                            }
                    if (!placed && !eq.stow(it)) m_profile.addToStash(std::move(it));
                }
                m_sim.setupBase(eq);
                m_lookYaw = m_sim.player().yaw;
                m_lookPitch = 0.0f;
                rebuildScene();
                m_sim.message("Patched up. Raiders on the range are live fire: K removes them.");
                break;
            }
            if (m_sim.mode == Simulation::Mode::Raid && !m_raidOver) {
                if (m_sim.playerExtracted) {
                    endRaid(true);
                    break;
                }
                if (m_sim.playerDeathTime >= 0.0 && m_sim.time - m_sim.playerDeathTime > 3.5) {
                    endRaid(false);
                    break;
                }
            }
        }
    }
    // A loot screen closes when you walk away from it.
    if (m_screen == Screen::Loot) {
        glm::vec3 at(1e9f);
        if (m_lootTarget.type == Interaction::Container && m_lootTarget.index < int(m_sim.loot.containers.size()))
            at = m_sim.loot.containers[m_lootTarget.index].position;
        if (m_lootTarget.type == Interaction::Body && m_sim.bodyEquipment(m_lootTarget.index)) at = m_sim.chars[m_lootTarget.index]->chest();
        if (glm::length(at - m_sim.player().position) > 3.5f) closeScreen();
    }
    if (m_screen != Screen::None && m_screen != Screen::Results && !m_sim.player().alive()) closeScreen();

    m_fpsFrames++;
    m_fpsTimer += frameTime;
    if (m_fpsTimer >= 0.5) {
        m_fps = float(m_fpsFrames / m_fpsTimer);
        m_fpsFrames = 0;
        m_fpsTimer = 0.0;
    }
    updateAudio(float(frameTime));
    render(m_step.alpha());
    m_window->swapBuffers();
}

void Game::updateAudio(float frameTime) {
    if (!m_audioOk) return;
    m_audio.update(m_sim.sound, &m_sim.world, m_camera.position, m_camera.forward(), m_camera.right(), 0);
    const Character& p = m_sim.player();
    if (!p.alive()) return;
    // Your own pulse and breathing when worked up.
    m_heartPhase += p.heartRate / 60.0f * frameTime;
    if (m_heartPhase >= 1.0f) {
        m_heartPhase -= 1.0f;
        float g = std::clamp((p.heartRate - 105.0f) / 70.0f, 0.0f, 1.0f);
        if (g > 0.0f) m_audio.playLocal(Clip_Heartbeat, 0.25f * g);
    }
    m_breathTimer -= frameTime;
    if (m_breathTimer <= 0.0f) {
        float e = p.exertion();
        if (e > 0.45f && !p.weapon.isHoldingBreath()) m_audio.playLocal(Clip_Breath, 0.12f * e);
        m_breathTimer = 3.2f - 1.6f * e;
    }
}

// ------------------------------------------------------------------ rendering

bool Game::project(const glm::vec3& world, glm::vec2& screen) const {
    glm::vec4 clip = m_camera.viewProjection() * glm::vec4(world, 1.0f);
    if (clip.w <= 0.01f) return false;
    float W = float(m_window->width()), H = float(m_window->height());
    screen = glm::vec2((clip.x / clip.w * 0.5f + 0.5f) * W, (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * H);
    return true;
}

void Game::render(float alpha) {
    int w = std::max(1, m_window->width()), h = std::max(1, m_window->height());
    Character& p = m_sim.player();
    const Weapon& wpn = p.weapon;

    if (p.alive() || !p.active) {
        m_camera.position = p.interpEye(alpha);
        float weld = 0.85f * wpn.adsBlend() * wpn.adsBlend();
        glm::vec3 weldEye = wpn.sightEyeWorld(alpha);
        if (wpn.hasWeapon() && glm::length(weldEye - m_camera.position) < 0.15f) m_camera.position = glm::mix(m_camera.position, weldEye, weld);
        m_camera.yaw = m_lookYaw + m_sim.flinch.x;
        m_camera.pitch = m_lookPitch + m_sim.flinch.y;
        m_camera.roll = 0.5f * p.leanAngle();
    } else {
        // Dead: the view sinks with the body.
        glm::vec3 head = glm::mix(p.prevJoints[J_Head], p.joints[J_Head], alpha);
        m_camera.position = head + glm::vec3(0.0f, 0.25f, 0.0f);
        m_camera.pitch = std::max(m_camera.pitch - 0.01f, -1.2f);
        m_camera.roll *= 0.98f;
    }
    m_camera.aspect = float(w) / float(h);
    float aimedFov = wpn.optic().type == OpticType::Magnified ? 50.0f : 62.0f;
    m_camera.fovY = glm::radians(glm::mix(70.0f, aimedFov, wpn.adsBlend()));
    m_camera.farPlane = m_sim.map.hasTerrain ? 1300.0f : 1500.0f;

    m_renderer.begin();
    if (m_sim.mode == Simulation::Mode::Base) m_sim.targets.draw(m_renderer);
    for (auto& c : m_sim.chars) {
        if (!c->active) continue;
        if (c->id != 0 && glm::length(c->position - m_camera.position) > 700.0f) continue;
        c->draw(m_renderer, alpha, c->id == 0 && p.alive());
    }
    m_sim.loot.draw(m_renderer, m_camera.position, 120.0f);
    m_sim.fx.draw(m_renderer, m_camera, m_sim.world);
    // Extraction flares burn while someone is extracting.
    for (auto& c : m_sim.chars)
        if (c->alive() && c->action() == Character::Action::Extract)
            for (const ExtractPoint& e : m_sim.map.extracts)
                if (glm::length(c->position - e.position) < e.radius) {
                    glm::mat4 m = glm::scale(glm::translate(glm::mat4(1.0f), e.position + glm::vec3(0, 6.4f, 0)), glm::vec3(0.7f));
                    float flick = 2.0f + std::sin(float(m_sim.time) * 37.0f);
                    m_renderer.draw(m_renderer.sphere, m, glm::vec3(1.0f, 0.25f, 0.15f), Pattern::Glow, glm::vec4(0, 0, 0, flick), false);
                }

    m_renderer.renderShadows(m_camera);
    OpticView optic = wpn.opticView(alpha);
    bool scoped = optic.type == OpticType::Magnified && wpn.adsBlend() > 0.02f && p.alive();
    float trueHalfFov = 0.0f;
    if (scoped) {
        float apparentHalf = std::atan(optic.ocularRadius / optic.eyeRelief);
        trueHalfFov = std::atan(std::tan(apparentHalf) / optic.magnification);
        Camera sc;
        sc.position = m_camera.position;
        sc.useOrientation = true;
        sc.orientation = optic.axis;
        sc.fovY = 2.0f * trueHalfFov;
        sc.aspect = 1.0f;
        sc.nearPlane = 0.1f;
        sc.farPlane = m_camera.farPlane;
        m_renderer.renderView(sc, m_scopeTarget.fbo, m_scopeTarget.width, m_scopeTarget.height, false);
    }
    m_renderer.renderView(m_camera, 0, w, h, true);

    if (m_showTracers) {
        for (const Trail& t : m_sim.ballistics.trails()) {
            float a = std::clamp(1.0f - t.age / 8.0f, 0.0f, 1.0f);
            for (size_t i = 1; i < t.points.size(); ++i) m_debug.line(t.points[i - 1], t.points[i], glm::vec4(1.0f, 0.9f, 0.2f, a));
        }
    }
    if (m_showColliders) {
        for (const Collider& c : m_sim.world.colliders()) {
            if (c.terrain || glm::length(c.box.center - m_camera.position) > 40.0f) continue;
            glm::vec4 col = !c.blocksBullets ? glm::vec4(0.3f, 0.9f, 0.3f, 0.5f) : c.penetrable ? glm::vec4(0.2f, 0.6f, 1.0f, 0.8f)
                                                                                                : glm::vec4(0.2f, 1.0f, 0.3f, 0.5f);
            m_debug.box(c.box, col);
        }
    }
    if (m_showDebug && wpn.hasWeapon()) {
        glm::vec3 mz = wpn.muzzle();
        m_debug.line(mz, mz + wpn.boreDirection() * 3.0f, glm::vec4(1.0f, 0.2f, 0.2f, 0.9f));
    }
    m_debug.render(m_camera.viewProjection());
    m_debug.clear();

    if (scoped) {
        glm::vec2 centre;
        if (project(optic.ocular, centre)) {
            float dist = std::max(glm::length(optic.ocular - m_camera.position), 0.02f);
            float rPx = (optic.ocularRadius / dist) / std::tan(m_camera.fovY * 0.5f) * float(h) * 0.5f;
            glm::vec3 local = glm::conjugate(optic.axis) * (m_camera.position - optic.ocular);
            float tolerance = std::max(0.006f, 2.0f * optic.objectiveRadius / optic.magnification);
            glm::vec2 shadow = glm::vec2(local.x, -local.y) / tolerance * rPx * 0.35f;
            if (glm::length(shadow) > rPx * 2.0f) shadow = glm::normalize(shadow) * rPx * 2.0f;
            ScopeOverlay::Params sp;
            sp.screen = glm::vec2(float(w), float(h));
            sp.center = centre;
            sp.radius = rPx;
            sp.shadow = shadow;
            sp.pxPerMrad = rPx / std::tan(trueHalfFov) * 0.001f;
            sp.reticle = int(optic.reticle);
            sp.opacity = std::pow(wpn.adsBlend(), 2.0f);
            sp.image = m_scopeTarget.color;
            m_scopeOverlay.draw(sp);
        }
    }
    renderOptic(alpha, w, h);
    drawHud();
    m_hud.render(w, h);
}

void Game::renderOptic(float alpha, int, int h) {
    OpticView optic = m_sim.player().weapon.opticView(alpha);
    if (optic.type != OpticType::RedDot || !m_sim.player().alive()) return;
    glm::vec2 dot, window;
    glm::vec3 axisDir = optic.axis * glm::vec3(0, 0, -1);
    if (!project(m_camera.position + axisDir * 500.0f, dot) || !project(optic.ocular, window)) return;
    float dist = std::max(glm::length(optic.ocular - m_camera.position), 0.05f);
    float winPx = (optic.ocularRadius / dist) / std::tan(m_camera.fovY * 0.5f) * float(h) * 0.5f;
    if (glm::length(dot - window) > winPx * 0.92f) return;
    m_hud.circle(dot.x, dot.y, 4.5f, glm::vec4(1.0f, 0.15f, 0.1f, 0.25f));
    m_hud.circle(dot.x, dot.y, 2.0f, glm::vec4(1.0f, 0.25f, 0.2f, 0.95f));
}

bool Game::button(float x, float y, float w, float h, const std::string& label) {
    bool hover = m_ui.mouse.x >= x && m_ui.mouse.y >= y && m_ui.mouse.x < x + w && m_ui.mouse.y < y + h;
    m_hud.rect(x, y, w, h, hover ? glm::vec4(0.35f, 0.3f, 0.15f, 0.95f) : glm::vec4(0.15f, 0.15f, 0.15f, 0.95f));
    float s = 2.0f * std::clamp(float(m_window->height()) / 900.0f, 0.75f, 2.0f);
    m_hud.text(x + (w - Hud::textWidth(label, s)) * 0.5f, y + (h - 12 * s) * 0.5f, label, kWhite, s);
    return hover && m_ui.lmbPressed;
}

void Game::drawCompass(float W) {
    float yawDeg = std::fmod(glm::degrees(-m_camera.yaw) + 3600.0f, 360.0f); // 0 = -Z = north
    float cx = W * 0.5f, y = 14.0f, span = 90.0f, width = 420.0f;
    m_hud.rect(cx - width * 0.5f, y, width, 22, glm::vec4(0, 0, 0, 0.35f));
    for (int d = 0; d < 360; d += 15) {
        float rel = std::fmod(float(d) - yawDeg + 540.0f, 360.0f) - 180.0f;
        if (std::abs(rel) > span) continue;
        float x = cx + rel / span * width * 0.5f;
        const char* names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        if (d % 45 == 0) {
            std::string n = names[d / 45];
            m_hud.text(x - Hud::textWidth(n, 1.6f) * 0.5f, y + 3, n, d == 0 ? kRed : kWhite, 1.6f);
        } else {
            m_hud.rect(x, y + 12, 1, 8, kDim);
        }
    }
    m_hud.rect(cx - 1, y + 20, 2, 6, kAccent);
    std::string deg = format("%03.0f", yawDeg);
    m_hud.text(cx - Hud::textWidth(deg, 1.4f) * 0.5f, y + 26, deg, kDim, 1.4f);
}

void Game::drawBodyStatus(float x, float y, float s) {
    const Health& h = m_sim.player().health;
    struct Box {
        int zone;
        float x, y, w, h;
    };
    const Box boxes[] = {{Z_Head, 12, 0, 12, 10},  {Z_Thorax, 9, 11, 18, 16}, {Z_Stomach, 10, 28, 16, 10}, {Z_LArm, 0, 11, 8, 24},
                         {Z_RArm, 28, 11, 8, 24}, {Z_LLeg, 9, 39, 8, 26},    {Z_RLeg, 19, 39, 8, 26}};
    for (const Box& b : boxes) {
        const ZoneState& z = h.zone(b.zone);
        float f = z.max > 0 ? z.hp / z.max : 0.0f;
        glm::vec4 c = z.destroyed() ? glm::vec4(0.05f, 0.05f, 0.05f, 0.9f) : glm::vec4(glm::mix(glm::vec3(0.8f, 0.1f, 0.1f), glm::vec3(0.3f, 0.7f, 0.3f), f), 0.75f);
        if ((z.heavyBleed || z.lightBleed) && std::fmod(m_sim.time, 0.8) < 0.4) c = glm::vec4(1.0f, 0.1f, 0.1f, 0.95f);
        m_hud.rect(x + b.x * s, y + b.y * s, b.w * s, b.h * s, c);
        if (z.fracture) m_hud.rect(x + b.x * s, y + (b.y + b.h * 0.5f) * s, b.w * s, 1.5f * s, glm::vec4(1, 1, 1, 0.9f));
    }
}

void Game::drawHud() {
    const float W = float(std::max(1, m_window->width())), H = float(std::max(1, m_window->height()));
    Character& p = m_sim.player();
    const Weapon& wpn = p.weapon;
    bool raid = m_sim.mode == Simulation::Mode::Raid;

    // Incoming fire: darkened edges when rounds crack past, a red flash when hit.
    if (m_sim.suppression > 0.01f) {
        const int bands = 10;
        for (int i = 0; i < bands; ++i) {
            float t = float(i) / bands;
            float a = m_sim.suppression * 0.55f * (1.0f - t) * (1.0f - t) / bands * 3.0f;
            float bw = W * 0.18f / bands, bh = H * 0.18f / bands;
            glm::vec4 col(0.0f, 0.0f, 0.0f, a);
            m_hud.rect(i * bw, 0, bw, H, col);
            m_hud.rect(W - (i + 1) * bw, 0, bw, H, col);
            m_hud.rect(0, i * bh, W, bh, col);
            m_hud.rect(0, H - (i + 1) * bh, W, bh, col);
        }
    }
    if (m_sim.hitFlash > 0.0f) {
        m_hud.rect(0, 0, W, H, glm::vec4(0.8f, 0.0f, 0.0f, 0.35f * m_sim.hitFlash));
        glm::vec3 to = m_sim.hitFrom - m_camera.position;
        float ang = std::atan2(glm::dot(to, m_camera.right()), glm::dot(to, m_camera.forward()));
        glm::vec2 c(W * 0.5f + std::sin(ang) * 170.0f, H * 0.5f - std::cos(ang) * 170.0f);
        for (int i = 0; i < 4; ++i) {
            glm::vec2 q = c - glm::vec2(std::sin(ang), -std::cos(ang)) * float(i) * 9.0f;
            m_hud.circle(q.x, q.y, 8.0f - i * 1.5f, glm::vec4(1.0f, 0.15f, 0.1f, 0.9f * m_sim.hitFlash));
        }
    }
    // Blood loss greys the world out.
    float hpFrac = p.health.totalHp() / p.health.totalMax();
    if (hpFrac < 0.5f && p.alive()) m_hud.rect(0, 0, W, H, glm::vec4(0.1f, 0.0f, 0.0f, (0.5f - hpFrac) * 0.6f));

    if (m_screen == Screen::Results) {
        drawResults(W, H);
        return;
    }
    if (!p.alive()) {
        std::string msg = "You died: " + p.causeOfDeath;
        m_hud.text(W * 0.5f - Hud::textWidth(msg, 3.0f) * 0.5f, H * 0.4f, msg, kRed, 3.0f);
        return;
    }

    if (m_screen == Screen::Inventory || m_screen == Screen::Stash || m_screen == Screen::Store || m_screen == Screen::Loot) {
        InventoryUi::Context ctx;
        ctx.player = &p;
        ctx.sim = &m_sim;
        ctx.profile = &m_profile;
        ctx.inRaid = raid;
        if (m_screen == Screen::Store) {
            ctx.panel = InventoryUi::Panel::Store;
            ctx.grid = &m_profile.stash;
        } else if (m_screen == Screen::Stash) {
            ctx.panel = InventoryUi::Panel::Stash;
            ctx.grid = &m_profile.stash;
        } else if (m_screen == Screen::Loot) {
            if (m_lootTarget.type == Interaction::Container) {
                ctx.panel = InventoryUi::Panel::Container;
                ctx.grid = m_sim.containerGrid(m_lootTarget.index);
                if (ctx.grid) ctx.title = containerName(m_sim.loot.containers[m_lootTarget.index].type);
            } else {
                ctx.panel = InventoryUi::Panel::Body;
                ctx.body = m_sim.bodyEquipment(m_lootTarget.index);
                if (ctx.body) ctx.title = "Body: " + m_sim.chars[m_lootTarget.index]->name;
            }
        }
        if (m_inv.update(m_ui, ctx, m_hud, W, H)) closeScreen();
        return;
    }
    if (m_screen == Screen::Map) {
        drawMap(W, H);
        return;
    }
    if (m_screen == Screen::Pause) {
        drawPause(W, H);
        return;
    }
    if (m_screen == Screen::Deploy) {
        drawDeploy(W, H);
        return;
    }

    if (!m_window->isMouseCaptured()) {
        const std::string msg = "Click to play  -  Esc for menu";
        m_hud.rect(0, H * 0.5f - 30, W, 60, glm::vec4(0, 0, 0, 0.45f));
        m_hud.text(W * 0.5f - Hud::textWidth(msg, 3.0f) * 0.5f, H * 0.5f - 10, msg, kWhite, 3.0f);
    }
    if (raid) drawCompass(W);

    // Top-left: status
    float y = 12;
    y += m_hud.text(12, y, format("%.0f fps", m_fps), kDim, 1.6f);
    if (raid) {
        int m = int(m_sim.time / 60.0), s = int(m_sim.time) % 60;
        y += m_hud.text(12, y, format("Raid time %d:%02d", m, s), kDim, 1.6f);
    } else {
        y += m_hud.text(12, y, "BASE - shooting range", kAccent, 2.0f);
    }

    // Bottom-left: weapon
    float by = H - 120;
    if (wpn.hasWeapon()) {
        const WeaponDef& def = wpn.def();
        const OpticDef& opt = wpn.optic();
        m_hud.text(12, by, def.name, kAccent, 2.4f);
        std::string optics = opt.name;
        if (opt.maxMag > opt.minMag) optics += format(" @ %.1fx", wpn.magnification());
        m_hud.text(12, by + 30, format("%s   zero %.0f m   %s", optics.c_str(), wpn.zeroRange(), caliberName(def.caliber)), kWhite);
        const char* stance = wpn.isSwitching() ? "SWITCHING" : wpn.isReloading() ? "RELOADING" : wpn.isHandLoading() ? "LOADING ROUND"
                           : wpn.isCycling() ? (def.action == ActionType::Bolt ? "WORKING BOLT" : "PUMPING")
                           : wpn.isLowered() ? "LOWERED" : p.sprinting ? "SPRINT" : wpn.isAiming() ? "AIMED" : "READY";
        std::string ammo = def.reload == ReloadType::SingleLoad ? (wpn.chamberLoaded() ? "CHAMBERED" : "EMPTY")
                                                                : format("%d / %d", wpn.ammo(), wpn.capacity());
        int spareMags = p.equip.magazinesAvailable(def.magMask, def.caliber);
        int spare = p.equip.roundsAvailable(def.caliber);
        std::string reserve = def.reload == ReloadType::Magazine ? format("  (%d mags, %d loose)", spareMags, spare) : format("  (%d loose)", spare);
        m_hud.text(12, by + 54, format("%s   %s   %s%s", wpn.fireModeName(), stance, ammo.c_str(), reserve.c_str()), kWhite);
        if (*wpn.note()) m_hud.text(12, by + 78, wpn.note(), kRed, 2.0f);
    } else {
        m_hud.text(12, by + 30, "No weapon in hand (1 / 2 to switch, Tab inventory)", kDim);
    }
    // Hand slots
    for (int s = 0; s < 2; ++s) {
        const Item* it = p.equip.slots[s].get();
        std::string l = format("%d %s", s + 1, it ? it->d().name.c_str() : "-");
        m_hud.text(12 + s * 300, H - 30, l, s == wpn.hand() ? kAccent : glm::vec4(0.8f, 0.8f, 0.8f, 0.55f), 1.6f);
    }

    // Bottom-right: body, heart rate, breath
    float bx = W - 130, byy = H - 170;
    drawBodyStatus(bx, byy, 2.4f);
    float e = p.exertion();
    glm::vec4 hr = e < 0.4f ? glm::mix(kWhite, glm::vec4(1.0f, 0.75f, 0.2f, 1.0f), e / 0.4f)
                            : glm::mix(glm::vec4(1.0f, 0.75f, 0.2f, 1.0f), glm::vec4(1.0f, 0.2f, 0.15f, 1.0f), (e - 0.4f) / 0.6f);
    m_hud.text(bx - 150, byy + 10, format("HR %.0f bpm", p.heartRate), hr, 2.0f);
    m_hud.text(bx - 150, byy + 36, format("HP %.0f", p.health.totalHp()), kWhite, 2.0f);
    float ly = byy + 62;
    if (p.health.heavyBleeding()) ly += m_hud.text(bx - 150, ly, "HEAVY BLEEDING [4]", kRed, 1.6f);
    else if (p.health.bleeding()) ly += m_hud.text(bx - 150, ly, "Bleeding [4]", kRed, 1.6f);
    if (p.health.anyFracture()) ly += m_hud.text(bx - 150, ly, "Fracture [5]", glm::vec4(1.0f, 0.75f, 0.3f, 1.0f), 1.6f);
    if (p.health.pain() > 0.3f) ly += m_hud.text(bx - 150, ly, "Pain [7]", glm::vec4(1.0f, 0.75f, 0.3f, 1.0f), 1.6f);
    if (wpn.isAiming() || wpn.breathRemaining() < 1.0f) {
        float b = std::clamp(wpn.breathRemaining(), 0.0f, 1.0f);
        m_hud.rect(bx - 150, byy - 16, 120, 8, glm::vec4(0, 0, 0, 0.5f));
        m_hud.rect(bx - 150, byy - 16, 120 * b, 8, glm::vec4(0.6f, 0.85f, 1.0f, 0.85f));
    }

    // Centre: interaction prompt and timed actions
    Interaction f = m_sim.focusFor(p);
    std::string prompt = m_sim.describe(f, p);
    if (!prompt.empty() && p.action() == Character::Action::None)
        m_hud.text(W * 0.5f - Hud::textWidth(prompt, 2.0f) * 0.5f, H * 0.58f, prompt, kWhite, 2.0f);
    if (p.action() != Character::Action::None) {
        std::string l = p.actionLabel() + "   (X cancel)";
        m_hud.text(W * 0.5f - Hud::textWidth(l, 2.0f) * 0.5f, H * 0.58f, l, kAccent, 2.0f);
        m_hud.rect(W * 0.5f - 150, H * 0.58f + 28, 300, 8, glm::vec4(0, 0, 0, 0.6f));
        m_hud.rect(W * 0.5f - 150, H * 0.58f + 28, 300 * std::clamp(p.actionProgress(), 0.0f, 1.0f), 8, kAccent);
    }
    if (m_sim.lastHitTimer > 0.0f) {
        float a = std::min(1.0f, m_sim.lastHitTimer);
        m_hud.text(W * 0.5f - Hud::textWidth(m_sim.lastHitReport) * 0.5f, H * 0.68f, m_sim.lastHitReport, glm::vec4(1.0f, 0.55f, 0.45f, a));
    }

    // Top-right: feed
    float fy = 50;
    for (const FeedLine& l : m_sim.feed()) {
        float a = std::min(1.0f, l.timer);
        m_hud.text(W - 12 - Hud::textWidth(l.text, 1.8f), fy, l.text, glm::vec4(0.95f, 0.9f, 0.8f, a), 1.8f);
        fy += 24;
    }

    if (m_showDebug) {
        glm::vec2 centre(W * 0.5f, H * 0.5f);
        m_hud.rect(centre.x - 2, centre.y - 2, 4, 4, glm::vec4(1, 1, 1, 0.9f));
        if (wpn.hasWeapon()) {
            glm::vec3 mz = wpn.muzzle();
            WorldHit hit;
            glm::vec3 far = mz + wpn.boreDirection() * 1000.0f;
            SegmentQuery q;
            q.ignoreOwner = 0;
            glm::vec3 pt = m_sim.world.segment(mz, far, hit, q) ? hit.point : far;
            glm::vec2 s;
            if (project(pt, s)) {
                const glm::vec4 red(1.0f, 0.25f, 0.2f, 0.95f);
                m_hud.rect(s.x - 7, s.y - 7, 14, 2, red);
                m_hud.rect(s.x - 7, s.y + 5, 14, 2, red);
                m_hud.rect(s.x - 7, s.y - 7, 2, 14, red);
                m_hud.rect(s.x + 5, s.y - 7, 2, 14, red);
            }
        }
        float dy = fy + 10;
        auto line = [&](const std::string& str) { dy += m_hud.text(W - 12 - Hud::textWidth(str), dy, str, kAccent); };
        line(format("aim error %.2f deg  drift %.2f/%.2f", glm::degrees(wpn.arm.errorAngle), glm::degrees(wpn.drift().x), glm::degrees(wpn.drift().y)));
        line(format("projectiles %d  static chunks %d  tris %zu", m_sim.ballistics.activeCount(), m_renderer.lastStaticChunks, m_scene.triangleCount()));
        line(format("pos %.0f %.0f %.0f", p.position.x, p.position.y, p.position.z));
        for (auto& c : m_sim.chars)
            if (c->id != 0 && c->alive())
                line(format("%s: %s  %.0f m  hp %.0f", c->name.c_str(), m_sim.bots.describe(c->id).c_str(),
                            glm::length(c->position - p.position), c->health.totalHp()));
        // Labels over bots
        for (auto& c : m_sim.chars) {
            if (c->id == 0 || !c->alive()) continue;
            glm::vec2 s;
            if (project(c->head() + glm::vec3(0, 0.4f, 0), s)) m_hud.text(s.x - 20, s.y, c->name + " " + m_sim.bots.describe(c->id), kAccent, 1.4f);
        }
    } else if (m_showHelp) {
        std::vector<const char*> help = {
            "WASD move  Shift sprint  Ctrl crouch  Space jump  Q/E lean",
            "LMB fire  RMB aim  Shift+RMB hold breath  R reload (RR dump)  B fire mode",
            "1/2 weapons  Wheel zoom  [ ] zero  F interact (hold to search)",
            "Tab inventory  4 stop bleeding  5 splint  6 heal  7 painkillers  X cancel",
        };
        if (raid) help.push_back("M map   Extract at a flare pole; send back-slot guns home at its crate");
        else help.push_back("Locker: stash and loadout   Gate: deploy   N spawn raider   K remove   H hide");
        float dy = fy + 10;
        for (const char* s : help) dy += m_hud.text(W - 12 - Hud::textWidth(s, 1.6f), dy, s, kDim, 1.6f);
    }
}

void Game::drawMap(float W, float H) {
    m_hud.rect(0, 0, W, H, glm::vec4(0, 0, 0, 0.6f));
    float size = std::min(W, H) * 0.9f;
    float x0 = (W - size) * 0.5f, y0 = (H - size) * 0.5f;
    float half = std::max(m_sim.map.halfSize, 1.0f);
    auto toMap = [&](glm::vec2 p) { return glm::vec2(x0 + (p.x + half) / (2 * half) * size, y0 + (p.y + half) / (2 * half) * size); };
    m_hud.rect(x0, y0, size, size, glm::vec4(0.78f, 0.74f, 0.62f, 0.97f));
    for (const MapRect& r : m_sim.map.mapRects) {
        glm::vec2 dir(std::cos(r.angle), std::sin(r.angle));
        glm::vec4 col(r.color, 0.85f);
        if (std::abs(r.angle) < 1e-3f) {
            glm::vec2 a = toMap(r.center - r.half), b = toMap(r.center + r.half);
            m_hud.rect(a.x, a.y, b.x - a.x, b.y - a.y, col);
        } else {
            // Rotated strip (roads): a chain of small squares.
            int n = int(r.half.x * 2.0f / 3.0f) + 1;
            for (int i = 0; i <= n; ++i) {
                glm::vec2 p = r.center + dir * (-r.half.x + 2.0f * r.half.x * float(i) / n);
                glm::vec2 m = toMap(p);
                float w = std::max(2.0f, r.half.y * 2.0f / (2 * half) * size);
                m_hud.rect(m.x - w * 0.5f, m.y - w * 0.5f, w, w, col);
            }
        }
    }
    for (const MapLabel& l : m_sim.map.labels) {
        if (l.name == "Woods") continue;
        glm::vec2 m = toMap(l.center);
        m_hud.text(m.x - Hud::textWidth(l.name, 1.6f) * 0.5f, m.y - 30, l.name, glm::vec4(0.15f, 0.12f, 0.1f, 1.0f), 1.6f);
    }
    for (const Hotspot& h : m_sim.map.hotspots) {
        glm::vec2 m = toMap(glm::vec2(h.position.x, h.position.z));
        m_hud.circle(m.x, m.y, 7, glm::vec4(0.7f, 0.15f, 0.1f, 0.9f));
        m_hud.text(m.x + 10, m.y - 6, h.name, glm::vec4(0.5f, 0.1f, 0.05f, 1.0f), 1.5f);
    }
    for (const ExtractPoint& e : m_sim.map.extracts) {
        glm::vec2 m = toMap(glm::vec2(e.position.x, e.position.z));
        m_hud.circle(m.x, m.y, 9, glm::vec4(0.1f, 0.5f, 0.15f, 0.95f));
        m_hud.text(m.x + 12, m.y - 6, "EXFIL " + e.name, glm::vec4(0.05f, 0.35f, 0.1f, 1.0f), 1.5f);
    }
    m_hud.text(x0 + 10, y0 + 10, "N", glm::vec4(0.6f, 0.1f, 0.1f, 1.0f), 3.0f);
    m_hud.text(x0 + 10, y0 + size - 28, "Grid: 100 m.  No GPS: use the compass and landmarks.   M close", glm::vec4(0.2f, 0.18f, 0.15f, 1.0f), 1.5f);
    for (int i = 1; i < 8; ++i) {
        float t = i / 8.0f * size;
        m_hud.rect(x0 + t, y0, 1, size, glm::vec4(0.3f, 0.28f, 0.22f, 0.25f));
        m_hud.rect(x0, y0 + t, size, 1, glm::vec4(0.3f, 0.28f, 0.22f, 0.25f));
    }
}

void Game::drawResults(float W, float H) {
    m_hud.rect(0, 0, W, H, glm::vec4(0.03f, 0.03f, 0.03f, 0.9f));
    float s = std::clamp(H / 900.0f, 0.75f, 2.0f);
    std::string title = m_extracted ? "EXTRACTED" : "KILLED IN ACTION";
    m_hud.text(W * 0.5f - Hud::textWidth(title, 5.0f * s) * 0.5f, H * 0.18f, title, m_extracted ? kGreen : kRed, 5.0f * s);
    float y = H * 0.32f;
    for (const std::string& l : m_resultLines) {
        m_hud.text(W * 0.5f - Hud::textWidth(l, 2.0f * s) * 0.5f, y, l, kWhite, 2.0f * s);
        y += 30 * s;
    }
    std::string career = format("Raids %d   Extractions %d   Deaths %d   Kills %d", m_profile.raids, m_profile.extractions, m_profile.deaths, m_profile.kills);
    m_hud.text(W * 0.5f - Hud::textWidth(career, 1.6f * s) * 0.5f, y + 20 * s, career, kDim, 1.6f * s);
    if (button(W * 0.5f - 160 * s, H * 0.8f, 320 * s, 50 * s, "Return to base")) returnToBase();
}

void Game::drawPause(float W, float H) {
    m_hud.rect(0, 0, W, H, glm::vec4(0, 0, 0, 0.6f));
    float s = std::clamp(H / 900.0f, 0.75f, 2.0f);
    float bw = 360 * s, bh = 50 * s, x = W * 0.5f - bw * 0.5f, y = H * 0.35f;
    if (button(x, y, bw, bh, "Resume")) closeScreen();
    bool raid = m_sim.mode == Simulation::Mode::Raid;
    if (button(x, y + 70 * s, bw, bh, raid ? "Quit (gear is lost)" : "Save and quit")) m_window->requestClose();
    std::string info = raid ? "Leaving a raid early counts as dying: only what you sent home is kept." : "Your stash is saved to save/profile.txt";
    m_hud.text(W * 0.5f - Hud::textWidth(info, 1.6f * s) * 0.5f, y + 150 * s, info, kDim, 1.6f * s);
    std::string cfg = format("config.txt: bots=%d  (raiders kept alive in each raid)", m_config.bots);
    m_hud.text(W * 0.5f - Hud::textWidth(cfg, 1.6f * s) * 0.5f, y + 175 * s, cfg, kDim, 1.6f * s);
}

void Game::drawDeploy(float W, float H) {
    m_hud.rect(0, 0, W, H, glm::vec4(0, 0, 0, 0.7f));
    float s = std::clamp(H / 900.0f, 0.75f, 2.0f);
    std::string title = "DEPLOY";
    m_hud.text(W * 0.5f - Hud::textWidth(title, 4.0f * s) * 0.5f, H * 0.22f, title, kAccent, 4.0f * s);
    float bw = 620 * s, bh = 70 * s, x = W * 0.5f - bw * 0.5f, y = H * 0.36f;
    int value = m_sim.player().equip.totalValue();
    if (button(x, y, bw, bh, format("Deploy with my loadout (value %d)", value))) {
        deploy(false);
        return;
    }
    m_hud.text(x, y + bh + 6 * s, "Everything you carry is at risk. Extract to keep it.", kDim, 1.5f * s);
    if (button(x, y + 130 * s, bw, bh, "Deploy with a pistol only (free)")) {
        deploy(true);
        return;
    }
    m_hud.text(x, y + 130 * s + bh + 6 * s, "Your loadout stays safe at home. Anything you bring out goes to the stash.", kDim, 1.5f * s);
    m_hud.text(x, y + 260 * s, "Esc  back", kDim, 1.6f * s);
}
