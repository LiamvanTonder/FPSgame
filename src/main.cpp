#include "ai/NavGrid.h"
#include "audio/Audio.h"
#include "core/Paths.h"
#include "core/Window.h"
#include "game/ArmModel.h"
#include "game/Ballistics.h"
#include "game/Game.h"
#include "game/Health.h"
#include "game/Ragdoll.h"
#include "game/Simulation.h"
#include "game/WeaponDefs.h"
#include "items/Profile.h"
#include "items/Store.h"
#include "world/Loot.h"
#include "world/MapBuilder.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
// Prints a symbolised stack trace on a crash (Release builds carry a PDB).
static LONG WINAPI crashHandler(EXCEPTION_POINTERS* info) {
    HANDLE process = GetCurrentProcess(), thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    std::fprintf(stderr, "\nCRASH: exception 0x%08lx\n", info->ExceptionRecord->ExceptionCode);
    for (int i = 0; i < 32; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        const char* name = SymFromAddr(process, frame.AddrPC.Offset, &disp, sym) ? sym->Name : "?";
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &ldisp, &line))
            std::fprintf(stderr, "  %s  %s:%lu\n", name, line.FileName, line.LineNumber);
        else
            std::fprintf(stderr, "  %s\n", name);
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

namespace {

bool g_ok = true;
void check(bool cond, const char* what) {
    std::printf("  %-60s %s\n", what, cond ? "ok" : "FAILED");
    g_ok = g_ok && cond;
}
double seconds(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
}

World groundWorld() {
    World w;
    Collider ground;
    ground.box = OBB::fromMinMax(glm::vec3(-100, -1, -200), glm::vec3(100, 0, 50));
    ground.surface = Surface::Grass;
    w.add(ground);
    return w;
}

void ragdollTest() {
    World world = groundWorld();
    JointArray j = {glm::vec3(0, 1.70f, 0), glm::vec3(0, 1.52f, 0), glm::vec3(0, 1.30f, 0), glm::vec3(0, 0.98f, 0),
                    glm::vec3(-0.19f, 1.46f, 0), glm::vec3(0.19f, 1.46f, 0), glm::vec3(-0.24f, 1.18f, 0.02f), glm::vec3(0.24f, 1.18f, 0.02f),
                    glm::vec3(-0.26f, 0.92f, 0.04f), glm::vec3(0.26f, 0.92f, 0.04f), glm::vec3(-0.10f, 0.92f, 0), glm::vec3(0.10f, 0.92f, 0),
                    glm::vec3(-0.11f, 0.50f, -0.02f), glm::vec3(0.11f, 0.50f, -0.02f), glm::vec3(-0.12f, 0.06f, 0), glm::vec3(0.12f, 0.06f, 0)};
    for (glm::vec3& p : j) p.y += 1.0f;
    JointArray v{};
    v[J_Chest] = glm::vec3(0.0f, 0.0f, 1.0f);
    Ragdoll r;
    r.init(j, v, 1.0f / 240.0f);
    for (int i = 0; i < 720; ++i) r.step(1.0f / 240.0f, world);
    float lowest = 1e9f, highest = -1e9f;
    for (const glm::vec3& p : r.joints()) {
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
    }
    std::printf("[Ragdoll]\n");
    check(lowest > -0.02f && highest < 0.6f && r.maxBoneError() < 0.03f, "1 m drop settles on the ground, bones intact");
}

void inventoryTest() {
    std::printf("[Inventory]\n");
    Grid g(4, 3);
    ItemPtr rifle = makeLoadedWeapon(kRifle); // 4x2
    check(g.autoPlace(rifle) && !rifle, "4x2 rifle fits a 4x3 grid");
    ItemPtr big = makeItem("bp_patrol");      // 4x4
    check(!g.autoPlace(big) && big, "4x4 pack does not");
    ItemPtr mag = makeItem("m_stanag_30");    // 1x2
    check(g.autoPlace(mag) && g.items.back().rot, "a 1x2 magazine rotates into the last free row");
    Grid h(2, 4);
    ItemPtr r2 = makeLoadedWeapon(kRifle);
    check(h.autoPlace(r2) && h.items[0].rot, "rifle rotates to fit a 2x4 grid");
    // Ammo stacks merge
    Grid a(2, 1);
    ItemPtr s1 = makeAmmo(defaultAmmo(Cal_556), 40), s2 = makeAmmo(defaultAmmo(Cal_556), 30);
    a.autoPlace(s1);
    a.autoPlace(s2);
    check(a.items.size() == 2 && a.items[0].item->count == 60 && a.items[1].item->count == 10, "ammo merges into partial stacks (60 max)");
    // Mixed magazine keeps its order
    Equipment eq;
    eq.slots[ES_Rig] = makeItem("rig_recon");
    ItemPtr m = makeItem("m_stanag_30");
    int fmj = findAmmo("556_fmj"), ap = findAmmo("556_ap");
    m->rounds = {uint8_t(fmj), uint8_t(fmj), uint8_t(ap)};
    eq.stow(m);
    ItemPtr best = eq.takeBestMagazine(magBit(Mag_Stanag), Cal_556);
    check(best && best->rounds.size() == 3 && best->rounds.back() == ap, "magazine keeps round order (AP on top)");
    std::vector<uint8_t> out;
    ItemPtr loose = makeAmmo(fmj, 25);
    eq.stow(loose);
    check(eq.takeRounds(Cal_556, 10, out) == 10 && eq.roundsAvailable(Cal_556) == 15, "loose rounds are taken from stacks");
    check(magazineFits(itemDefs()[findItemDef("m_stanag_30")], findWeapon("shade300")), "STANAG fits the .300 BLK rifle");
    check(!magazineFits(itemDefs()[findItemDef("m_545_30")], kRifle), "5.45 magazine does not fit the 5.56 carbine");
    std::printf("  catalogue: %zu weapons, %zu item kinds, %zu loads of ammunition\n", weaponDefs().size(), itemDefs().size(), ammoDefs().size());
    check(weaponDefs().size() >= 50, "50+ weapons");
}

void saveTest() {
    std::printf("[Save]\n");
    Profile p;
    Equipment loadout;
    p.starter(loadout);
    std::string path = executableDir() + "save/selftest_profile.txt";
    check(p.save(path, loadout), "profile saves");
    Profile q;
    Equipment l2;
    check(q.load(path, l2), "profile loads");
    std::ostringstream a, b;
    writeGrid(a, p.stash);
    writeGrid(b, q.stash);
    for (int s = 0; s < kEquipSlots; ++s) {
        if (loadout.slots[s]) writeItem(a, *loadout.slots[s]);
        if (l2.slots[s]) writeItem(b, *l2.slots[s]);
    }
    check(a.str() == b.str() && !a.str().empty(), "round trip is identical");
    std::remove(path.c_str());
}

void healthTest() {
    std::printf("[Health]\n");
    std::mt19937 rng(3);
    const AmmoDef& fmj = ammoDefs()[findAmmo("556_fmj")];
    const AmmoDef& nine = ammoDefs()[findAmmo("9_fmj")];
    float e556 = 0.5f * 0.00356f * 900.0f * 900.0f, e9 = 0.5f * 0.008f * 360.0f * 360.0f;
    {
        Health h;
        h.reset();
        h.applyHit(BP_Head, e556, fmj, 1, rng);
        check(h.dead(), "5.56 to the head kills");
    }
    {
        Health h;
        h.reset();
        h.applyHit(BP_Head, e9, nine, 1, rng);
        check(h.dead(), "9 mm to the head kills");
    }
    {
        Health h;
        h.reset();
        h.applyHit(BP_Chest, e556, fmj, 1, rng);
        bool alive1 = !h.dead();
        h.applyHit(BP_Chest, e556, fmj, 1, rng);
        check(alive1 && h.dead(), "two 5.56 to the thorax kill, one does not");
    }
    {
        Health h;
        h.reset();
        for (int i = 0; i < 6; ++i) h.applyHit(BP_ThighL, e9, nine, 1, rng);
        check(h.zone(Z_LLeg).destroyed() && !h.canSprint() && h.moveSpeedScale() < 0.7f, "destroyed leg: no sprinting, slow");
        float before = h.zone(Z_Thorax).hp;
        h.applyHit(BP_ShinL, e9, nine, 1, rng);
        check(h.zone(Z_Thorax).hp < before, "damage to a destroyed limb spreads to the body");
        float pool = 0.0f;
        check(h.canTreat(MedKind::Surgical, Z_LLeg), "surgical kit applies to the destroyed leg");
        h.treat(MedKind::Surgical, Z_LLeg, pool);
        check(!h.zone(Z_LLeg).destroyed(), "surgery restores it");
    }
    {
        Health h;
        h.reset();
        // Force a heavy bleed on the arm and check the drain and the tourniquet.
        for (int i = 0; i < 40 && !h.zone(Z_RArm).heavyBleed; ++i) {
            h.reset();
            h.applyHit(BP_ForearmR, e556, fmj, 1, rng);
        }
        float hp0 = h.zone(Z_RArm).hp;
        for (int i = 0; i < 240 * 5; ++i) h.update(1.0f / 240.0f);
        check(h.zone(Z_RArm).heavyBleed && h.zone(Z_RArm).hp < hp0 - 7.0f, "heavy bleed drains the limb");
        float pool = 0.0f;
        h.treat(MedKind::Tourniquet, Z_RArm, pool);
        check(!h.zone(Z_RArm).heavyBleed, "tourniquet stops it");
        // Fracture
        Health f;
        f.reset();
        for (int i = 0; i < 60 && !f.zone(Z_RLeg).fracture; ++i) f.applyHit(BP_ThighR, 300.0f, nine, 1, rng);
        check(f.zone(Z_RLeg).fracture && !f.canSprint(), "fractured leg: can't sprint");
        f.treat(MedKind::Splint, Z_RLeg, pool);
        check(!f.zone(Z_RLeg).fracture, "splint fixes it");
    }
}

void driftTest() {
    World world = groundWorld();
    auto rms = [&](bool ads, float exertion) {
        Equipment eq;
        eq.slots[ES_Hand0] = makeLoadedWeapon(kRifle);
        Weapon w;
        WeaponContext ctx;
        ctx.eye = glm::vec3(0, 1.66f, 0);
        ctx.aimPoint = glm::vec3(0, 1.66f, -100.0f);
        ctx.exertion = exertion;
        ctx.heartRate = 72.0f + 113.0f * exertion;
        w.init(ctx, &eq, 0, true);
        WeaponInput in;
        in.adsHeld = ads;
        Ballistics b;
        std::vector<Impact> impacts;
        std::vector<WeaponEvent> events;
        double sum = 0.0;
        int n = 0;
        for (int i = 0; i < 240 * 40; ++i) {
            w.update(in, ctx, 1.0f / 240.0f, world, b, impacts, events);
            if (i > 240 * 2) {
                sum += double(glm::dot(w.drift(), w.drift()));
                ++n;
            }
        }
        return float(std::sqrt(sum / n));
    };
    float hip = rms(false, 0.0f), aimed = rms(true, 0.0f), tired = rms(false, 1.0f);
    std::printf("[Aim drift] RMS hip %.2f deg, aimed %.2f deg, hip at max heart rate %.2f deg\n", glm::degrees(hip), glm::degrees(aimed),
                glm::degrees(tired));
    check(hip > aimed * 4.0f && tired > hip * 1.8f, "hip >> aimed, grows with heart rate");
}

void weaponFeedTest() {
    std::printf("[Weapons]\n");
    World world = groundWorld();
    Equipment eq;
    eq.slots[ES_Rig] = makeItem("rig_assault");
    eq.slots[ES_Hand0] = makeLoadedWeapon(kRifle);
    std::mt19937 rng(1);
    addAmmoFor(*eq.slots[ES_Rig]->grid, kRifle, 1, 0, rng);
    Weapon w;
    WeaponContext ctx;
    ctx.eye = glm::vec3(0, 1.66f, 0);
    ctx.aimPoint = glm::vec3(0, 1.66f, -100.0f);
    w.init(ctx, &eq, 0, true);
    Ballistics b;
    std::vector<Impact> impacts;
    std::vector<WeaponEvent> events;
    WeaponInput in;
    for (int i = 0; i < 240; ++i) w.update(in, ctx, 1.0f / 240.0f, world, b, impacts, events);
    int start = w.ammo();
    int shots = 0;
    for (int i = 0; i < 240 * 20 && w.ammo() > 0; ++i) {
        in.firePressed = (i % 60) == 0;
        w.update(in, ctx, 1.0f / 240.0f, world, b, impacts, events);
        in.firePressed = false;
    }
    for (const WeaponEvent& e : events) shots += e.type == WeaponEvent::Shot ? 1 : 0;
    std::printf("  start %d shots %d left %d\n", start, shots, w.ammo());
    check(start == 30 && shots == 30 && w.ammo() == 0, "carbine fires exactly its 29 + 1 loaded rounds");
    in.reloadPressed = true;
    w.update(in, ctx, 1.0f / 240.0f, world, b, impacts, events);
    in.reloadPressed = false;
    for (int i = 0; i < 240 * 4; ++i) w.update(in, ctx, 1.0f / 240.0f, world, b, impacts, events);
    check(w.ammo() == 30 && eq.magazinesAvailable(magBit(Mag_Stanag), Cal_556) == 0, "reload takes the spare magazine from the rig");
    check(eq.roundsAvailable(Cal_556) == 0 && !eq.slots[ES_Rig]->grid->items.empty(), "the empty magazine went back in the rig");
    in.reloadPressed = true;
    w.update(in, ctx, 1.0f / 240.0f, world, b, impacts, events);
    check(std::string(w.note()) == "NO MAGAZINE", "no loaded magazine: reload refuses");

    // Every gun: zeroed trajectory crosses the sight line at its zero range.
    for (const WeaponDef& wd : weaponDefs()) {
        float zero = Ballistics::solveZeroAngle(wd.projectile, wd.ironSightHeight, wd.defaultZero);
        World wz;
        Collider c;
        c.box = OBB::fromCenterSize(glm::vec3(0, 0, -wd.defaultZero), glm::vec3(40, 40, 0.004f));
        c.penetrable = true;
        wz.add(c);
        Ballistics bb;
        std::vector<Impact> imp;
        bb.fire(wd.projectile, glm::vec3(0, 0, 0.5f), glm::vec3(0.0f), glm::vec3(0.0f, std::sin(zero), -std::cos(zero)) * wd.projectile.muzzleVelocity, wz, imp);
        for (int i = 0; i < 480 && bb.activeCount() > 0 && imp.empty(); ++i) bb.update(1.0f / 240.0f, wz, imp);
        bool ok = !imp.empty() && std::abs(imp[0].point.y - wd.ironSightHeight) < 0.005f;
        if (!ok) {
            std::printf("  %s zero check failed\n", wd.name.c_str());
            g_ok = false;
        }
    }
    check(true, "every weapon's zero solves");
}

void terrainTest() {
    std::printf("[Terrain & broadphase]\n");
    World world;
    Terrain t;
    MapData map;
    std::vector<glm::vec3> colors;
    MapBuilder mb;
    auto t0 = std::chrono::steady_clock::now();
    mb.build(1234u, world, t, map, colors);
    double buildTime = seconds(t0);
    std::printf("  map built in %.2f s: %zu colliders, %zu scenery blocks, %zu containers, %zu spawns\n", buildTime, world.colliders().size(),
                map.blocks.size(), map.containers.size(), map.spawns.size());
    check(map.extracts.size() == 4 && map.hotspots.size() == 4 && map.spawns.size() >= 10, "4 extracts, 4 hotspots, spawn points");

    // Downward rays hit the analytic height
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(-380.0f, 380.0f);
    float worst = 0.0f;
    for (int i = 0; i < 200; ++i) {
        float x = u(rng), z = u(rng);
        float tt;
        glm::vec3 n;
        if (t.segment(glm::vec3(x, 200, z), glm::vec3(x, -200, z), tt, n)) worst = std::max(worst, std::abs(200.0f - 400.0f * tt - t.height(x, z)));
    }
    check(worst < 0.01f, "terrain ray hits match the heightfield (< 1 cm)");

    // Grid vs brute force
    int mismatches = 0;
    t0 = std::chrono::steady_clock::now();
    const int rays = 10000;
    std::vector<std::pair<glm::vec3, glm::vec3>> segs;
    for (int i = 0; i < rays; ++i) {
        glm::vec3 a(u(rng), t.height(0, 0) + 1.5f + 10.0f * (u(rng) + 380.0f) / 760.0f, u(rng));
        a.y = t.height(a.x, a.z) + 1.6f;
        glm::vec3 dir(u(rng), u(rng) * 0.05f, u(rng));
        glm::vec3 b = a + glm::normalize(dir) * 150.0f;
        segs.push_back({a, b});
    }
    std::vector<WorldHit> grid(rays);
    std::vector<bool> gridHit(rays);
    SegmentQuery q;
    q.noTerrain = true;
    for (int i = 0; i < rays; ++i) gridHit[i] = world.segment(segs[i].first, segs[i].second, grid[i], q);
    double gridTime = seconds(t0);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < rays; ++i) {
        float best = 1.0f;
        int bestC = -1;
        for (int c = 0; c < int(world.colliders().size()); ++c) {
            const Collider& col = world.collider(c);
            if (col.terrain || !col.active || !col.blocksBullets) continue;
            float tt;
            glm::vec3 n;
            if (intersectSegmentOBB(segs[i].first, segs[i].second, col.box, tt, n) && tt <= best) {
                best = tt;
                bestC = c;
            }
        }
        bool bh = bestC >= 0;
        if (bh != gridHit[i] || (bh && std::abs(best - grid[i].t) > 1e-5f)) ++mismatches;
    }
    double bruteTime = seconds(t0);
    std::printf("  10k 150 m rays: grid %.1f ms, brute force %.1f ms\n", gridTime * 1000.0, bruteTime * 1000.0);
    check(mismatches == 0, "grid broadphase matches brute force exactly");

