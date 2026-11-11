#include "ai/NavGrid.h"
#include "audio/Audio.h"
#include "core/Paths.h"
#include "core/Window.h"
#include "game/ArmModel.h"
#include "game/Attachments.h"
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
    check(weaponDefs().size() >= 60, "60+ weapons");
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
                        glm::vec3 wp = b->pathIndex < b->path.size() ? b->path[b->pathIndex] : glm::vec3(-1);
                        std::printf("    [%s] path %zu/%zu next %.1f %.1f %.1f dest %.1f %.1f walkable %d move %.2f %.2f speed %.2f grounded %d action %d loot %d/%d\n",
                                    b->debug.c_str(), b->pathIndex, b->path.size(), wp.x, wp.y, wp.z, b->destination.x, b->destination.z,
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
    int armorStops = 0;
    for (auto& c : sim.chars) armorStops += c->armorStops;
    std::printf("  tactics: %d flanks, %d pushes, %d withdrawals; %d of %d shots from cover; %d heals out of sight; %d bodies watched first\n",
                s.botFlanks, s.botPushes, s.botWithdrawals, s.botCoveredShots, s.botShots, s.botHealsHidden, s.botBodyWatches);
    std::printf("  gear: %d swaps, %d attachments fitted; %d armour stops (living raiders); %d bot-quarter-seconds upstairs; %d loot-zone visits\n",
                s.botGearSwaps, s.botAttachmentsFitted, armorStops, s.botUpstairs, s.botLootZone);
    check(s.botShots == 0 || s.botCoveredShots * 2 > s.botShots, "most bot shots come from cover");
    check(s.botLootZone > 0, "bots visit the loot zone");
    check(s.botsKilled > 0, "bots found and killed each other");
    check(s.botLoots > 0 && s.botWeaponsTaken > 0, "bots looted and took weapons");
    check(s.botExtracts + s.botSendBacks > 0, "bots extracted or sent loot home");
    check(s.botRespawns > 0, "replacements spawned");
    check(stuck <= 2, "no bot stuck for over a minute (2 allowed)");
    check(t < 900.0, "runs faster than real time");
}

