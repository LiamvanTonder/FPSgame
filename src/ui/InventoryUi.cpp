#include "ui/InventoryUi.h"

#include "game/Character.h"
#include "game/Simulation.h"
#include "game/WeaponDefs.h"
#include "game/Attachments.h"
#include "items/Profile.h"
#include "render/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {
const glm::vec4 kPanel(0.06f, 0.07f, 0.07f, 0.88f);
const glm::vec4 kCellA(0.16f, 0.17f, 0.17f, 0.9f), kCellB(0.13f, 0.14f, 0.14f, 0.9f);
const glm::vec4 kText(0.9f, 0.9f, 0.88f, 1.0f), kDim(0.65f, 0.67f, 0.66f, 1.0f), kAccent(1.0f, 0.85f, 0.35f, 1.0f);
const glm::vec4 kGood(0.4f, 0.9f, 0.4f, 0.35f), kBad(0.9f, 0.3f, 0.25f, 0.35f);

glm::vec4 catColor(ItemCat c) {
    switch (c) {
    case ItemCat::Weapon: return {0.24f, 0.25f, 0.27f, 0.95f};
    case ItemCat::Magazine: return {0.22f, 0.26f, 0.22f, 0.95f};
    case ItemCat::Ammo: return {0.36f, 0.30f, 0.18f, 0.95f};
    case ItemCat::Med: return {0.40f, 0.16f, 0.15f, 0.95f};
    case ItemCat::Optic: return {0.15f, 0.17f, 0.24f, 0.95f};
    case ItemCat::Rig: return {0.25f, 0.29f, 0.19f, 0.95f};
    case ItemCat::Backpack: return {0.30f, 0.24f, 0.17f, 0.95f};
    case ItemCat::Attachment: return {0.17f, 0.20f, 0.26f, 0.95f};
    case ItemCat::Armor: case ItemCat::Helmet: return {0.24f, 0.27f, 0.25f, 0.95f};
    }
    return {0.3f, 0.3f, 0.3f, 0.95f};
}

void frame(Hud& hud, float x, float y, float w, float h, const glm::vec4& c, float t = 1.0f) {
    hud.rect(x, y, w, t, c);
    hud.rect(x, y + h - t, w, t, c);
    hud.rect(x, y, t, h, c);
    hud.rect(x + w - t, y, t, h, c);
}

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof(buf), f, a);
    va_end(a);
    return buf;
}

int ammoFor(const Item& mag) {
    return mag.rounds.empty() ? -1 : mag.rounds.back();
}

bool canLoad(const Item& mag, const Item& ammo) {
    const ItemDef& m = mag.d();
    const ItemDef& a = ammo.d();
    if (m.cat != ItemCat::Magazine || a.cat != ItemCat::Ammo) return false;
    if (m.caliber >= 0 && m.caliber != a.caliber) return false;
    if (m.caliber < 0 && a.caliber != Cal_556 && a.caliber != Cal_300BLK) return false;
    if (!mag.rounds.empty() && ammoDefs()[mag.rounds.back()].caliber != a.caliber) return false;
    return int(mag.rounds.size()) < m.capacity;
}
} // namespace

void InventoryUi::reset() {
    cancelDrag();
    m_menu.open = false;
    m_stashScroll = 0.0f;
}

void InventoryUi::cancelDrag() {
    if (m_drag.item) returnDrag();
}

void InventoryUi::drawItem(Hud& hud, const Item& it, Rect r, float alpha) {
    const ItemDef& d = it.d();
    glm::vec4 c = catColor(d.cat);
    c.a *= alpha;
    hud.rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, c);
    frame(hud, r.x + 1, r.y + 1, r.w - 2, r.h - 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.6f * alpha));
    if (d.cat == ItemCat::Weapon) {
        // Side-view silhouette from the weapon's parts.
        const WeaponDef& w = it.weaponDef();
        float L = w.length + (w.wallBack > 0.0f ? 0.0f : 0.02f);
        float sx = (r.w - 10) / std::max(L, 0.2f);
        float sc = std::min(sx, (r.h - 16) / 0.25f);
        float ox = r.x + 5 + (r.w - 10 - L * sc) * 0.5f, oy = r.y + r.h * 0.5f;
        for (const Part& p : w.parts) {
            if (p.flags & Part::Hand) continue;
            float px = ox + (-p.center.z - p.size.z * 0.5f) * sc, pw = std::max(1.0f, p.size.z * sc);
            float py = oy - (p.center.y + p.size.y * 0.5f) * sc, ph = std::max(1.0f, p.size.y * sc);
            if (std::abs(p.tiltX) > 0.15f) std::swap(pw, ph), px += (pw - ph) * 0.0f;
            glm::vec3 col = glm::min(p.color * 1.8f + glm::vec3(0.08f), glm::vec3(1.0f));
            hud.rect(px, py, pw, ph, glm::vec4(col, 0.95f * alpha));
        }
        if (it.optic > 0) hud.rect(ox + (-w.railZ - 0.06f) * sc, oy - (w.railTopY + 0.05f) * sc, 0.12f * sc, 0.03f * sc, glm::vec4(0.1f, 0.1f, 0.12f, alpha));
    }
    std::string label = it.label();
    float scale = (d.cat == ItemCat::Weapon ? 1.2f : 1.0f) * m_s;
    hud.text(r.x + 3, r.y + 2, label, glm::vec4(kText.r, kText.g, kText.b, alpha), scale);
    if (d.cat == ItemCat::Med && d.hpPool > 0.0f) {
        float f = it.pool / d.hpPool;
        hud.rect(r.x + 3, r.y + r.h - 6, (r.w - 6) * f, 3, glm::vec4(0.4f, 0.9f, 0.4f, alpha));
    }
    if ((d.cat == ItemCat::Armor || d.cat == ItemCat::Helmet) && d.durability > 0.0f) {
        float f = std::clamp(it.condition / d.durability, 0.0f, 1.0f);
        hud.rect(r.x + 3, r.y + r.h - 6, (r.w - 6) * f, 3, glm::vec4(glm::mix(glm::vec3(0.9f, 0.3f, 0.2f), glm::vec3(0.4f, 0.8f, 0.9f), f), alpha));
    }
}