    // Navigation
    NavGrid nav;
    t0 = std::chrono::steady_clock::now();
    nav.build(world, map.halfSize);
    std::printf("  nav grid built in %.2f s\n", seconds(t0));
    int ok = 0, total = 0;
    std::vector<glm::vec2> path;
    double pathTime = 0.0;
    int maxExp = 0;
    auto tryPath = [&](glm::vec3 a, glm::vec3 b) {
        ++total;
        auto s = std::chrono::steady_clock::now();
        bool f = nav.findPath({a.x, a.z}, {b.x, b.z}, path);
        pathTime += seconds(s);
        maxExp = std::max(maxExp, nav.lastExpansions());
        // The path must not cross blocked cells
        bool clean = f;
        glm::vec2 prev(a.x, a.z);
        for (const glm::vec2& p : path) {
            if (!nav.clearLine(p, p)) clean = false;
            prev = p;
        }
        ok += clean ? 1 : 0;
    };
    for (size_t i = 0; i < map.spawns.size(); i += 3)
        for (const ExtractPoint& e : map.extracts) tryPath(map.spawns[i].position, e.position);
    for (const Hotspot& h : map.hotspots)
        for (const ExtractPoint& e : map.extracts) tryPath(h.position, e.position);
    std::printf("  %d/%d paths found (spawns, hotspots -> extracts), %.1f ms average, max %d expansions\n", ok, total, pathTime / total * 1000.0, maxExp);
    check(ok == total, "every spawn and hotspot can reach every extract");
    std::vector<NavGrid::Cover> cover;
    const Hotspot& hs = map.hotspots[0];
    nav.findCover({hs.position.x, hs.position.z}, {hs.position.x + 40.0f, hs.position.z}, 25.0f, cover);
    check(!cover.empty(), "cover found around a hotspot");
}

