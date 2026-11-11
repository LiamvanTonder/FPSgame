// The base store screen (part of InventoryUi).
#include "ui/InventoryUi.h"

#include "game/Character.h"
#include "game/Attachments.h"
#include "game/WeaponDefs.h"
#include "items/Profile.h"
#include "render/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
const glm::vec4 kText(0.9f, 0.9f, 0.88f, 1.0f), kDim(0.62f, 0.64f, 0.63f, 1.0f), kAccent(1.0f, 0.85f, 0.35f, 1.0f),
    kGreen(0.45f, 0.9f, 0.45f, 1.0f), kRedText(1.0f, 0.5f, 0.4f, 1.0f);

std::string credits(int n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d cr", n);
    return buf;
}

// Every distinct gun the player owns (loadout and stash).
std::vector<int> arsenal(const Equipment& eq, const Grid* stash) {
    std::vector<int> out;
    auto add = [&](const Item& it) {
        if (it.d().cat == ItemCat::Weapon && std::find(out.begin(), out.end(), it.d().weapon) == out.end()) out.push_back(it.d().weapon);
    };
    for (const ItemPtr& s : eq.slots)
        if (s) add(*s);
    if (stash)
        for (const Grid::Entry& e : stash->items) add(*e.item);
    return out;
}

std::string shortGunName(int weapon) {
    std::string n = weaponDefs()[weapon].name;
    size_t sp = n.find(' ');
    if (sp != std::string::npos) sp = n.find(' ', sp + 1);
    return sp == std::string::npos ? n : n.substr(0, sp);
}

std::string offerInfo(const ItemDef& d) {
    char buf[160];
    switch (d.cat) {
    case ItemCat::Weapon: {
        const WeaponDef& w = weaponDefs()[d.weapon];
        const char* action = w.canAuto ? "auto" : w.action == ActionType::Bolt ? "bolt" : w.action == ActionType::Pump ? "pump" : "semi";
        int cap = w.reload == ReloadType::SingleLoad ? 1 : w.magazine;
        std::snprintf(buf, sizeof(buf), "%s  %s  %d rds  %.0f m/s%s", caliberName(w.caliber), action, cap, w.projectile.muzzleVelocity,
                      w.suppressed ? "  suppressed" : "");
        return buf;
    }
    case ItemCat::Ammo: {
        const AmmoDef& a = ammoDefs()[d.ammo];
        std::snprintf(buf, sizeof(buf), "%d rounds  dmg x%.2f  pen %.0f%%", d.stack, a.damage, a.penetration * 100.0f);
        return buf;
    }
    case ItemCat::Magazine:
        std::snprintf(buf, sizeof(buf), "%d rounds  (sold empty)", d.capacity);
        return buf;
    case ItemCat::Med: {
        const char* what[] = {"light bleeding", "heavy bleeding (limbs)", "heavy bleeding", "fractures", "pain, 90 s", "HP + light bleeding",
                              "destroyed limbs"};
        return std::string("treats ") + what[int(d.med)];
    }
    case ItemCat::Optic: {
        const OpticDef& o = opticDefs()[d.optic];
        std::snprintf(buf, sizeof(buf), "%s  %s rail", o.type == OpticType::RedDot ? "1x dot" : o.minMag < o.maxMag ? "variable scope" : "magnified",
                      o.mount <= 1 ? "any" : o.mount == 2 ? "short" : "long");
        return buf;
    }
    case ItemCat::Attachment: {
        const AttachmentDef& a = attachmentDefs()[d.attach];
        std::string s = std::string(attachSlotName(a.slot)) + "  " + attachSpecialName(a.special);
        if (a.slot == AS_Muzzle) s += std::string("  ") + threadName(a.mount);
        if (a.slot == AS_Stock) s += std::string("  ") + stockMountName(a.mount);
        if (a.loudnessDb != 0.0f) {
            std::snprintf(buf, sizeof(buf), "  %+.0f dB", a.loudnessDb);
            s += buf;
        }
        return s;
    }
    case ItemCat::Armor: case ItemCat::Helmet:
        std::snprintf(buf, sizeof(buf), "class %d  %.0f condition  %.1f kg", d.armorClass, d.durability, d.mass);
        return buf;
    default:
        std::snprintf(buf, sizeof(buf), "%d x %d storage", d.gridW, d.gridH);
        return buf;
    }
}
} // namespace