void InventoryUi::drawGrid(Hud& hud, Grid& g, float x, float y, const std::string& title, bool mine, float maxRows, float scroll) {
    float c = m_cell;
    hud.text(x, y - 16 * m_s, title, kDim, 1.4f * m_s);
    int first = int(scroll);
    int rows = std::min(g.h - first, int(maxRows));
    for (int yy = 0; yy < rows; ++yy)
        for (int xx = 0; xx < g.w; ++xx)
            hud.rect(x + xx * c, y + yy * c, c - 1, c - 1, ((xx + yy + first) & 1) ? kCellA : kCellB);
    Target t;
    t.type = Target::GridT;
    t.rect = {x, y - first * c, g.w * c, g.h * c};
    t.grid = &g;
    t.cell = c;
    t.mine = mine;
    // Only the visible part accepts drops.
    Rect visible{x, y, g.w * c, rows * c};
    m_targets.push_back(t);
    m_targets.back().rect = t.rect;
    for (Grid::Entry& e : g.items) {
        int w = e.item->width(e.rot), h = e.item->height(e.rot);
        if (e.y + h <= first || e.y >= first + rows) continue;
        Rect r{x + e.x * c, y + (e.y - first) * c, w * c, h * c};
        // Clip to the visible rows (stash scrolling)
        float top = std::max(r.y, visible.y), bottom = std::min(r.y + r.h, visible.y + visible.h);
        if (bottom <= top) continue;
        Rect drawn{r.x, top, r.w, bottom - top};
        drawItem(hud, *e.item, drawn);
        m_items.push_back({drawn, e.item.get(), Src::Grid, &g, nullptr, -1, mine});
    }
    frame(hud, x - 1, y - 1, g.w * c + 1, rows * c + 1, glm::vec4(0.4f, 0.42f, 0.4f, 0.8f));
}

void InventoryUi::drawSlot(Hud& hud, Equipment& eq, int slot, Rect r, bool mine) {
    hud.text(r.x, r.y - 15 * m_s, equipSlotName(slot), kDim, 1.3f * m_s);
    hud.rect(r.x, r.y, r.w, r.h, kCellB);
    frame(hud, r.x, r.y, r.w, r.h, glm::vec4(0.45f, 0.47f, 0.45f, 0.8f));
    Target t;
    t.type = Target::SlotT;
    t.rect = r;
    t.eq = &eq;
    t.slot = slot;
    t.mine = mine;
    m_targets.push_back(t);
    if (eq.slots[slot]) {
        drawItem(hud, *eq.slots[slot], r);
        m_items.push_back({r, eq.slots[slot].get(), Src::Slot, nullptr, &eq, slot, mine});
    }
}

void InventoryUi::drawEquipment(Hud& hud, Equipment& eq, float x, float y, bool mine, bool compact) {
    float c = m_cell;
    float gap = 22 * m_s;
    Rect wr{x, y, 5 * c, 2 * c};
    // Weapon slots
    for (int s : {ES_Hand0, ES_Hand1, ES_Back0, ES_Back1}) {
        Rect r = wr;
        if (compact) {
            int k = s - ES_Hand0;
            r.x = x + (k % 2) * (5 * c + 10 * m_s);
            r.y = y + (k / 2) * (2 * c + gap);
        } else {
            r.y = y + (s - ES_Hand0) * (2 * c + gap);
        }
        drawSlot(hud, eq, s, r, mine);
    }
    float gy = compact ? y + 2 * (2 * c + gap) : y + 4 * (2 * c + gap);
    drawSlot(hud, eq, ES_Rig, {x, gy, 2.5f * c, 2 * c}, mine);
    drawSlot(hud, eq, ES_Backpack, {x + 2.5f * c + 10 * m_s, gy, 2.5f * c, 2 * c}, mine);
    float ay = gy + 2 * c + gap;
    drawSlot(hud, eq, ES_Armor, {x, ay, 2.5f * c, 2 * c}, mine);
    drawSlot(hud, eq, ES_Helmet, {x + 2.5f * c + 10 * m_s, ay, 2.5f * c, 2 * c}, mine);
    // Grids
    float gx = compact ? x : x + 5 * c + 30 * m_s;
    float yy = compact ? ay + 2 * c + gap + 4 : y;
    if (eq.slots[ES_Rig] && eq.slots[ES_Rig]->grid) {
        drawGrid(hud, *eq.slots[ES_Rig]->grid, gx, yy, "Rig", mine);
        yy += eq.slots[ES_Rig]->grid->h * c + gap + 4;
    }
    drawGrid(hud, eq.pockets, gx, yy, "Pockets", mine);
    yy += eq.pockets.h * c + gap + 4;
    if (eq.slots[ES_Backpack] && eq.slots[ES_Backpack]->grid) drawGrid(hud, *eq.slots[ES_Backpack]->grid, gx, yy, "Backpack", mine);
}