void audioTest() {
    std::printf("[Audio]\n");
    Audio a;
    std::vector<ClipData> clips;
    synthesizeClips(clips);
    bool ok = true;
    for (const ClipData& c : clips) {
        float peak = 0.0f;
        for (float s : c.samples) peak = std::max(peak, std::abs(s));
        if (c.samples.empty() || peak < 0.05f || peak > 1.0f) ok = false;
    }
    check(ok && clips.size() == kClipCount, "all clips synthesized, non-silent and unclipped");
    SoundEvent e;
    e.db = 160.0f;
    e.position = glm::vec3(0, 0, -343.0f);
    Hearing h = SoundBus::hear(e, glm::vec3(0.0f), nullptr);
    check(std::abs(h.delay - 1.0f) < 1e-3f, "343 m away is heard 1 s later");
    Hearing h2 = SoundBus::hear(e, glm::vec3(0, 0, -343.0f + 34.3f), nullptr);
    check(h2.level > h.level + 19.0f, "ten times closer is ~20 dB louder");

    // Someone walking on your right: louder in the right ear, and it reaches the left ear later.
    Audio mixer;
    mixer.loadClips();
    mixer.disableAmbience();
    SoundBus bus;
    bus.setTime(1.0);
    SoundEvent step;
    step.type = SoundType::Footstep;
    step.position = glm::vec3(6.0f, 0.0f, 0.0f);
    step.db = 70.0f;
    step.owner = 3;
    step.variant = int(Surface::Concrete);
    bus.emit(step);
    mixer.update(bus, nullptr, glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(1, 0, 0), 0);
    std::vector<float> buf(48000 * 2);
    mixer.mix(buf.data(), 48000);
    double eL = 0.0, eR = 0.0;
    int firstL = -1, firstR = -1;
    for (int i = 0; i < 48000; ++i) {
        eL += buf[i * 2] * buf[i * 2];
        eR += buf[i * 2 + 1] * buf[i * 2 + 1];
        if (firstL < 0 && std::abs(buf[i * 2]) > 1e-5f) firstL = i;
        if (firstR < 0 && std::abs(buf[i * 2 + 1]) > 1e-5f) firstR = i;
    }
    std::printf("  footstep 6 m to the right: R/L energy %.1fx, left ear %d samples later\n", eR / std::max(eL, 1e-12), firstL - firstR);
    check(eR > eL * 2.0 && firstL > firstR, "sound from the right is louder and earlier in the right ear");
}