// Two raiders meet in the open with some crates about: they should get behind them, not stand
// and trade shots in the field.
void duelTest() {
    std::printf("[Field duel]\n");
    Simulation sim;
    Equipment none;
    sim.setupRaid(1234u, 0, none, false);
    // Find a clear patch of open ground.
    glm::vec3 q(0.0f);
    bool found = false;
    std::vector<int> ov;
    for (int i = 0; i < 400 && !found; ++i) {
        float x = -300.0f + float(i % 20) * 30.0f, z = -300.0f + float(i / 20) * 30.0f;
        float h = sim.terrain.height(x, z);
        AABB box{glm::vec3(x - 40.0f, h - 3.0f, z - 22.0f), glm::vec3(x + 40.0f, h + 8.0f, z + 22.0f)};
        sim.world.overlap(box, ov);
        bool clear = true;
        for (int ci : ov) {
            const Collider& c = sim.world.collider(ci);
            if (!c.terrain && c.owner == kOwnerWorld) clear = false;
        }
        float h2 = sim.terrain.height(x - 25.0f, z), h3 = sim.terrain.height(x + 25.0f, z);
        if (clear && std::abs(h2 - h3) < 3.0f) {
            q = glm::vec3(x, h, z);
            found = true;
        }
    }
    check(found, "an open field for the duel");
    if (!found) return;
    glm::vec3 A = q + glm::vec3(-25.0f, 0, 0), B = q + glm::vec3(25.0f, 0, 0);
    auto crate = [&](glm::vec3 c, glm::vec3 half) {
        c.y = sim.terrain.height(c.x, c.z);
        Collider col;
        col.box = OBB::fromCenterSize(c + glm::vec3(0, half.y - 0.3f, 0), half * 2.0f);
        col.surface = Surface::Wood;
        sim.world.add(col);
    };
    for (const glm::vec3& base : {A, B}) {
        float s = base.x < q.x ? 1.0f : -1.0f;
        crate(base + glm::vec3(s * 8.0f, 0, 9.0f), glm::vec3(0.5f, 0.8f, 1.3f));
        crate(base + glm::vec3(s * 10.0f, 0, -8.0f), glm::vec3(0.5f, 0.8f, 1.3f));
        crate(base + glm::vec3(-s * 6.0f, 0, 11.0f), glm::vec3(1.3f, 0.8f, 0.5f));
    }
    sim.nav.build(sim.world, sim.map.halfSize);
    sim.nav.addLevels(sim.world, sim.map);
    int ids[2];
    for (int k = 0; k < 2; ++k) {
        ids[k] = sim.spawnBot(1, 0.0f);
        Character& c = *sim.chars[ids[k]];
        c.equip.clear();
        c.equip.slots[ES_Rig] = makeItem("rig_assault");
        c.equip.slots[ES_Hand0] = makeLoadedWeapon(kRifle);
        std::mt19937 r(3);
        addAmmoFor(*c.equip.slots[ES_Rig]->grid, kRifle, 3, 0, r);
        ItemPtr band = makeItem("med_bandage");
        c.equip.stow(band);
        glm::vec3 p = k == 0 ? A : B;
        p.y = sim.terrain.height(p.x, p.z) + 0.05f;
        c.spawn(sim.world, p, k == 0 ? -1.5708f : 1.5708f); // facing each other
        Bot* b = sim.bots.find(ids[k]);
        b->aggression = 0.5f;
        b->caution = 0.5f;
        b->skill = 0.5f;
    }
    sim.stats = RaidStats{};
    double cover[2] = {-1.0, -1.0}, seen[2] = {-1.0, -1.0};
    CharacterInput idle;
    const float dt = 1.0f / 240.0f;
    for (int i = 0; i < 240 * 30; ++i) {
        sim.tick(dt, idle, false);
        for (int k = 0; k < 2; ++k) {
            Bot* b = sim.bots.find(ids[k]);
            Character& c = *sim.chars[ids[k]];
            if (!b || !c.alive() || cover[k] >= 0.0) continue;
            if (seen[k] < 0.0)
                for (const Contact& x : b->contacts)
                    if (x.id == ids[1 - k] && x.visible) seen[k] = sim.time;
            if (seen[k] < 0.0) continue;
            if (b->hasCover && glm::length(b->cover - glm::vec2(c.position.x, c.position.z)) < 1.3f) cover[k] = sim.time;
        }
    }
    float worst = 0.0f;
    for (int k = 0; k < 2; ++k)
        if (Bot* b = sim.bots.find(ids[k])) worst = std::max(worst, b->maxExposed);
    const RaidStats& s = sim.stats;
    std::printf("  first saw the other at %.1f s / %.1f s\n", seen[0], seen[1]);
    std::printf("  reached cover after %.1f s / %.1f s; %d of %d shots from cover; longest exposed %.1f s; flanks %d, pushes %d, withdrawals %d\n",
                cover[0], cover[1], s.botCoveredShots, s.botShots, worst, s.botFlanks, s.botPushes, s.botWithdrawals);
    bool quick = seen[0] >= 0.0 || seen[1] >= 0.0;
    for (int k = 0; k < 2; ++k)
        if (seen[k] >= 0.0 && !(cover[k] >= 0.0 && cover[k] - seen[k] < 6.0)) quick = false;
    check(quick, "whoever sees the other takes cover within 6 s");
    check(s.botShots == 0 || s.botCoveredShots > s.botShots * 0.6, "over 60 % of shots fired from cover");
    check(worst < 4.0f, "nobody stands exposed for more than 4 s at a time");
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
                bool level = std::abs(d.y) < 0.6f; // stairs: got up there
                reached = std::sqrt(d.x * d.x + d.z * d.z) < 0.6f && level;
                // Past it is fine too (walked straight through).
                if (glm::dot(p.position - e.inside, to) > 0.0f && level) reached = true;
            }
            if (!reached) {
                ++failed;
                if (failed <= 6) {
                    std::printf("  blocked: seed %u from %.1f %.1f %.1f to %.1f %.1f %.1f (stopped at %.1f %.1f %.1f)\n", seed, e.outside.x, e.outside.y,
                                e.outside.z, e.inside.x, e.inside.y, e.inside.z, p.position.x, p.position.y, p.position.z);
                    AABB box{p.position + glm::vec3(-0.6f, 0.05f, -0.6f), p.position + glm::vec3(0.6f, 1.8f, 0.6f)};
                    std::vector<int> ov;
                    sim.world.overlap(box, ov);
                    for (int ci : ov) {
                        const Collider& c = sim.world.collider(ci);
                        if (!c.blocksPlayer || c.terrain) continue;
                        AABB b = c.box.bounds();
                        std::printf("    near [%.2f %.2f %.2f]-[%.2f %.2f %.2f]\n", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
                    }
                }
            }
        }
    }
    std::printf("  %d of %d building entrances walkable over 3 maps\n", total - failed, total);
    check(total > 60 && failed == 0, "every door can be walked through");
}