void InventoryUi::drawHealth(Hud& hud, Character& ch, float x, float y) {
    const Health& h = ch.health;
    float s = m_s;
    hud.text(x, y - 16 * s, fmt("Health %.0f / %.0f", h.totalHp(), h.totalMax()), kDim, 1.4f * s);
    struct Box {
        int zone;
        float x, y, w, h;
    };
    const Box boxes[] = {{Z_Head, 70, 0, 60, 50},       {Z_Thorax, 55, 56, 90, 80},  {Z_Stomach, 60, 142, 80, 55},
                         {Z_LArm, 0, 56, 50, 120},      {Z_RArm, 150, 56, 50, 120},  {Z_LLeg, 50, 203, 47, 140},
                         {Z_RLeg, 103, 203, 47, 140}};
    for (const Box& b : boxes) {
        const ZoneState& z = h.zone(b.zone);
        float f = z.max > 0.0f ? z.hp / z.max : 0.0f;
        glm::vec4 col = z.destroyed() ? glm::vec4(0.08f, 0.08f, 0.08f, 0.95f)
                                      : glm::vec4(glm::mix(glm::vec3(0.6f, 0.12f, 0.1f), glm::vec3(0.25f, 0.5f, 0.25f), f), 0.9f);
        Rect r{x + b.x * s, y + b.y * s, b.w * s, b.h * s};
        hud.rect(r.x, r.y, r.w, r.h, col);
        frame(hud, r.x, r.y, r.w, r.h, glm::vec4(0, 0, 0, 0.8f));
        hud.text(r.x + 3, r.y + 3, fmt("%.0f", z.hp), kText, 1.3f * s);
        std::string cond;
        if (z.heavyBleed) cond += "HB ";
        else if (z.lightBleed) cond += "B ";
        if (z.fracture) cond += "F";
        if (!cond.empty()) hud.text(r.x + 3, r.y + 18 * s, cond, glm::vec4(1.0f, 0.75f, 0.3f, 1.0f), 1.3f * s);
        Target t;
        t.type = Target::ZoneT;
        t.rect = r;
        t.zone = b.zone;
        t.mine = true;
        m_targets.push_back(t);
    }
    float ty = y + 350 * s;
    hud.text(x, ty, "B bleeding  HB heavy bleeding  F fracture", kDim, 1.2f * s);
    hud.text(x, ty + 16 * s, "Drop a med on a body part to treat it", kDim, 1.2f * s);
    if (h.painkillerTime() > 0.0f) hud.text(x, ty + 32 * s, fmt("Painkillers: %.0f s", h.painkillerTime()), kAccent, 1.2f * s);
    else if (h.pain() > 0.2f) hud.text(x, ty + 32 * s, "In pain: aim shaky", glm::vec4(1.0f, 0.6f, 0.4f, 1.0f), 1.2f * s);
    if (ch.action() != Character::Action::None) {
        hud.text(x, ty + 50 * s, ch.actionLabel(), kAccent, 1.3f * s);
        hud.rect(x, ty + 68 * s, 200 * s, 6 * s, glm::vec4(0, 0, 0, 0.6f));
        hud.rect(x, ty + 68 * s, 200 * s * ch.actionProgress(), 6 * s, glm::vec4(1.0f, 0.85f, 0.35f, 0.9f));
    }
}