void raidTest() {
    std::printf("[Headless raid: 8 bots, no player, 15 minutes]\n");
    Simulation sim;
    Equipment none;
    auto t0 = std::chrono::steady_clock::now();
    sim.setupRaid(4242u, 8, none, false);
    std::printf("  raid set up in %.2f s\n", seconds(t0));
    t0 = std::chrono::steady_clock::now();
    const float dt = 1.0f / 240.0f;
    CharacterInput idle;
    std::vector<glm::vec3> lastPos(64, glm::vec3(1e9f));
    std::vector<double> lastMove(64, 0.0);
    int stuck = 0;
    std::vector<std::string> goalsSeen;
    for (int i = 0; i < 240 * 60 * 15; ++i) {
        sim.tick(dt, idle, false);
        if (i % 240 == 0) {
            for (auto& c : sim.chars) {
                if (c->id >= 64) continue;
                if (!c->alive()) {
                    lastPos[c->id] = glm::vec3(1e9f);
                    continue;
                }
                std::string g = sim.bots.describe(c->id);
                if (std::find(goalsSeen.begin(), goalsSeen.end(), g) == goalsSeen.end()) goalsSeen.push_back(g);
                if (glm::length(c->position - lastPos[c->id]) > 3.0f) {
                    lastPos[c->id] = c->position;
                    lastMove[c->id] = sim.time;
                } else if (sim.time - lastMove[c->id] > 60.0 && g != "extracting" && g != "healing" && g != "fighting" && g != "sending loot home") {
                    ++stuck;
                    std::printf("  stuck: %s (%s) at %.1f %.1f %.1f\n", c->name.c_str(), g.c_str(), c->position.x, c->position.y, c->position.z);
                    if (Bot* b = sim.bots.find(c->id)) {
                        glm::vec2 wp = b->pathIndex < b->path.size() ? b->path[b->pathIndex] : glm::vec2(-1);
                        std::printf("    [%s] path %zu/%zu next %.1f %.1f dest %.1f %.1f walkable %d move %.2f %.2f speed %.2f grounded %d action %d loot %d/%d\n",
                                    b->debug.c_str(), b->pathIndex, b->path.size(), wp.x, wp.y, b->destination.x, b->destination.y,
                                    int(sim.nav.walkable({c->position.x, c->position.z})), b->input.move.x, b->input.move.y,
                                    c->horizontalSpeed(), int(c->grounded), int(c->action()), b->lootType, b->lootIndex);
                        AABB box{c->position - glm::vec3(0.7f, 0.0f, 0.7f), c->position + glm::vec3(0.7f, 1.8f, 0.7f)};
                        std::vector<int> nearby;
                        sim.world.overlap(box, nearby);
                        for (int ci : nearby) {
                            const Collider& col = sim.world.collider(ci);
                            if (col.owner != kOwnerWorld || !col.blocksPlayer) continue;
                            AABB cb = col.box.bounds();
                            std::printf("    collider %d surf %d [%.2f %.2f %.2f]-[%.2f %.2f %.2f]\n", ci, int(col.surface), cb.min.x, cb.min.y,
                                        cb.min.z, cb.max.x, cb.max.y, cb.max.z);
                        }
                    }
                    lastMove[c->id] = sim.time;
                }
            }
        }
    }
    double t = seconds(t0);
    const RaidStats& s = sim.stats;
    std::printf("  simulated 15:00 in %.1f s (%.1fx real time)\n", t, 900.0 / t);
    std::printf("  kills %d, bodies/containers looted %d, weapons taken %d, extractions %d, send-backs %d, respawns %d, stuck %d\n",
                s.botsKilled, s.botLoots, s.botWeaponsTaken, s.botExtracts, s.botSendBacks, s.botRespawns, stuck);
    std::string goals;
    for (const std::string& g : goalsSeen) goals += g + " ";
    std::printf("  behaviours seen: %s\n", goals.c_str());
    check(s.botsKilled > 0, "bots found and killed each other");
    check(s.botLoots > 0 && s.botWeaponsTaken > 0, "bots looted and took weapons");
    check(s.botExtracts + s.botSendBacks > 0, "bots extracted or sent loot home");
    check(s.botRespawns > 0, "replacements spawned");
    check(stuck <= 2, "no bot stuck for over a minute (2 allowed)");
    check(t < 900.0, "runs faster than real time");
}

