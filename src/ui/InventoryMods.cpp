// The weapon modding panel (part of InventoryUi): a large side view of the gun with its
// mount points, what is fitted where, and how the build changes the gun's physics.
#include "ui/InventoryUi.h"

#include "game/Attachments.h"
#include "game/Character.h"
#include "game/Simulation.h"
#include "game/WeaponDefs.h"
#include "items/Profile.h"
#include "render/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>

namespace {
const glm::vec4 kText(0.9f, 0.9f, 0.88f, 1.0f), kDim(0.62f, 0.64f, 0.63f, 1.0f), kAccent(1.0f, 0.85f, 0.35f, 1.0f),
    kGreen(0.45f, 0.9f, 0.45f, 1.0f), kRed(1.0f, 0.5f, 0.4f, 1.0f);

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

void frame(Hud& hud, float x, float y, float w, float h, const glm::vec4& c) {
    hud.rect(x, y, w, 1, c);
    hud.rect(x, y + h - 1, w, 1, c);
    hud.rect(x, y, 1, h, c);
    hud.rect(x + w - 1, y, 1, h, c);
}

// One shared display item per attachment, for drawing fitted parts as inventory tiles.
const Item& thumb(int att) {
    static std::map<int, ItemPtr> cache;
    ItemPtr& p = cache[att];
    if (!p) p = makeItem(attachmentItemDef(att));
    return *p;
}

struct Stat {
    const char* name;
    float base, now;
    const char* unit;
    bool lowerIsBetter;
};
} // namespace

ItemPtr InventoryUi::detachAttachment(Item& w, int slot) {
    if (slot < 0 || slot >= kAttachSlots || w.att[slot] < 0) return nullptr;
    int a = w.att[slot];
    w.att[slot] = -1;
    if (slot == AS_Offset || slot == AS_Magnifier) w.altSight = false;
    if (slot == AS_Stock) w.folded = false;
    if (!(w.weaponDef().light)) w.lightOn = false;
    return makeItem(attachmentItemDef(a));
}

bool InventoryUi::fitAttachment(Item& w, ItemPtr& att, ItemPtr& removed, std::string& why) {
    if (!att || att->d().cat != ItemCat::Attachment || w.d().cat != ItemCat::Weapon) {
        why = "That doesn't fit a weapon";
        return false;
    }
    int a = att->d().attach;
    int slot = attachmentDefs()[a].slot;
    if (!attachmentFits(weaponDefs()[w.d().weapon], a, &w, &why)) return false;
    removed = detachAttachment(w, slot);
    w.att[slot] = a;
    if (slot == AS_Stock && !w.weaponDef().foldable) w.folded = false;
    att.reset();
    say(std::string("Fitted ") + attachmentDefs()[a].name);
    return true;
}