void InventoryUi::drawTooltip(Hud& hud, const Item& it, glm::vec2 at, float W, float H) {
    const ItemDef& d = it.d();
    std::vector<std::string> lines;
    lines.push_back(d.name);
    lines.push_back(std::string(categoryName(d.cat)) + fmt("   value %d", it.totalValue()));
    switch (d.cat) {
    case ItemCat::Weapon: {
        const WeaponDef& w = it.weaponDef();
        lines.push_back(fmt("%s   %.0f m/s   %s", caliberName(w.caliber), w.projectile.muzzleVelocity,
                            w.canAuto ? "semi / auto" : w.action == ActionType::Bolt ? "bolt action" : w.action == ActionType::Pump ? "pump action" : "semi-auto"));
        lines.push_back(fmt("Loaded %d / %d   optic: %s", loadedRounds(it), loadedCapacity(it), opticDefs()[it.optic].name.c_str()));
        if (w.reload == ReloadType::Magazine) {
            std::string mags = "Magazines:";
            for (const ItemDef& m : itemDefs())
                if (m.cat == ItemCat::Magazine && magazineFits(m, d.weapon)) mags += " " + std::to_string(m.capacity);
            lines.push_back(mags);
        } else if (w.reload == ReloadType::Tube) {
            lines.push_back(fmt("Loads round by round (%d)", w.magazine));
        } else {
            lines.push_back("Single shot: a round is hand-fed after every shot");
        }
        if (w.suppressed) lines.push_back("Suppressed: quiet, little flash");
        lines.push_back(fmt("%.1f kg   %.0f cm   %.0f dB", w.mass, w.length * 100.0f, w.loudness));
        {
            std::string a;
            for (int s = 0; s < kAttachSlots; ++s)
                if (it.att[s] >= 0) a += (a.empty() ? "" : ", ") + attachmentDefs()[it.att[s]].shortName;
            if (!a.empty()) lines.push_back("Fitted: " + a);
        }
        lines.push_back("Right-click > Modify to fit attachments");
        break;
    }
    case ItemCat::Attachment: {
        const AttachmentDef& a = attachmentDefs()[d.attach];
        lines.push_back(fmt("%s, %s   %.2f kg", attachSlotName(a.slot), attachSpecialName(a.special), a.mass));
        if (a.slot == AS_Muzzle) lines.push_back(std::string("Thread: ") + threadName(a.mount) + (a.length > 0.0f ? fmt("   +%.0f cm", a.length * 100.0f) : ""));
        if (a.slot == AS_Stock) lines.push_back(std::string("Mount: ") + stockMountName(a.mount));
        std::string fx;
        if (a.loudnessDb != 0.0f) fx += fmt("%+.0f dB  ", a.loudnessDb);
        if (a.riseMul != 1.0f) fx += fmt("rise %+.0f%%  ", (a.riseMul - 1.0f) * 100.0f);
        if (a.backMul != 1.0f) fx += fmt("kick %+.0f%%  ", (a.backMul - 1.0f) * 100.0f);
        if (a.yawMul != 1.0f) fx += fmt("side %+.0f%%  ", (a.yawMul - 1.0f) * 100.0f);
        if (a.swayMul != 1.0f) fx += fmt("sway %+.0f%%  ", (a.swayMul - 1.0f) * 100.0f);
        if (a.adsMul != 1.0f) fx += fmt("aim time %+.0f%%  ", (a.adsMul - 1.0f) * 100.0f);
        if (a.flashMul != 1.0f) fx += fmt("flash %+.0f%%  ", (a.flashMul - 1.0f) * 100.0f);
        if (a.spreadMul != 1.0f) fx += fmt("spread %+.0f%%", (a.spreadMul - 1.0f) * 100.0f);
        if (!fx.empty()) lines.push_back(fx);
        lines.push_back("Drag onto a weapon (or its Modify panel) to fit");
        break;
    }
    case ItemCat::Armor: case ItemCat::Helmet:
        lines.push_back(fmt("Class %d   condition %.0f / %.0f   %.1f kg", d.armorClass, it.condition, d.durability, d.mass));
        lines.push_back(std::string("Covers: ") + ((d.coverage & AC_Head) ? "head (not the face) " : "") + ((d.coverage & AC_Thorax) ? "chest " : "") +
                        ((d.coverage & AC_Stomach) ? "stomach" : ""));
        break;
    case ItemCat::Magazine:
        lines.push_back(fmt("%d / %d rounds", int(it.rounds.size()), d.capacity));
        if (!it.rounds.empty()) lines.push_back("Top round: " + ammoDefs()[it.rounds.back()].name);
        lines.push_back("Drag ammo onto it to load");
        break;
    case ItemCat::Ammo: {
        const AmmoDef& a = ammoDefs()[d.ammo];
        lines.push_back(fmt("%d rounds   damage x%.2f   penetration %.0f%%", it.count, a.damage, a.penetration * 100.0f));
        break;
    }
    case ItemCat::Med: {
        const char* what[] = {"Stops light bleeding", "Stops heavy bleeding (limbs)", "Stops heavy bleeding", "Fixes a fracture",
                              "Suppresses pain (90 s)", "Restores HP, stops light bleeding", "Restores a destroyed limb"};
        lines.push_back(what[int(d.med)]);
        lines.push_back(fmt("Use time %.1f s", d.useTime));
        break;
    }
    case ItemCat::Optic:
        lines.push_back(std::string("Needs a ") + (opticDefs()[d.optic].mount <= 1 ? "pistol plate or any rail" : opticDefs()[d.optic].mount == 2 ? "short top rail" : "long top rail"));
        lines.push_back("Drag onto a weapon to mount");
        break;
    case ItemCat::Rig:
    case ItemCat::Backpack: lines.push_back(fmt("%d x %d", d.gridW, d.gridH)); break;
    }
    float w = 0;
    for (const std::string& l : lines) w = std::max(w, Hud::textWidth(l, 1.4f * m_s));
    float h = lines.size() * 17 * m_s + 8;
    float x = std::min(at.x + 18, W - w - 16), y = std::min(at.y + 18, H - h - 8);
    hud.rect(x, y, w + 12, h, glm::vec4(0.02f, 0.02f, 0.02f, 0.93f));
    float yy = y + 4;
    for (size_t i = 0; i < lines.size(); ++i) {
        hud.text(x + 6, yy, lines[i], i == 0 ? kAccent : kText, 1.4f * m_s);
        yy += 17 * m_s;
    }
}

void InventoryUi::beginDrag(const ItemHit& h, const glm::vec2& mouse) {
    m_drag = Drag{};
    m_drag.src = h.src;
    m_drag.grab = mouse - glm::vec2(h.rect.x, h.rect.y);
    if (h.src == Src::Grid) {
        for (int i = 0; i < int(h.grid->items.size()); ++i)
            if (h.grid->items[i].item.get() == h.item) {
                m_drag.grid = h.grid;
                m_drag.x = h.grid->items[i].x;
                m_drag.y = h.grid->items[i].y;
                m_drag.rot = m_drag.origRot = h.grid->items[i].rot;
                m_drag.item = h.grid->take(i);
                break;
            }
    } else if (h.src == Src::Slot) {
        m_drag.eq = h.eq;
        m_drag.slot = h.slot;
        m_drag.item = std::move(h.eq->slots[h.slot]);
    }
}

void InventoryUi::returnDrag() {
    if (!m_drag.item) return;
    if (m_drag.src == Src::Grid && m_drag.grid) {
        if (!m_drag.grid->place(m_drag.item, m_drag.x, m_drag.y, m_drag.origRot)) m_drag.grid->autoPlace(m_drag.item);
        if (m_drag.item) {
            // Lost its place (shouldn't happen): grow the grid rather than lose it.
            m_drag.grid->h += m_drag.item->d().h;
            m_drag.grid->autoPlace(m_drag.item);
        }
    } else if (m_drag.src == Src::Slot && m_drag.eq) {
        m_drag.eq->slots[m_drag.slot] = std::move(m_drag.item);
    }
    m_drag = Drag{};
}