bool InventoryUi::sellItem(Context& ctx, uint32_t uid) {
    if (!ctx.profile) return false;
    ItemPtr it;
    if (ctx.grid)
        for (int i = 0; i < int(ctx.grid->items.size()) && !it; ++i)
            if (ctx.grid->items[i].item->uid == uid) it = ctx.grid->take(i);
    if (!it) it = ctx.player->equip.remove(uid);
    if (!it) return false;
    int price = store::sellPrice(*it);
    ctx.profile->money += price;
    say("Sold " + it->d().name + " for " + credits(price));
    return true;
}

void InventoryUi::drawStore(Hud& hud, Context& ctx, const UiInput& in, float x, float y, float w, float h) {
    float s = m_s;
    hud.rect(x - 8 * s, y - 8 * s, w + 16 * s, h + 16 * s, glm::vec4(0.09f, 0.09f, 0.09f, 0.92f));
    hud.text(x, y, "STORE", kAccent, 2.4f * s);
    std::string money = "Credits: " + credits(ctx.profile ? ctx.profile->money : 0);
    hud.text(x + w - Hud::textWidth(money, 2.0f * s), y + 2 * s, money, kGreen, 2.0f * s);
    float yy = y + 34 * s;

    auto button = [&](float bx, float by, float bw, float bh, const std::string& label, bool active, bool enabled = true) {
        bool hover = enabled && in.mouse.x >= bx && in.mouse.y >= by && in.mouse.x < bx + bw && in.mouse.y < by + bh;
        glm::vec4 c = !enabled ? glm::vec4(0.12f, 0.12f, 0.12f, 0.9f)
                    : active ? glm::vec4(0.45f, 0.38f, 0.15f, 0.95f)
                    : hover ? glm::vec4(0.28f, 0.26f, 0.18f, 0.95f) : glm::vec4(0.17f, 0.17f, 0.17f, 0.95f);
        hud.rect(bx, by, bw, bh, c);
        hud.text(bx + 6 * s, by + (bh - 12 * 1.3f * s) * 0.5f, label, enabled ? kText : kDim, 1.3f * s);
        return hover && in.lmbPressed && !m_drag.item && !m_menu.open;
    };

    // Your guns: click one to see exactly what feeds it.
    std::vector<int> guns = arsenal(ctx.player->equip, ctx.panel == Panel::Store ? ctx.grid : nullptr);
    hud.text(x, yy, "Your guns (click to filter ammo, magazines, optics and attachments):", kDim, 1.3f * s);
    yy += 18 * s;
    float cx = x;
    if (m_filterWeapon >= 0 && std::find(guns.begin(), guns.end(), m_filterWeapon) == guns.end()) m_filterWeapon = -1;
    for (int g : guns) {
        std::string label = shortGunName(g);
        float bw = Hud::textWidth(label, 1.3f * s) + 14 * s;
        if (cx + bw > x + w) {
            cx = x;
            yy += 24 * s;
        }
        if (button(cx, yy, bw, 20 * s, label, m_filterWeapon == g)) {
            m_filterWeapon = m_filterWeapon == g ? -1 : g;
            if (m_filterWeapon >= 0 && m_tab == StoreTab::Weapons) m_tab = StoreTab::Ammo;
            m_storeScroll = 0.0f;
        }
        cx += bw + 6 * s;
    }
    if (guns.empty()) hud.text(x, yy + 3 * s, "(none: buy one below, or deploy with the free pistol)", kDim, 1.3f * s);
    yy += 26 * s;
    if (m_filterWeapon >= 0) {
        const WeaponDef& wd = weaponDefs()[m_filterWeapon];
        std::string mags;
        for (const ItemDef& d : itemDefs())
            if (d.cat == ItemCat::Magazine && magazineFits(d, m_filterWeapon)) mags += (mags.empty() ? "" : ", ") + std::to_string(d.capacity);
        std::string takes = std::string(wd.name) + " takes " + caliberName(wd.caliber) + "   ";
        if (wd.reload == ReloadType::Magazine) takes += "magazines: " + mags + " rds";
        else if (wd.reload == ReloadType::Tube) takes += "loose rounds, " + std::to_string(wd.magazine) + " at a time";
        else takes += "single rounds, hand-fed";
        hud.text(x, yy, takes, kGreen, 1.3f * s);
        yy += 18 * s;
    }

    // Tabs
    float tx = x;
    for (int t = 0; t < int(StoreTab::kCount); ++t) {
        std::string label = storeTabName(StoreTab(t));
        float bw = Hud::textWidth(label, 1.3f * s) + 16 * s;
        if (button(tx, yy, bw, 22 * s, label, int(m_tab) == t)) {
            m_tab = StoreTab(t);
            m_storeScroll = 0.0f;
        }
        tx += bw + 4 * s;
    }
    yy += 30 * s;

    // Offers
    std::vector<StoreOffer> offers = store::offers(m_tab);
    if (m_filterWeapon >= 0 && (m_tab == StoreTab::Ammo || m_tab == StoreTab::Magazines || m_tab == StoreTab::Optics || m_tab == StoreTab::Attachments)) {
        offers.erase(std::remove_if(offers.begin(), offers.end(),
                                    [&](const StoreOffer& o) { return !store::fits(itemDefs()[o.def], m_filterWeapon); }),
                     offers.end());
    }
    float rowH = 34 * s;
    float listTop = yy, listH = y + h - 60 * s - listTop;
    int visible = std::max(1, int(listH / rowH));
    bool overList = in.mouse.x >= x && in.mouse.x < x + w && in.mouse.y >= listTop && in.mouse.y < listTop + listH;
    if (overList) m_storeScroll = std::clamp(m_storeScroll - in.scroll * 2.0f, 0.0f, std::max(0.0f, float(offers.size() - visible)));
    int first = int(m_storeScroll);
    for (int i = first; i < int(offers.size()) && i < first + visible; ++i) {
        const StoreOffer& o = offers[i];
        const ItemDef& d = itemDefs()[o.def];
        float ry = listTop + (i - first) * rowH;
        hud.rect(x, ry, w, rowH - 3 * s, (i & 1) ? glm::vec4(0.14f, 0.14f, 0.14f, 0.9f) : glm::vec4(0.11f, 0.11f, 0.11f, 0.9f));
        bool owned = d.cat == ItemCat::Weapon && std::find(guns.begin(), guns.end(), d.weapon) != guns.end();
        hud.text(x + 6 * s, ry + 3 * s, d.name + (owned ? "   (owned)" : ""), kText, 1.4f * s);
        // Which of your guns this feeds, in green.
        std::string fitsLine;
        if (d.cat == ItemCat::Ammo || d.cat == ItemCat::Magazine || d.cat == ItemCat::Optic || d.cat == ItemCat::Attachment)
            for (int g : guns)
                if (store::fits(d, g)) fitsLine += (fitsLine.empty() ? "fits: " : ", ") + shortGunName(g);
        hud.text(x + 6 * s, ry + 18 * s, offerInfo(d), kDim, 1.15f * s);
        if (!fitsLine.empty()) hud.text(x + 6 * s + Hud::textWidth(offerInfo(d), 1.15f * s) + 14 * s, ry + 18 * s, fitsLine, kGreen, 1.15f * s);
        std::string price = credits(o.price);
        bool afford = ctx.profile && ctx.profile->money >= o.price;
        hud.text(x + w - 150 * s - Hud::textWidth(price, 1.4f * s), ry + 9 * s, price, afford ? kText : kRedText, 1.4f * s);
        if (button(x + w - 130 * s, ry + 5 * s, 120 * s, rowH - 13 * s, "Buy", false, afford)) {
            std::string msg;
            store::buy(*ctx.profile, o, msg);
            say(msg);
        }
    }
    if (offers.empty()) hud.text(x, listTop + 6 * s, "Nothing here fits that gun.", kDim, 1.4f * s);
    if (int(offers.size()) > visible)
        hud.text(x, listTop + listH + 2 * s, "Mouse wheel: more (" + std::to_string(first + 1) + "-" + std::to_string(std::min(int(offers.size()), first + visible)) +
                                                 " of " + std::to_string(offers.size()) + ")",
                 kDim, 1.2f * s);

    // Sell box: drop anything here (or right-click it -> Sell).
    Rect sell{x, y + h - 40 * s, w, 40 * s};
    bool hot = m_drag.item && sell.contains(in.mouse);
    hud.rect(sell.x, sell.y, sell.w, sell.h, hot ? glm::vec4(0.35f, 0.25f, 0.1f, 0.95f) : glm::vec4(0.18f, 0.14f, 0.08f, 0.9f));
    std::string sl = m_drag.item ? "Drop to sell for " + credits(store::sellPrice(*m_drag.item))
                                 : "SELL: drag items here, or right-click > Sell  (half their value)";
    hud.text(sell.x + 10 * s, sell.y + 12 * s, sl, kAccent, 1.5f * s);
    Target t;
    t.type = Target::SellT;
    t.rect = sell;
    m_targets.push_back(t);
}