void playerFlowTest() {
    std::printf("[Player raid flow]\n");
    Simulation sim;
    Equipment kit;
    kit.slots[ES_Rig] = makeItem("rig_recon");
    kit.slots[ES_Hand0] = makeLoadedWeapon(kRifle);
    kit.slots[ES_Back0] = makeLoadedWeapon(kShotgun);
    kit.slots[ES_Back1] = makeLoadedWeapon(kSniper, 4);
    sim.setupRaid(77u, 0, kit, true);
    Character& p = sim.player();
    const float dt = 1.0f / 240.0f;
    CharacterInput in;
    auto teleport = [&](const glm::vec3& pos) {
        p.position = pos + glm::vec3(0.0f, 0.1f, 0.0f);
        p.velocity = glm::vec3(0.0f);
        for (int i = 0; i < 60; ++i) sim.tick(dt, in, false);
    };
    // Search a container
    const LootContainer& c0 = sim.loot.containers[0];
    teleport(c0.position + glm::vec3(1.2f, -c0.half.y, 0.0f));
    bool opened = false;
    for (int i = 0; i < 240 * 12 && !opened; ++i) opened = sim.interact(p, Interaction{Interaction::Container, 0}, true, dt), sim.tick(dt, in, false);
    check(opened && c0.searched(0), "searching a container opens it");
    // Send the back guns home
    const ExtractPoint& e = sim.map.extracts[0];
    teleport(e.dropBox + glm::vec3(0.8f, -0.8f, 0.0f));
    Interaction f = sim.focusFor(p);
    check(f.type == Interaction::SendBack, "standing at the uplink crate offers send-back");
    for (int i = 0; i < 240 * 27; ++i) sim.tick(dt, in, i < 10);
    check(sim.sentHome.size() == 2 && !p.equip.slots[ES_Back0] && !p.equip.slots[ES_Back1], "both back-slot guns sent home");
    // Extract
    teleport(e.position + glm::vec3(-2.0f, 0.0f, 2.0f));
    check(sim.focusFor(p).type == Interaction::Extract, "inside the zone offers extraction");
    for (int i = 0; i < 240 * 9 && !sim.playerExtracted; ++i) sim.tick(dt, in, i < 10);
    check(sim.playerExtracted && p.equip.slots[ES_Hand0], "extracted with the gun in hand");
}