void levelsTest() {
    std::printf("[Upper floors & loot zone]\n");
    std::vector<std::string> zones;
    int floors = 0, reachedAll = 0, floorsTried = 0;
    for (unsigned seed : {1234u, 77u, 4242u, 9u, 31337u, 555u}) {
        MapData map;
        World world;
        Terrain terrain;
        std::vector<glm::vec3> colors;
        MapBuilder().build(seed, world, terrain, map, colors);
        world.setTerrain(&terrain);
        if (std::find(zones.begin(), zones.end(), map.lootZone.name) == zones.end()) zones.push_back(map.lootZone.name);
        if (seed != 1234u && seed != 77u) continue;
        NavGrid nav;
        nav.build(world, map.halfSize);
        nav.addLevels(world, map);
        floors += int(map.floors.size());
        // From a spawn point to the middle of every upper level.
        for (const NavFloor& f : map.floors) {
            ++floorsTried;
            glm::vec3 goal(0.0f);
            bool found = false;
            // A walkable point on that floor near its middle.
            for (float fx : {0.5f, 0.3f, 0.7f, 0.2f, 0.8f})
                for (float fz : {0.5f, 0.3f, 0.7f, 0.2f, 0.8f}) {
                    glm::vec3 q(f.min.x + (f.max.x - f.min.x) * fx, f.y, f.min.y + (f.max.y - f.min.y) * fz);
                    if (!found && nav.walkable3(q)) {
                        goal = q;
                        found = true;
                    }
                }
            std::vector<glm::vec3> path;
            const SpawnPoint& s = map.spawns[floorsTried % map.spawns.size()];
            if (found && nav.findPath3(s.position, goal, path) && !path.empty() && std::abs(path.back().y - f.y) < 0.3f) ++reachedAll;
            else std::printf("  no path to the level at %.1f %.1f (y %.1f)%s\n", (f.min.x + f.max.x) * 0.5f, (f.min.y + f.max.y) * 0.5f, f.y, found ? "" : " - nothing walkable");
        }
    }
    std::printf("  %d upper levels on 2 maps, %d/%d reachable; loot zones seen:", floors, reachedAll, floorsTried);
    for (const std::string& z : zones) std::printf(" %s;", z.c_str());
    std::printf("\n");
    check(floors >= 16 && reachedAll == floorsTried, "every upper floor, walkway and roof is reachable by stairs");
    check(zones.size() >= 3, "the loot zone moves between areas from raid to raid");

    // Loot-zone containers carry more.
    std::mt19937 rng(5);
    double plain = 0.0, zone = 0.0;
    int dear = 0;
    const ContainerType kinds[] = {ContainerType::WeaponCrate, ContainerType::Duffel, ContainerType::AmmoBox, ContainerType::Toolbox};
    for (int i = 0; i < 2000; ++i) {
        LootContainer a, b;
        a.type = b.type = kinds[i % 4];
        a.tier = 0;
        b.tier = 1;
        b.lootZone = true;
        fillContainer(a, rng);
        fillContainer(b, rng);
        for (auto& e : a.grid.items) plain += e.item->totalValue();
        for (auto& e : b.grid.items) {
            zone += e.item->totalValue();
            if (e.item->d().cat == ItemCat::Weapon && weaponDefs()[e.item->d().weapon].value >= 14000) ++dear;
        }
    }
    std::printf("  mean container value: ordinary %.0f cr, loot zone %.0f cr; 14k+ guns in 2000 zone containers: %d\n", plain / 2000, zone / 2000, dear);
    check(zone > plain * 2.0 && dear > 0 && dear < 60, "loot zone: clearly better, the dearest guns still rare");
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

void botKitTest() {
    std::printf("[Raider kits]\n");
    std::mt19937 rng(11);
    int count[4] = {};
    for (int i = 0; i < 10000; ++i) ++count[rollBotTier(rng).tier];
    std::printf("  tiers: %.1f%% %.1f%% %.1f%% %.1f%%\n", count[0] / 100.0, count[1] / 100.0, count[2] / 100.0, count[3] / 100.0);
    check(std::abs(count[0] - 3500) < 200 && std::abs(count[1] - 4000) < 200 && std::abs(count[2] - 1500) < 200 && std::abs(count[3] - 1000) < 200,
          "35 / 40 / 15 / 10 split");
    double mean[4] = {};
    bool pistolOnly = true, dearRare = true;
    int dear = 0;
    for (int t = 0; t < 4; ++t) {
        for (int i = 0; i < 400; ++i) {
            Equipment eq;
            BotKitRoll k{t, t == 3 ? float(i % 20) / 19.0f * float(i % 20) / 19.0f : 0.0f};
            makeLoadout(eq, t, rng, k.quality);
            mean[t] += eq.totalValue() / 400.0;
            if (t == 0)
                for (int s : {ES_Hand0, ES_Hand1, ES_Back0, ES_Back1})
                    if (eq.slots[s] && weaponDefs()[eq.slots[s]->d().weapon].cls != WeaponClass::Pistol) pistolOnly = false;
            if (t == 3 && eq.slots[ES_Hand0] && weaponDefs()[eq.slots[ES_Hand0]->d().weapon].value >= 14000) ++dear;
        }
    }
    dearRare = dear < 400 / 4;
    std::printf("  mean kit value: %.0f / %.0f / %.0f / %.0f cr, tier-3 kits with a 14k+ gun: %d / 400\n", mean[0], mean[1], mean[2], mean[3], dear);
    check(pistolOnly, "barebones raiders carry only a pistol");
    check(mean[0] < mean[1] && mean[1] < mean[2] && mean[2] < mean[3], "kit value rises with tier");
    check(dearRare, "the dearest guns stay rare even in the best kits");
}

void armorTest() {
    std::printf("[Armour]\n");
    auto def = [](const char* id) { return itemDefs()[findItemDef(id)]; };
    auto energy = [](const char* ammo, float v) {
        const AmmoDef& a = ammoDefs()[findAmmo(ammo)];
        return 0.5f * a.mass * v * v;
    };
    const AmmoDef &nine = ammoDefs()[findAmmo("9_fmj")], &x39 = ammoDefs()[findAmmo("762x39_fmj")], &m855 = ammoDefs()[findAmmo("556_fmj")],
                  &ap = ammoDefs()[findAmmo("556_ap")];
    ItemDef soft = def("arm_soft"), heavy = def("arm_heavy"), assault = def("arm_assault");
    float e9 = energy("9_fmj", 360.0f), e39 = energy("762x39_fmj", 715.0f), e556 = energy("556_fmj", 910.0f), eap = energy("556_ap", 880.0f);
    check(armorStopsBullet(soft, soft.durability, nine, e9) && !armorStopsBullet(soft, soft.durability, x39, e39), "class 1 stops 9 mm but not 7.62x39");
    check(armorStopsBullet(heavy, heavy.durability, m855, e556) && armorStopsBullet(heavy, heavy.durability, ap, eap), "class 4 stops 5.56 FMJ and AP");
    check(armorStopsBullet(assault, assault.durability, m855, e556) && !armorStopsBullet(assault, assault.durability * 0.1f, m855, e556),
          "worn-out class 3 lets 5.56 through");
    check(!armorStopsBullet(assault, assault.durability, ap, eap), "5.56 AP defeats class 3");
}

void attachmentTest() {
    std::printf("[Attachments]\n");
    const auto& atts = attachmentDefs();
    std::printf("  %d attachments, %d optics\n", int(atts.size()), int(opticDefs().size()));
    check(atts.size() >= 44, "44+ attachments");
    const WeaponDef& rifle = weaponDefs()[kRifle];
    int supp = findAttachment("sup_r556"), multi = findAttachment("sup_r30"), brake = findAttachment("brk_r556");
    check(supp >= 0 && attachmentFits(rifle, supp), "5.56 suppressor fits the carbine");
    check(!attachmentFits(weaponDefs()[findWeapon("talon308")], supp), "...but not a .308 thread");
    WeaponDef blk = rifle;
    blk.caliber = Cal_300BLK;
    blk.bulletDiameter = 0.0078f;
    check(!attachmentFits(blk, supp) && attachmentFits(blk, multi), "a 5.56 bore can't take .300 BLK; the 30-cal can can");
    check(!attachmentFits(weaponDefs()[kPistol], findAttachment("vg")) && attachmentFits(weaponDefs()[kPistol], findAttachment("pl_light")),
          "pistols take pistol lights, not foregrips");
    check(opticFits(weaponDefs()[kPistol], 1) && !opticFits(weaponDefs()[kPistol], 4) && opticFits(rifle, 8), "optic rails: dot on a pistol, LPVO on a carbine, no 8x on a pistol");

    // Suppressor: quieter, longer muzzle, slower to swing.
    ItemPtr plain = makeLoadedWeapon(kRifle), can = makeLoadedWeapon(kRifle);
    can->att[AS_Muzzle] = supp;
    const WeaponDef &a = plain->weaponDef(), &b = can->weaponDef();
    std::printf("  suppressed: %.0f -> %.0f dB, muzzle %.2f -> %.2f m, mass %.2f -> %.2f kg, arm accel x%.2f\n", a.loudness, b.loudness,
                -a.muzzle.z, -b.muzzle.z, a.mass, b.mass, b.armAccelScale / a.armAccelScale);
    check(b.suppressed && a.loudness - b.loudness > 20.0f && b.muzzle.z < a.muzzle.z - 0.15f && b.armAccelScale < a.armAccelScale,
          "suppressor: quieter, longer, slower to swing");

    World world = groundWorld();
    auto fireOnce = [&](ItemPtr gun, float& riseDeg, float& db) {
        Equipment eq;
        eq.slots[ES_Hand0] = std::move(gun);
        Weapon w;
        WeaponContext ctx;
        ctx.eye = glm::vec3(0, 1.66f, 0);
        ctx.aimPoint = glm::vec3(0, 1.66f, -100.0f);
        w.init(ctx, &eq, 0, true);
        WeaponInput in;
        in.adsHeld = true;
        Ballistics bl;
        std::vector<Impact> imp;
        std::vector<WeaponEvent> ev;
        for (int i = 0; i < 240; ++i) w.update(in, ctx, 1.0f / 240.0f, world, bl, imp, ev);
        float p0 = std::asin(w.pointing().y), peak = 0.0f;
        in.firePressed = true;
        for (int i = 0; i < 72; ++i) {
            w.update(in, ctx, 1.0f / 240.0f, world, bl, imp, ev);
            in.firePressed = false;
            peak = std::max(peak, std::asin(w.pointing().y) - p0);
        }
        riseDeg = glm::degrees(peak);
        db = 0.0f;
        for (const WeaponEvent& e : ev)
            if (e.type == WeaponEvent::Shot) db = e.loudness;
    };
    float rPlain, rBrake, rCan, dPlain, dBrake, dCan;
    ItemPtr braked = makeLoadedWeapon(kRifle);
    braked->att[AS_Muzzle] = brake;
    fireOnce(makeLoadedWeapon(kRifle), rPlain, dPlain);
    fireOnce(std::move(braked), rBrake, dBrake);
    ItemPtr can2 = makeLoadedWeapon(kRifle);
    can2->att[AS_Muzzle] = supp;
    fireOnce(std::move(can2), rCan, dCan);
    std::printf("  muzzle rise after a shot: plain %.2f deg, brake %.2f deg, suppressor %.2f deg; report %.0f / %.0f / %.0f dB\n", rPlain, rBrake, rCan,
                dPlain, dBrake, dCan);
    check(rBrake < rPlain * 0.95f && dBrake > dPlain && dCan < dPlain - 20.0f, "brake: less rise, louder; suppressed shot is quieter");

    // Bipod: rest the legs on a crate in front, aim, and the hold steadies.
    auto hold = [&](bool crate, bool& deployed) {
        World wd = groundWorld();
        if (crate) {
            Collider c;
            c.box = OBB::fromCenterSize(glm::vec3(0, 1.0f, -1.0f), glm::vec3(1.0f, 0.62f, 1.2f)); // top at 1.31 m
            wd.add(c);
        }
        Equipment eq;
        ItemPtr g = makeLoadedWeapon(kRifle);
        g->att[AS_Under] = findAttachment("bp_light");
        eq.slots[ES_Hand0] = std::move(g);
        Weapon w;
        WeaponContext ctx;
        ctx.eye = glm::vec3(0, 1.62f, 0);
        ctx.aimPoint = glm::vec3(0, 1.62f, -100.0f);
        ctx.crouching = true;
        w.init(ctx, &eq, 0, true);
        WeaponInput in;
        in.adsHeld = true;
        Ballistics bl;
        std::vector<Impact> imp;
        std::vector<WeaponEvent> ev;
        double sum = 0.0;
        int n = 0;
        deployed = false;
        for (int i = 0; i < 240 * 12; ++i) {
            w.update(in, ctx, 1.0f / 240.0f, wd, bl, imp, ev);
            deployed |= w.bipodDeployed();
            if (i > 240 * 3) {
                sum += glm::dot(w.drift(), w.drift());
                ++n;
            }
        }
        return float(std::sqrt(sum / n));
    };
    bool dep0, dep1;
    float free = hold(false, dep0), rested = hold(true, dep1);
    std::printf("  bipod: drift %.3f deg free, %.3f deg rested (deployed %d/%d)\n", glm::degrees(free), glm::degrees(rested), int(dep0), int(dep1));
    check(!dep0 && dep1 && rested < free * 0.5f, "bipod deploys on a crate and steadies the hold");

    // Save: attachments, folded stock and armour condition survive; old lines still load.
    ItemPtr kit = makeLoadedWeapon(findWeapon("volk762"), 1);
    kit->att[AS_Muzzle] = findAttachment("sup_r762");
    kit->att[AS_Side] = findAttachment("sl_combo");
    kit->folded = true;
    std::stringstream ss;
    writeItem(ss, *kit);
    ItemPtr back = readItem(ss);
    check(back && back->att[AS_Muzzle] == kit->att[AS_Muzzle] && back->att[AS_Side] == kit->att[AS_Side] && back->folded,
          "attachments survive a save");
    ItemPtr vest = makeItem("arm_assault");
    vest->condition = 31.5f;
    std::stringstream sv;
    writeItem(sv, *vest);
    ItemPtr v2 = readItem(sv);
    check(v2 && std::abs(v2->condition - 31.5f) < 0.01f, "armour condition survives a save");
    std::stringstream old("{ w_ridgeline556 1 556_fmj 1 1 100 0 1 0 0 N N }");
    ItemPtr o = readItem(old);
    check(o && o->d().weapon == findWeapon("ridgeline556") && o->att[AS_Muzzle] < 0, "an old-format item line still loads");
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
    attachmentTest();
    armorTest();
    botKitTest();
    terrainTest();
    audioTest();
    playerFlowTest();
    entranceTest();
    levelsTest();
    duelTest();
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