void InventoryUi::drawMods(Hud& hud, Context& ctx, const UiInput& in, float x, float y, float W, float H) {
    Equipment* eqOut = nullptr;
    Item* wp = findAnywhere(ctx, m_modUid, &eqOut);
    if (!wp || wp->d().cat != ItemCat::Weapon) {
        m_modUid = 0;
        return;
    }
    Item& w = *wp;
    const float s = m_s;
    const WeaponDef& base = weaponDefs()[w.d().weapon];
    const WeaponDef& now = w.weaponDef();

    hud.rect(x, y, W, H, glm::vec4(0.05f, 0.055f, 0.06f, 0.96f));
    hud.text(x + 10 * s, y + 8 * s, "MODIFY  " + w.d().name, kAccent, 1.9f * s);
    {
        float bw = 90 * s, bh = 24 * s, bx = x + W - bw - 8 * s, by = y + 6 * s;
        bool hover = in.mouse.x >= bx && in.mouse.x < bx + bw && in.mouse.y >= by && in.mouse.y < by + bh;
        hud.rect(bx, by, bw, bh, hover ? glm::vec4(0.35f, 0.3f, 0.15f, 0.95f) : glm::vec4(0.2f, 0.2f, 0.2f, 0.95f));
        hud.text(bx + 18 * s, by + 5 * s, "Close", kText, 1.4f * s);
        if (hover && in.lmbPressed && !m_drag.item) {
            m_modUid = 0;
            return;
        }
    }

    // ---- side view
    float viewTop = y + 70 * s, viewH = std::min(H * 0.36f, 230 * s);
    float L = now.length + (now.wallBack > 0.0f ? 0.05f : 0.0f) + 0.08f;
    float back = base.cls == WeaponClass::Pistol && now.stockMount == SM_PistolAdapter && w.att[AS_Stock] >= 0 ? 0.30f : 0.02f;
    float sc = std::min((W - 60 * s) / (L + back), viewH / 0.32f);
    float ox = x + 30 * s + back * sc, oy = viewTop + viewH * 0.55f;
    auto toScreen = [&](const glm::vec3& p) { return glm::vec2(ox + (-p.z) * sc, oy - p.y * sc); };
    for (const Part& p : now.parts) {
        if (p.flags & Part::Hand) continue;
        if (w.optic > 0 && (p.flags & Part::RearSight)) continue;
        float px = ox + (-p.center.z - p.size.z * 0.5f) * sc, pw = std::max(1.0f, p.size.z * sc);
        float py = oy - (p.center.y + p.size.y * 0.5f) * sc, ph = std::max(1.0f, p.size.y * sc);
        if (std::abs(p.tiltX) > 0.15f) std::swap(pw, ph);
        glm::vec3 col = glm::min(p.color * 1.9f + glm::vec3(0.08f), glm::vec3(1.0f));
        hud.rect(px, py, pw, ph, glm::vec4(col, 0.95f));
    }
    std::vector<Part> optic;
    buildOpticParts(now, opticDefs()[w.optic], optic);
    for (const Part& p : optic) {
        float px = ox + (-p.center.z - p.size.z * 0.5f) * sc, pw = std::max(1.0f, p.size.z * sc);
        float py = oy - (p.center.y + p.size.y * 0.5f) * sc, ph = std::max(1.0f, p.size.y * sc);
        hud.rect(px, py, pw, ph, glm::vec4(0.25f, 0.27f, 0.32f, 0.95f));
    }

    // ---- slot boxes around the gun, each tied to its mount point
    struct SlotUi {
        int slot;
        glm::vec3 point;
        bool above;
        bool available;
    };
    float railZ = base.railZ;
    std::vector<SlotUi> slots = {
        {AS_Muzzle, base.muzzle, true, base.thread != Th_None},
        {AS_Offset, glm::vec3(0.0f, base.railTopY + 0.03f, railZ + 0.10f), true, base.offsetMount},
        {AS_Magnifier, glm::vec3(0.0f, base.railTopY + 0.03f, railZ + 0.10f), true, base.offsetMount},
        {AS_Side, base.sidePoint, true, base.sideRail},
        {AS_Under, base.underPoint, false, base.underRail},
        {AS_Grip, base.gripPoint, false, base.gripMount},
        {AS_Stock, glm::vec3(0.0f, -0.02f, base.stockFrontZ * 0.5f), false, base.stockMount != SM_None},
    };
    const float bw = 2.6f * m_cell, bh = 1.2f * m_cell;
    // Lay each row out left to right in mount order, never overlapping, kept inside the panel.
    std::vector<float> boxX(slots.size());
    for (int row = 0; row < 2; ++row) {
        std::vector<int> idx;
        for (int i = 0; i < int(slots.size()); ++i)
            if (slots[i].above == (row == 0)) idx.push_back(i);
        std::sort(idx.begin(), idx.end(), [&](int a, int c) { return toScreen(slots[a].point).x < toScreen(slots[c].point).x; });
        float cursor = x + 6 * s;
        for (int i : idx) {
            float want = toScreen(slots[i].point).x - bw * 0.5f;
            boxX[i] = std::max(want, cursor);
            cursor = boxX[i] + bw + 8 * s;
        }
        float over = cursor - 8 * s - (x + W - 6 * s);
        if (over > 0.0f)
            for (int i : idx) boxX[i] = std::max(x + 6 * s, boxX[i] - over);
    }
    int hoverSlot = -1;
    for (int si = 0; si < int(slots.size()); ++si) {
        const SlotUi& su = slots[si];
        glm::vec2 mp = toScreen(su.point);
        float bx = boxX[si];
        float by = su.above ? viewTop - 26 * s : viewTop + viewH + 6 * s;
        // leader line
        glm::vec2 a(bx + bw * 0.5f, su.above ? by + bh : by);
        float steps = 12.0f;
        for (int k = 0; k <= int(steps); ++k) {
            glm::vec2 p = glm::mix(a, mp, k / steps);
            hud.rect(p.x - 1, p.y - 1, 2, 2, glm::vec4(0.6f, 0.6f, 0.55f, su.available ? 0.6f : 0.2f));
        }
        Rect r{bx, by, bw, bh};
        bool hov = r.contains(in.mouse);
        glm::vec4 fill = !su.available ? glm::vec4(0.10f, 0.10f, 0.10f, 0.9f) : hov ? glm::vec4(0.22f, 0.24f, 0.22f, 0.95f) : glm::vec4(0.15f, 0.16f, 0.16f, 0.95f);
        bool canDrop = false;
        if (m_drag.item && m_drag.item->d().cat == ItemCat::Attachment) {
            int a2 = m_drag.item->d().attach;
            canDrop = su.available && attachmentDefs()[a2].slot == su.slot && attachmentFits(base, a2, &w);
            if (canDrop) fill = glm::vec4(0.18f, 0.32f, 0.18f, 0.95f);
        }
        hud.rect(r.x, r.y, r.w, r.h, fill);
        frame(hud, r.x, r.y, r.w, r.h, canDrop ? glm::vec4(0.5f, 0.95f, 0.5f, 1.0f) : glm::vec4(0.45f, 0.47f, 0.45f, 0.8f));
        hud.text(r.x + 3, r.y - 13 * s, attachSlotName(su.slot), su.available ? kDim : glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), 1.15f * s);
        if (!su.available) {
            hud.text(r.x + 6, r.y + bh * 0.3f, "n/a", glm::vec4(0.45f, 0.45f, 0.45f, 1.0f), 1.2f * s);
            continue;
        }
        Target t;
        t.type = Target::ModT;
        t.rect = r;
        t.slot = su.slot;
        t.mine = true;
        m_targets.push_back(t);
        if (w.att[su.slot] >= 0) {
            drawItem(hud, thumb(w.att[su.slot]), {r.x + 2, r.y + 2, r.w - 4, r.h - 4});
            if (hov) hoverSlot = su.slot;
        } else {
            hud.text(r.x + 6, r.y + bh * 0.3f, "empty", kDim, 1.2f * s);
        }
    }
    // Click a fitted attachment to take it off.
    if (hoverSlot >= 0 && in.lmbPressed && !m_drag.item && !m_menu.open) {
        ItemPtr off = detachAttachment(w, hoverSlot);
        if (off) {
            say("Removed " + off->d().name);
            if (!ctx.player->equip.stow(off)) {
                if (ctx.grid && ctx.grid->autoPlace(off)) {
                } else if (ctx.profile && !ctx.inRaid) ctx.profile->addToStash(std::move(off));
                else if (ctx.sim && ctx.inRaid) ctx.sim->dropFromCharacter(*ctx.player, std::move(off));
            }
        }
    }

    // ---- what the build does to the gun (base -> now, and a preview while dragging)
    const WeaponDef* preview = nullptr;
    std::shared_ptr<const WeaponDef> previewHold;
    if (m_drag.item && m_drag.item->d().cat == ItemCat::Attachment) {
        int a2 = m_drag.item->d().attach;
        if (attachmentFits(base, a2, &w)) {
            ItemPtr c = w.clone();
            c->att[attachmentDefs()[a2].slot] = a2;
            previewHold = buildEffectiveWeapon(*c);
            preview = previewHold.get();
        }
    }
    auto arm = [](const WeaponDef& d) { return d.armAccelScale; };
    Stat stats[] = {
        {"Mass", base.mass, now.mass, "kg", true},
        {"Length", base.length * 100.0f, now.length * 100.0f, "cm", true},
        {"Report", base.loudness, now.loudness, "dB", true},
        {"Muzzle rise", base.recoilRise, now.recoilRise, "", true},
        {"Kick into shoulder", base.recoilBack, now.recoilBack, "", true},
        {"Sideways kick", base.recoilYaw, now.recoilYaw, "", true},
        {"Sway", base.swayScale, now.swayScale, "", true},
        {"Time to aim", base.adsTime * 1000.0f, now.adsTime * 1000.0f, "ms", true},
        {"Swing (arm accel.)", arm(base), arm(now), "", false},
        {"Muzzle velocity", base.projectile.muzzleVelocity, now.projectile.muzzleVelocity, "m/s", false},
        {"Muzzle flash", base.flashScale, now.flashScale, "", true},
    };
    float sy = viewTop + viewH + bh + 34 * s;
    hud.text(x + 10 * s, sy, "BUILD", kAccent, 1.5f * s);
    hud.text(x + 230 * s, sy, "base", kDim, 1.3f * s);
    hud.text(x + 320 * s, sy, "now", kDim, 1.3f * s);
    if (preview) hud.text(x + 410 * s, sy, "with this", kGreen, 1.3f * s);
    sy += 20 * s;
    for (int i = 0; i < int(sizeof(stats) / sizeof(stats[0])); ++i) {
        const Stat& st = stats[i];
        auto shown = [&](float v) { return std::abs(v) >= 100.0f ? fmt("%.0f %s", v, st.unit) : fmt("%.2f %s", v, st.unit); };
        auto tint = [&](float v) {
            if (std::abs(v - st.base) < 1e-3f * std::max(1.0f, std::abs(st.base))) return kText;
            bool better = st.lowerIsBetter ? v < st.base : v > st.base;
            return better ? kGreen : kRed;
        };
        hud.text(x + 10 * s, sy, st.name, kDim, 1.3f * s);
        hud.text(x + 230 * s, sy, shown(st.base), kText, 1.3f * s);
        hud.text(x + 320 * s, sy, shown(st.now), tint(st.now), 1.3f * s);
        if (preview) {
            float pv[] = {preview->mass, preview->length * 100.0f, preview->loudness, preview->recoilRise, preview->recoilBack,
                          preview->recoilYaw, preview->swayScale, preview->adsTime * 1000.0f, arm(*preview),
                          preview->projectile.muzzleVelocity, preview->flashScale};
            hud.text(x + 410 * s, sy, shown(pv[i]), tint(pv[i]), 1.3f * s);
        }
        sy += 17 * s;
    }
    std::string notes;
    if (now.suppressed) notes += "Suppressed.  ";
    if (now.bipod) notes += "Bipod: rest it on cover while aiming.  ";
    if (now.light & 1) notes += "Light ";
    if (now.light & 2) notes += "Laser ";
    if (now.light) notes += "(L toggles).  ";
    if (now.offsetSight || now.magnifier) notes += "V switches sights.  ";
    if (now.foldable) notes += w.folded ? "Stock folded.  " : "Stock folds (right-click the gun).  ";
    sy += 6 * s;
    if (!notes.empty()) hud.text(x + 10 * s, sy, notes, kText, 1.25f * s);
    sy += 18 * s;
    hud.text(x + 10 * s, sy, fmt("Muzzle thread: %s   Stock: %s   Top rail: %s", threadName(base.thread), stockMountName(base.stockMount),
                                  base.opticMount <= 0 ? "none" : base.opticMount == 1 ? "pistol plate" : base.opticMount == 2 ? "short" : "long"),
             kDim, 1.2f * s);
    hud.text(x + 10 * s, sy + 16 * s, "Drag attachments onto a slot. Click a fitted one to take it off.", kDim, 1.2f * s);
}