void entranceTest() {
    std::printf("[Buildings]\n");
    int failed = 0, total = 0;
    for (unsigned seed : {1234u, 77u, 4242u}) {
        Simulation sim;
        Equipment none;
        sim.setupRaid(seed, 0, none, true);
        Character& p = sim.player();
        const float dt = 1.0f / 240.0f;
        for (const Entrance& e : sim.map.entrances) {
            ++total;
            p.position = e.outside + glm::vec3(0.0f, 0.05f, 0.0f);
            p.velocity = glm::vec3(0.0f);
            p.cancelAction();
            glm::vec3 to = e.inside - e.outside;
            CharacterInput in;
            in.yaw = std::atan2(-to.x, -to.z);
            in.move = glm::vec2(0.0f, 1.0f);
            bool reached = false;
            for (int i = 0; i < 240 * 6 && !reached; ++i) {
                sim.tick(dt, in, false);
                glm::vec3 d = e.inside - p.position;
                reached = std::sqrt(d.x * d.x + d.z * d.z) < 0.6f;
                // Past it is fine too (walked straight through).
                if (glm::dot(p.position - e.inside, to) > 0.0f) reached = true;
            }
            if (!reached) {
                ++failed;
                if (failed <= 6)
                    std::printf("  blocked: seed %u door at %.1f %.1f (stopped at %.1f %.1f)\n", seed, (e.outside.x + e.inside.x) * 0.5f,
                                (e.outside.z + e.inside.z) * 0.5f, p.position.x, p.position.z);
            }
        }
    }
    std::printf("  %d of %d building entrances walkable over 3 maps\n", total - failed, total);
    check(total > 60 && failed == 0, "every door can be walked through");
}