Item* InventoryUi::findAnywhere(Context& ctx, uint32_t uid, Equipment** eqOut, Grid** gridOut) {
    if (Item* it = ctx.player->equip.find(uid)) {
        if (eqOut) *eqOut = &ctx.player->equip;
        return it;
    }
    if (ctx.body)
        if (Item* it = ctx.body->find(uid)) {
            if (eqOut) *eqOut = ctx.body;
            return it;
        }
    if (ctx.grid)
        for (Grid::Entry& e : ctx.grid->items)
            if (e.item->uid == uid) {
                if (gridOut) *gridOut = ctx.grid;
                return e.item.get();
            }
    return nullptr;
}

bool InventoryUi::drop(Context& ctx, const glm::vec2& mouse) {
    Item& it = *m_drag.item;
    const ItemDef& d = it.d();
    // Onto an item: combine (load ammo, insert magazine, mount optic, merge stacks)
    for (const ItemHit& h : m_items) {
        if (!h.rect.contains(mouse)) continue;
        Item& t = *h.item;
        const ItemDef& td = t.d();
        if (d.cat == ItemCat::Ammo && td.cat == ItemCat::Magazine) {
            if (!canLoad(t, it)) {
                say("That ammo doesn't fit");
                return false;
            }
            // Loading takes time; the ammo goes back where it was and is drawn from as it loads.
            uint32_t ammoUid = it.uid, magUid = t.uid;
            bool mineMag = h.mine;
            returnDrag();
            if (!mineMag || !ctx.player->equip.find(ammoUid)) {
                say("Both must be in your own gear to load");
                return true;
            }
            if (!ctx.player->startLoadMagazine(magUid, ammoUid)) say("Can't load that");
            return true;
        }
        if (d.cat == ItemCat::Ammo && td.cat == ItemCat::Ammo && td.ammo == d.ammo && t.count < td.stack) {
            int n = std::min(td.stack - t.count, it.count);
            t.count += n;
            it.count -= n;
            if (it.count <= 0) m_drag = Drag{};
            else returnDrag();
            return true;
        }
        if (d.cat == ItemCat::Magazine && td.cat == ItemCat::Weapon && magazineFits(d, td.weapon)) {
            ItemPtr old = std::move(t.mag);
            t.mag = std::move(m_drag.item);
            if (t.chambered < 0 && !t.mag->rounds.empty()) {
                t.chambered = t.mag->rounds.back();
                t.mag->rounds.pop_back();
            }
            m_drag.item = std::move(old);
            if (m_drag.item) returnDrag();
            else m_drag = Drag{};
            return true;
        }
        if (d.cat == ItemCat::Attachment && td.cat == ItemCat::Weapon) {
            ItemPtr removed;
            std::string why;
            if (!fitAttachment(t, m_drag.item, removed, why)) {
                say(why);
                return false;
            }
            m_drag = Drag{};
            if (removed && !ctx.player->equip.stow(removed) && ctx.grid) ctx.grid->autoPlace(removed);
            if (removed && ctx.profile && !ctx.inRaid) ctx.profile->addToStash(std::move(removed));
            if (removed && ctx.sim && ctx.inRaid) ctx.sim->dropFromCharacter(*ctx.player, std::move(removed));
            return true;
        }
        if (d.cat == ItemCat::Optic && td.cat == ItemCat::Weapon) {
            const WeaponDef& w = weaponDefs()[td.weapon];
            if (!opticFits(w, d.optic)) {
                say("That optic doesn't mount on this weapon");
                return false;
            }
            int old = t.optic;
            t.optic = d.optic;
            t.magnification = opticDefs()[d.optic].minMag;
            t.altSight = false;
            // A magnifier only works behind a red dot.
            if (t.att[AS_Magnifier] >= 0 && opticDefs()[d.optic].type != OpticType::RedDot) {
                ItemPtr mg = detachAttachment(t, AS_Magnifier);
                if (mg && !ctx.player->equip.stow(mg) && ctx.grid) ctx.grid->autoPlace(mg);
            }
            m_drag = Drag{};
            if (old > 0) {
                ItemPtr o = makeItem(opticItemDef(old));
                if (!ctx.player->equip.stow(o) && ctx.grid) ctx.grid->autoPlace(o);
                if (o) ctx.player->equip.pockets.autoPlace(o);
            }
            return true;
        }
    }
    for (const Target& t : m_targets) {
        if (!t.rect.contains(mouse)) continue;
        if (t.type == Target::ModT) {
            Item* w = findAnywhere(ctx, m_modUid);
            if (!w || d.cat != ItemCat::Attachment) {
                say(d.cat == ItemCat::Optic ? "Drop optics on the weapon itself" : "Only attachments go there");
                return false;
            }
            if (attachmentDefs()[d.attach].slot != t.slot) {
                say(std::string("That goes on the ") + attachSlotName(attachmentDefs()[d.attach].slot));
                return false;
            }
            ItemPtr removed;
            std::string why;
            if (!fitAttachment(*w, m_drag.item, removed, why)) {
                say(why);
                return false;
            }
            m_drag = Drag{};
            if (removed && !ctx.player->equip.stow(removed) && ctx.grid) ctx.grid->autoPlace(removed);
            if (removed && ctx.profile && !ctx.inRaid) ctx.profile->addToStash(std::move(removed));
            if (removed && ctx.sim && ctx.inRaid) ctx.sim->dropFromCharacter(*ctx.player, std::move(removed));
            return true;
        }
        if (t.type == Target::SellT) {
            if (!ctx.profile) return false;
            int price = store::sellPrice(it);
            ctx.profile->money += price;
            say("Sold " + it.d().name + " for " + std::to_string(price) + " cr");
            m_drag = Drag{};
            return true;
        }
        if (t.type == Target::ZoneT) {
            if (d.cat != ItemCat::Med) return false;
            uint32_t uid = it.uid;
            returnDrag();
            if (!ctx.player->equip.find(uid)) {
                say("Take it first");
                return true;
            }
            if (!ctx.player->startMed(uid, t.zone)) say("Nothing to treat there with that");
            return true;
        }
        if (t.type == Target::SlotT) {
            if (!t.eq->accepts(t.slot, it)) {
                say("Doesn't go there");
                return false;
            }
            if (t.eq->slots[t.slot]) {
                // Swap with what's there.
                ItemPtr other = std::move(t.eq->slots[t.slot]);
                t.eq->slots[t.slot] = std::move(m_drag.item);
                m_drag.item = std::move(other);
                if (m_drag.src == Src::Slot && m_drag.eq && m_drag.eq->accepts(m_drag.slot, *m_drag.item)) {
                    m_drag.eq->slots[m_drag.slot] = std::move(m_drag.item);
                    m_drag = Drag{};
                } else {
                    returnDrag();
                }
                return true;
            }
            t.eq->slots[t.slot] = std::move(m_drag.item);
            m_drag = Drag{};
            return true;
        }
        if (t.type == Target::GridT) {
            if (t.mine && !Equipment::stowable(it)) {
                say("Weapons and gear go in equipment slots");
                return false;
            }
            // A backpack can't go inside itself.
            if (it.grid.get() == t.grid) return false;
            glm::vec2 local = mouse - m_drag.grab + glm::vec2(t.cell * 0.5f) - glm::vec2(t.rect.x, t.rect.y);
            int cx = int(std::floor(local.x / t.cell)), cy = int(std::floor(local.y / t.cell));
            if (t.grid->place(m_drag.item, cx, cy, m_drag.rot)) {
                m_drag = Drag{};
                return true;
            }
            return false;
        }
    }
    // Into the world
    if (ctx.inRaid && ctx.sim) {
        ctx.sim->dropFromCharacter(*ctx.player, std::move(m_drag.item));
        m_drag = Drag{};
        return true;
    }
    return false;
}

