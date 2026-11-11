#pragma once

#include "items/Item.h"
#include "items/Store.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

class Hud;
class Character;
class Simulation;
struct Profile;

struct UiInput {
    glm::vec2 mouse{0.0f};
    bool lmbDown = false, lmbPressed = false, lmbReleased = false, rmbPressed = false;
    float scroll = 0.0f;
    bool rotate = false; // R while dragging
    bool shift = false;
};

// Tarkov-style gear screen: your equipment and grids, a body diagram for treating wounds,
// and a second panel for the stash or whatever you are looting.
class InventoryUi {
public:
    enum class Panel { None, Stash, Container, Body, Store };
    struct Context {
        Character* player = nullptr;
        Simulation* sim = nullptr;
        Profile* profile = nullptr;
        Panel panel = Panel::None;
        Grid* grid = nullptr;       // stash or container
        Equipment* body = nullptr;  // a corpse
        std::string title;
        bool inRaid = false;
    };

    void reset();
    // Draws and handles the screen. Returns a request to close.
    bool update(const UiInput& in, Context& ctx, Hud& hud, float W, float H);
    bool dragging() const { return m_drag.item != nullptr; }
    // Puts a dragged item back (when the screen closes mid-drag).
    void cancelDrag();
    std::string note;
    float noteTimer = 0.0f;

private:
    struct Rect {
        float x, y, w, h;
        bool contains(const glm::vec2& p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    };
    enum class Src { None, Grid, Slot };
    struct ItemHit {
        Rect rect;
        Item* item;
        Src src;
        Grid* grid;
        Equipment* eq;
        int slot;
        bool mine; // part of the player's own gear
    };
    struct Target {
        enum Type { GridT, SlotT, ZoneT, SellT, ModT } type;
        Rect rect;
        Grid* grid = nullptr;
        float cell = 0.0f;
        Equipment* eq = nullptr;
        int slot = -1;
        int zone = -1;
        bool mine = false;
    };
    struct Drag {
        ItemPtr item;
        Src src = Src::None;
        Grid* grid = nullptr;
        int x = 0, y = 0;
        bool rot = false, origRot = false;
        Equipment* eq = nullptr;
        int slot = -1;
        glm::vec2 grab{0.0f};
    };
    struct Menu {
        bool open = false;
        glm::vec2 at{0.0f};
        uint32_t uid = 0;
        bool mine = false;
        std::vector<std::string> options;
    };

    void drawGrid(Hud& hud, Grid& g, float x, float y, const std::string& title, bool mine, float maxRows = 1e9f, float scroll = 0.0f);
    void drawSlot(Hud& hud, Equipment& eq, int slot, Rect r, bool mine);
    void drawItem(Hud& hud, const Item& it, Rect r, float alpha = 1.0f);
    void drawHealth(Hud& hud, Character& c, float x, float y);
    void drawTooltip(Hud& hud, const Item& it, glm::vec2 at, float W, float H);
    void drawEquipment(Hud& hud, Equipment& eq, float x, float y, bool mine, bool compact);
    void drawStore(Hud& hud, Context& ctx, const UiInput& in, float x, float y, float w, float h);
    bool sellItem(Context& ctx, uint32_t uid);
    // Weapon modding panel (InventoryMods.cpp)
    void drawMods(Hud& hud, Context& ctx, const UiInput& in, float x, float y, float w, float h);
    // Fits an attachment item onto a weapon (slot -1: its own slot). The item is consumed on success;
    // whatever was in the slot comes back in 'removed'.
    bool fitAttachment(Item& weapon, ItemPtr& att, ItemPtr& removed, std::string& why);
    ItemPtr detachAttachment(Item& weapon, int slot);

    void beginDrag(const ItemHit& h, const glm::vec2& mouse);
    bool drop(Context& ctx, const glm::vec2& mouse);
    void returnDrag();
    bool quickMove(Context& ctx, const ItemHit& h);
    void openMenu(const ItemHit& h, const glm::vec2& at, Context& ctx);
    void runMenu(Context& ctx, const std::string& option);
    Item* findAnywhere(Context& ctx, uint32_t uid, Equipment** eqOut = nullptr, Grid** gridOut = nullptr);
    void say(const std::string& s) {
        note = s;
        noteTimer = 2.5f;
    }

    float m_cell = 34.0f, m_s = 1.0f;
    std::vector<ItemHit> m_items;
    std::vector<Target> m_targets;
    Drag m_drag;
    Menu m_menu;
    float m_stashScroll = 0.0f;
    StoreTab m_tab = StoreTab::Weapons;
    int m_filterWeapon = -1;
    float m_storeScroll = 0.0f;
    const Item* m_hover = nullptr;
    uint32_t m_modUid = 0; // weapon open in the modding panel
    glm::vec2 m_mouse{0.0f};
};