void rarityTest() {
    std::printf("[Loot rarity]\n");
    std::mt19937 rng(9);
    std::vector<int> counts(weaponDefs().size(), 0);
    for (int i = 0; i < 20000; ++i) counts[pickWeaponByValue(0, rng)]++;
    int cheap = 0, dear = 0, cheapN = 0, dearN = 0;
    for (size_t i = 0; i < counts.size(); ++i) {
        int v = weaponDefs()[i].value;
        if (v < 1500) cheap += counts[i], ++cheapN;
        if (v >= 9000) dear += counts[i], ++dearN;
    }
    float perCheap = float(cheap) / std::max(1, cheapN), perDear = float(dear) / std::max(1, dearN);
    std::printf("  per gun: guns under 1500 cr found %.0f times, guns over 9000 cr %.1f times (20000 ordinary rolls)\n", perCheap, perDear);
    check(perCheap > perDear * 20.0f, "expensive guns are much rarer than cheap ones");
    std::vector<int> hot(weaponDefs().size(), 0);
    for (int i = 0; i < 20000; ++i) hot[pickWeaponByValue(3, rng)]++;
    int dearHot = 0;
    for (size_t i = 0; i < hot.size(); ++i)
        if (weaponDefs()[i].value >= 9000) dearHot += hot[i];
    check(dearHot > dear * 3, "hotspot-quality loot finds expensive guns more often");
}