bool InventoryUi::quickMove(Context& ctx, const ItemHit& h) {
    beginDrag(h, m_mouse);
    if (!m_drag.item) return false;
    bool ok = false;
    if (h.mine) {
        // To the stash / container
        if (ctx.grid) ok = ctx.grid->autoPlace(m_drag.item);
        else if (ctx.body) ok = ctx.body->stow(m_drag.item);
    } else {
        Equipment& eq = ctx.player->equip;
        ItemCat c = m_drag.item->d().cat;
        if (c == ItemCat::Weapon) {
            for (int s : {ES_Hand0, ES_Hand1, ES_Back0, ES_Back1})
                if (!eq.slots[s]) {
                    eq.slots[s] = std::move(m_drag.item);
                    ok = true;
                    break;
                }
        } else if (c == ItemCat::Rig || c == ItemCat::Backpack || c == ItemCat::Armor || c == ItemCat::Helmet) {
            int s = c == ItemCat::Rig ? ES_Rig : c == ItemCat::Backpack ? ES_Backpack : c == ItemCat::Armor ? ES_Armor : ES_Helmet;
            if (!eq.slots[s]) {
                eq.slots[s] = std::move(m_drag.item);
                ok = true;
            }
        } else {
            ok = eq.stow(m_drag.item);
        }
        if (ok && ctx.inRaid && ctx.sim) ++ctx.sim->stats.itemsLooted;
    }
    if (ok || !m_drag.item) m_drag = Drag{};
    else {
        returnDrag();
        say("No room");
    }
    return ok;
}

void InventoryUi::openMenu(const ItemHit& h, const glm::vec2& at, Context& ctx) {
    m_menu = Menu{};
    m_menu.open = true;
    m_menu.at = at;
    m_menu.uid = h.item->uid;
    m_menu.mine = h.mine;
    const ItemDef& d = h.item->d();
    switch (d.cat) {
    case ItemCat::Med: if (h.mine) m_menu.options.push_back("Use"); break;
    case ItemCat::Magazine:
        if (!h.item->rounds.empty()) m_menu.options.push_back("Unload");
        if (h.mine) m_menu.options.push_back("Load ammo");
        break;
    case ItemCat::Ammo: if (h.mine) m_menu.options.push_back("Load into magazine"); break;
    case ItemCat::Weapon:
        if (loadedRounds(*h.item) > 0 || h.item->mag) m_menu.options.push_back("Unload");
        if (h.item->optic > 0) m_menu.options.push_back("Remove optic");
        m_menu.options.push_back("Modify");
        if (h.item->weaponDef().foldable) m_menu.options.push_back(h.item->folded ? "Unfold stock" : "Fold stock");
        break;
    default: break;
    }
    if (ctx.panel == Panel::Store) m_menu.options.push_back("Sell for " + std::to_string(store::sellPrice(*h.item)) + " cr");
    m_menu.options.push_back(h.mine ? (ctx.grid || ctx.body ? "Move across" : "") : "Take");
    if (h.mine && ctx.inRaid) m_menu.options.push_back("Drop");
    m_menu.options.erase(std::remove(m_menu.options.begin(), m_menu.options.end(), std::string()), m_menu.options.end());
}