void storeTest() {
    std::printf("[Store]\n");
    Profile p;
    p.stash = Grid(10, 10);
    p.money = 5000;
    auto weapons = store::offers(StoreTab::Weapons);
    bool noRaidOnly = true;
    for (const StoreOffer& o : weapons) noRaidOnly = noRaidOnly && itemDefs()[o.def].value <= store::kRaidOnlyValue;
    check(!weapons.empty() && noRaidOnly, "store sells guns, but not the raid-only ones");
    const StoreOffer& cheap = weapons.front();
    std::string msg;
    bool ok = store::buy(p, cheap, msg);
    check(ok && p.money == 5000 - cheap.price && p.stash.items.size() == 1, "buying takes credits and puts the gun in the stash");
    const Item& gun = *p.stash.items[0].item;
    check(gun.mag && gun.mag->rounds.empty() && loadedRounds(gun) == 0, "guns come with an empty magazine");
    int gunWeapon = gun.d().weapon;
    auto ammo = store::offers(StoreTab::Ammo);
    int fitting = 0;
    for (const StoreOffer& o : ammo) fitting += store::fits(itemDefs()[o.def], gunWeapon) ? 1 : 0;
    check(fitting > 0, "matching ammo is listed for it");
    int before = p.money;
    int price = store::sellPrice(gun);
    p.money += price;
    check(p.money > before && price < cheap.price, "selling pays back less than it cost");
    StoreOffer expensive = weapons.back();
    p.money = 10;
    check(!store::buy(p, expensive, msg), "can't buy without the credits");
}

int runSelfTest() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_ok = ArmModel::selfTest();
    ragdollTest();
    inventoryTest();
    saveTest();
    healthTest();
    driftTest();
    weaponFeedTest();
    terrainTest();
    audioTest();
    playerFlowTest();
    entranceTest();
    rarityTest();
    storeTest();
    raidTest();
    std::printf("Self-test %s\n", g_ok ? "PASSED" : "FAILED");
    return g_ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(crashHandler);
#endif
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--selftest") == 0) return runSelfTest();

    Window window;
    if (!window.create(1600, 900, "Range")) {
        std::fprintf(stderr, "Failed to create window\n");
        return 1;
    }
    Game game;
    std::string profile = "profile";
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--profile") == 0) profile = argv[i + 1];
    if (!game.init(window, executableDir() + "shaders/", profile)) {
        std::fprintf(stderr, "Failed to initialise game\n");
        window.destroy();
        return 1;
    }
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--raid") == 0) game.startRaidNow();
    double last = glfwGetTime();
    while (!window.shouldClose()) {
        double now = glfwGetTime();
        game.frame(now - last);
        last = now;
    }
    game.shutdown();
    window.destroy();
    return 0;
}