void InventoryUi::runMenu(Context& ctx, const std::string& opt) {
    Equipment* eq = nullptr;
    Grid* grid = nullptr;
    Item* it = findAnywhere(ctx, m_menu.uid, &eq, &grid);
    m_menu.open = false;
    if (!it) return;
    Equipment& mine = ctx.player->equip;
    auto stowNear = [&](ItemPtr p) {
        if (!p) return;
        if (eq == &mine && mine.stow(p)) return;
        if (grid && grid->autoPlace(p)) return;
        if (eq && eq != &mine && eq->stow(p)) return;
        if (mine.stow(p)) return;
        if (ctx.grid && ctx.grid->autoPlace(p)) return;
        if (ctx.sim && ctx.inRaid) ctx.sim->dropFromCharacter(*ctx.player, std::move(p));
        else if (ctx.profile) ctx.profile->addToStash(std::move(p));
    };
    auto roundsToStacks = [&](std::vector<uint8_t> rounds) {
        std::sort(rounds.begin(), rounds.end());
        size_t i = 0;
        while (i < rounds.size()) {
            size_t j = i;
            while (j < rounds.size() && rounds[j] == rounds[i]) ++j;
            stowNear(makeAmmo(rounds[i], int(j - i)));
            i = j;
        }
    };
    if (opt.rfind("Sell for", 0) == 0) {
        sellItem(ctx, it->uid);
    } else if (opt == "Use") {
        if (!ctx.player->startMed(it->uid, -1)) say("Nothing needs that right now");
    } else if (opt == "Unload") {
        if (it->d().cat == ItemCat::Magazine) {
            std::vector<uint8_t> r = it->rounds;
            it->rounds.clear();
            roundsToStacks(r);
        } else {
            std::vector<uint8_t> r = it->rounds;
            it->rounds.clear();
            if (it->chambered >= 0) r.push_back(uint8_t(it->chambered));
            it->chambered = -1;
            roundsToStacks(r);
            if (it->mag) stowNear(std::move(it->mag));
        }
    } else if (opt == "Load ammo" || opt == "Load into magazine") {
        bool isMag = it->d().cat == ItemCat::Magazine;
        for (Grid* g : mine.grids())
            for (Grid::Entry& e : g->items) {
                Item* mag = isMag ? it : e.item.get();
                Item* ammo = isMag ? e.item.get() : it;
                if (mag->d().cat == ItemCat::Magazine && ammo->d().cat == ItemCat::Ammo && canLoad(*mag, *ammo)) {
                    if (ctx.player->startLoadMagazine(mag->uid, ammo->uid)) return;
                }
            }
        say(isMag ? "No matching ammo" : "No magazine to load");
    } else if (opt == "Remove optic") {
        int o = it->optic;
        it->optic = 0;
        it->magnification = 1.0f;
        it->altSight = false;
        stowNear(makeItem(opticItemDef(o)));
        if (it->att[AS_Magnifier] >= 0) stowNear(detachAttachment(*it, AS_Magnifier));
    } else if (opt == "Modify") {
        m_modUid = it->uid;
    } else if (opt == "Fold stock" || opt == "Unfold stock") {
        it->folded = opt == "Fold stock";
    } else if (opt == "Take" || opt == "Move across") {
        for (const ItemHit& h : m_items)
            if (h.item == it) {
                quickMove(ctx, h);
                break;
            }
    } else if (opt == "Drop") {
        ItemPtr p = mine.remove(it->uid);
        if (p && ctx.sim) ctx.sim->dropFromCharacter(*ctx.player, std::move(p));
    }
}

bool InventoryUi::update(const UiInput& in, Context& ctx, Hud& hud, float W, float H) {
    m_s = std::clamp(H / 900.0f, 0.75f, 2.0f);
    m_cell = std::floor(34.0f * m_s);
    m_mouse = in.mouse;
    m_items.clear();
    m_targets.clear();
    noteTimer = std::max(0.0f, noteTimer - 1.0f / 60.0f);
    if (!ctx.player) return true;

    hud.rect(0, 0, W, H, kPanel);
    float top = 60 * m_s;
    hud.text(20 * m_s, 18 * m_s, ctx.inRaid ? "INVENTORY" : ctx.panel == Panel::Store ? "LOADOUT  -  BASE STORE" : "LOADOUT", kAccent, 2.4f * m_s);
    hud.text(W - 20 * m_s - Hud::textWidth("Tab / Esc close", 1.4f * m_s), 22 * m_s, "Tab / Esc close", kDim, 1.4f * m_s);

    drawEquipment(hud, ctx.player->equip, 20 * m_s, top + 10 * m_s, true, false);
    float healthX = 20 * m_s + 5 * m_cell + 30 * m_s + 6 * m_cell + 40 * m_s;
    if (m_modUid && !findAnywhere(ctx, m_modUid)) m_modUid = 0;
    if (m_modUid) {
        float px0 = W - 10 * m_cell - 40 * m_s;
        drawMods(hud, ctx, in, healthX - 10 * m_s, top + 4 * m_s, px0 - healthX - 20 * m_s, H - top - 60 * m_s);
    } else if (ctx.panel == Panel::Store) {
        float px0 = W - 10 * m_cell - 40 * m_s;
        drawStore(hud, ctx, in, healthX - 10 * m_s, top + 4 * m_s, px0 - healthX - 20 * m_s, H - top - 60 * m_s);
    } else {
        drawHealth(hud, *ctx.player, healthX, top + 10 * m_s);
    }

    // Right panel
    float px = W - 10 * m_cell - 40 * m_s;
    if ((ctx.panel == Panel::Stash || ctx.panel == Panel::Store) && ctx.grid) {
        float rows = std::floor((H - top - 80 * m_s) / m_cell);
        m_stashScroll = std::clamp(m_stashScroll - in.scroll * 2.0f, 0.0f, std::max(0.0f, float(ctx.grid->h) - rows));
        if (!in.lmbDown) m_stashScroll = std::floor(m_stashScroll);
        drawGrid(hud, *ctx.grid, px, top + 10 * m_s, fmt("STASH  (wheel to scroll, %d rows)", ctx.grid->h), false, rows, m_stashScroll);
        if (ctx.profile && !ctx.profile->hasWeapon(ctx.player->equip)) {
            Rect b{px, H - 50 * m_s, 10 * m_cell, 34 * m_s};
            bool hover = b.contains(in.mouse);
            hud.rect(b.x, b.y, b.w, b.h, hover ? glm::vec4(0.35f, 0.3f, 0.15f, 0.95f) : glm::vec4(0.22f, 0.2f, 0.12f, 0.95f));
            hud.text(b.x + 10, b.y + 8 * m_s, "Take free kit (pistol, 2 mags, bandage)", kText, 1.4f * m_s);
            if (hover && in.lmbPressed && !m_drag.item) {
                Equipment kit;
                makeFreeKit(kit);
                Equipment& eq = ctx.player->equip;
                for (int s = 0; s < kEquipSlots; ++s)
                    if (kit.slots[s]) {
                        if (!eq.slots[s]) eq.slots[s] = std::move(kit.slots[s]);
                        else ctx.profile->addToStash(std::move(kit.slots[s]));
                    }
                for (Grid::Entry& e : kit.pockets.items) ctx.profile->addToStash(std::move(e.item));
                return false;
            }
        }
    } else if (ctx.panel == Panel::Container && ctx.grid) {
        hud.text(px, top - 20 * m_s, ctx.title, kAccent, 2.0f * m_s);
        drawGrid(hud, *ctx.grid, px, top + 20 * m_s, "Contents", false);
    } else if (ctx.panel == Panel::Body && ctx.body) {
        hud.text(px, top - 20 * m_s, ctx.title, kAccent, 2.0f * m_s);
        drawEquipment(hud, *ctx.body, px, top + 30 * m_s, false, true);
    }

    // ----- input
    m_hover = nullptr;
    for (const ItemHit& h : m_items)
        if (h.rect.contains(in.mouse)) m_hover = h.item;

    if (m_menu.open) {
        float mw = 190 * m_s, mh = 22 * m_s;
        float x = std::min(m_menu.at.x, W - mw - 4), y = m_menu.at.y;
        for (size_t i = 0; i < m_menu.options.size(); ++i) {
            Rect r{x, y + i * mh, mw, mh};
            bool hover = r.contains(in.mouse);
            hud.rect(r.x, r.y, r.w, r.h, hover ? glm::vec4(0.3f, 0.28f, 0.15f, 0.97f) : glm::vec4(0.1f, 0.1f, 0.1f, 0.97f));
            hud.text(r.x + 8, r.y + 4 * m_s, m_menu.options[i], kText, 1.4f * m_s);
            if (hover && in.lmbPressed) {
                runMenu(ctx, m_menu.options[i]);
                return false;
            }
        }
        if (in.lmbPressed || in.rmbPressed) m_menu.open = false;
    } else if (m_drag.item) {
        if (in.rotate && m_drag.item->d().w != m_drag.item->d().h) m_drag.rot = !m_drag.rot;
        if (in.lmbReleased) {
            if (!drop(ctx, in.mouse)) returnDrag();
        }
    } else if (in.lmbPressed) {
        for (const ItemHit& h : m_items)
            if (h.rect.contains(in.mouse)) {
                if (in.shift) quickMove(ctx, h);
                else beginDrag(h, in.mouse);
                break;
            }
    } else if (in.rmbPressed) {
        for (const ItemHit& h : m_items)
            if (h.rect.contains(in.mouse)) {
                openMenu(h, in.mouse, ctx);
                break;
            }
    }

    // Dragged item follows the mouse, with a fit preview.
    if (m_drag.item) {
        const Item& it = *m_drag.item;
        float w = it.width(m_drag.rot) * m_cell, h = it.height(m_drag.rot) * m_cell;
        glm::vec2 p = in.mouse - m_drag.grab;
        for (const Target& t : m_targets) {
            if (t.type != Target::GridT || !t.rect.contains(in.mouse)) continue;
            glm::vec2 local = p + glm::vec2(t.cell * 0.5f) - glm::vec2(t.rect.x, t.rect.y);
            int cx = int(std::floor(local.x / t.cell)), cy = int(std::floor(local.y / t.cell));
            bool fits = t.grid->fits(it, cx, cy, m_drag.rot) && (!t.mine || Equipment::stowable(it));
            hud.rect(t.rect.x + cx * t.cell, t.rect.y + cy * t.cell, w, h, fits ? kGood : kBad);
        }
        drawItem(hud, it, {p.x, p.y, w, h}, 0.8f);
    } else if (m_hover && !m_menu.open) {
        drawTooltip(hud, *m_hover, in.mouse, W, H);
    }
    if (noteTimer > 0.0f) hud.text(W * 0.5f - Hud::textWidth(note, 2.0f * m_s) * 0.5f, H - 40 * m_s, note, glm::vec4(1.0f, 0.6f, 0.4f, 1.0f), 2.0f * m_s);
    hud.text(20 * m_s, H - 24 * m_s, "Drag to move   R rotate   Shift+click quick move   Right-click actions   Drop ammo on a magazine to load", kDim,
             1.3f * m_s);
    return false;
}
