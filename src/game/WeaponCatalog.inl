// Included from WeaponDefs.cpp inside its anonymous namespace: parametric placeholder
// geometry for the wider weapon catalogue, and the catalogue itself.

const glm::vec3 kTan(0.52f, 0.45f, 0.32f);
const glm::vec3 kPlum(0.20f, 0.18f, 0.20f);
const glm::vec3 kSteelBlue(0.18f, 0.20f, 0.24f);
const glm::vec3 kBirch(0.55f, 0.38f, 0.20f);

enum class MagStyle { None, Straight, Curved, Box, Drum, Tube };
enum class StockStyle { None, Polymer, Wood, Skeleton, Bullpup };
enum class SightStyle { Peep, Notch };

struct GunShape {
    float stockLen = 0.24f;
    StockStyle stock = StockStyle::Polymer;
    float receiverLen = 0.22f, receiverH = 0.055f;
    float handguardLen = 0.22f, handguardW = 0.056f;
    float barrelLen = 0.40f; // chamber to muzzle
    float barrelR = 0.009f;
    MagStyle mag = MagStyle::Curved;
    float magLen = 0.15f, magW = 0.026f, magDepth = 0.07f, magTilt = 0.2f;
    SightStyle sight = SightStyle::Peep;
    float sightH = 0.065f;
    bool suppressor = false;
    float suppressorLen = 0.17f;
    bool pump = false;
    bool twinBarrel = false;
    glm::vec3 furniture = kPolymer, metal = kMetal, accent = kMetalLight;
};

// Builds parts and anchor points of a long gun from a handful of proportions.
void buildLongGun(WeaponDef& w, const GunShape& s) {
    auto& p = w.parts;
    p.clear();
    const bool bullpup = s.stock == StockStyle::Bullpup;
    float recRear = bullpup ? 0.0f : -s.stockLen;
    float recFront = recRear - s.receiverLen;
    float recTop = s.receiverH * 0.5f - 0.005f;
    float recMid = (recRear + recFront) * 0.5f;

    // Stock
    if (!bullpup && s.stockLen > 0.0f) {
        float mid = -s.stockLen * 0.5f;
        switch (s.stock) {
        case StockStyle::Wood:
            p.push_back(box({0, -0.030f, mid}, {0.044f, 0.105f, s.stockLen}, s.furniture));
            break;
        case StockStyle::Skeleton:
            p.push_back(box({0, 0.010f, mid}, {0.020f, 0.016f, s.stockLen}, s.metal));
            p.push_back(box({0, -0.055f, mid}, {0.020f, 0.016f, s.stockLen}, s.metal));
            p.push_back(box({0, -0.022f, -0.010f}, {0.040f, 0.090f, 0.020f}, s.furniture));
            break;
        default:
            p.push_back(box({0, -0.012f, mid}, {0.045f, 0.085f, s.stockLen}, s.furniture));
            break;
        }
    }
    // Receiver (a bullpup's receiver is the stock)
    if (bullpup) {
        p.push_back(box({0, -0.020f, recMid}, {0.055f, 0.095f, s.receiverLen}, s.furniture));
        p.push_back(box({0, 0.020f, recMid - 0.03f}, {0.040f, 0.030f, s.receiverLen * 0.8f}, s.metal));
    } else {
        p.push_back(box({0, -0.005f, recMid}, {0.050f, s.receiverH, s.receiverLen}, s.metal));
    }
    float railTop = (bullpup ? 0.035f : recTop) + 0.008f;
    p.push_back(box({0, railTop - 0.004f, recMid - (bullpup ? 0.03f : 0.0f)}, {0.022f, 0.008f, s.receiverLen * 0.9f}, s.accent));

    // Grip and magazine
    float gripZ = bullpup ? recFront + 0.12f : recRear - 0.06f;
    float magZ = bullpup ? recRear - 0.13f : recRear - s.receiverLen * 0.58f;
    if (s.stock != StockStyle::Wood) p.push_back(box({0, -0.075f, gripZ}, {0.030f, 0.100f, 0.035f}, kPolymer, -0.35f));
    float magTop = -s.receiverH * 0.5f;
    switch (s.mag) {
    case MagStyle::Straight:
        p.push_back(box({0, magTop - s.magLen * 0.5f + 0.02f, magZ}, {s.magW, s.magLen, s.magDepth}, s.metal));
        break;
    case MagStyle::Curved:
        p.push_back(box({0, magTop - s.magLen * 0.30f + 0.02f, magZ}, {s.magW, s.magLen * 0.6f, s.magDepth}, s.metal, 0.12f));
        p.push_back(box({0, magTop - s.magLen * 0.78f + 0.02f, magZ - 0.03f}, {s.magW, s.magLen * 0.45f, s.magDepth}, s.metal, 0.45f));
        break;
    case MagStyle::Box:
        p.push_back(box({0.01f, magTop - 0.05f, magZ}, {0.08f, 0.10f, 0.12f}, kOdGreen));
        break;
    case MagStyle::Drum:
        p.push_back(cyl({0, magTop - 0.06f, magZ}, {0.11f, 0.11f, 0.05f}, s.metal));
        break;
    default: break;
    }

    // Handguard / fore-end
    float hgMid = recFront - s.handguardLen * 0.5f;
    float hgFront = recFront - s.handguardLen;
    float chamberZ = recFront + (bullpup ? s.receiverLen * 0.55f : 0.03f);
    float barrelEnd = chamberZ - s.barrelLen;
    float muzzleZ = barrelEnd - (s.suppressor ? s.suppressorLen : 0.0f);

    if (s.pump) {
        p.push_back(box({0, -0.030f, hgMid}, {0.050f, 0.050f, s.handguardLen * 0.8f}, s.furniture, 0, Part::Action));
    } else if (s.handguardLen > 0.0f) {
        p.push_back(box({0, (s.stock == StockStyle::Wood ? -0.020f : 0.004f), hgMid}, {s.handguardW, 0.058f, s.handguardLen},
                        s.furniture));
    }
    if (s.mag == MagStyle::Tube)
        p.push_back(cyl({0, -0.028f, (recFront + barrelEnd) * 0.5f + 0.03f}, {0.024f, 0.024f, (recFront - barrelEnd) * 0.85f}, s.metal));

    // Barrel(s)
    float blen = recFront - barrelEnd;
    if (s.twinBarrel) {
        p.push_back(cyl({-0.012f, 0, recFront - blen * 0.5f}, {0.022f, 0.022f, blen}, s.metal));
        p.push_back(cyl({0.012f, 0, recFront - blen * 0.5f}, {0.022f, 0.022f, blen}, s.metal));
    } else {
        p.push_back(cyl({0, 0, recFront - blen * 0.5f}, {s.barrelR * 2.0f, s.barrelR * 2.0f, blen}, s.metal));
    }
    if (s.suppressor) p.push_back(cyl({0, 0, barrelEnd - s.suppressorLen * 0.5f}, {0.042f, 0.042f, s.suppressorLen}, kPolymer));
    else if (!s.twinBarrel) p.push_back(cyl({0, 0, barrelEnd - 0.015f}, {s.barrelR * 2.4f, s.barrelR * 2.4f, 0.03f}, s.metal));

    // Iron sights
    float frontZ = std::max(barrelEnd + 0.04f, hgFront + 0.03f);
    float rearZ;
    if (s.sight == SightStyle::Peep) {
        rearZ = bullpup ? recRear - 0.17f : recRear - 0.035f;
        addPeep(p, rearZ, s.sightH, railTop);
        addPost(p, frontZ, s.sightH, 0.008f);
    } else {
        rearZ = recFront + 0.05f;
        addNotch(p, rearZ, s.sightH, railTop);
        addPost(p, frontZ, s.sightH, 0.010f);
    }

    // Hands
    p.push_back(box({0, -0.07f, gripZ + 0.005f}, {0.050f, 0.070f, 0.065f}, kGlove, -0.35f, Part::Hand));
    float leftZ = s.pump ? hgMid : (s.handguardLen > 0.0f ? hgMid - s.handguardLen * 0.1f : recFront + 0.05f);
    p.push_back(box({-0.004f, -0.018f, leftZ}, {0.064f, 0.062f, 0.095f}, kGlove, 0, s.pump ? (Part::Hand | Part::Action) : Part::Hand));

    w.muzzle = {0, 0, muzzleZ};
    w.chamber = {0, 0, chamberZ};
    w.ejectionPort = {0.03f, 0.012f, chamberZ - 0.02f};
    w.ironSightHeight = s.sightH;
    w.ironEyeZ = bullpup ? -0.08f : std::min(-0.10f, rearZ + (s.sight == SightStyle::Peep ? 0.08f : 0.26f));
    w.length = -muzzleZ;
    w.railTopY = railTop;
    w.railZ = bullpup ? recMid - 0.03f : recMid;
    w.rightHand = {0.02f, -0.08f, gripZ + 0.015f};
    w.leftHand = {-0.02f, -0.03f, leftZ};
    w.leftHandOnAction = s.pump;
}

struct PistolShape {
    float slideLen = 0.19f, slideH = 0.030f;
    glm::vec3 slide = kMetal, frame = kPolymer;
    bool revolver = false;
};

void buildPistol(WeaponDef& w, const PistolShape& s) {
    auto& p = w.parts;
    p.clear();
    float L = s.slideLen;
    if (s.revolver) {
        p.push_back(cyl({0, 0.004f, -0.06f - L * 0.5f}, {0.020f, 0.020f, L}, s.slide));          // barrel
        p.push_back(box({0, 0.016f, -0.06f - L * 0.5f}, {0.012f, 0.010f, L}, s.slide));          // rib
        p.push_back(cyl({0, -0.012f, -0.035f}, {0.042f, 0.042f, 0.045f}, s.slide));               // cylinder
        p.push_back(box({0, -0.004f, -0.025f}, {0.024f, 0.040f, 0.085f}, s.slide));               // frame
    } else {
        p.push_back(box({0, 0.012f, -L * 0.5f}, {0.028f, s.slideH, L}, s.slide, 0, Part::Action));
        p.push_back(box({0, -0.012f, -L * 0.53f}, {0.026f, 0.022f, L * 0.9f}, s.frame));
    }
    p.push_back(box({0, -0.060f, -0.035f}, {0.030f, 0.100f, 0.048f}, s.frame, -0.30f));
    float sightY = 0.032f;
    addNotch(p, -0.012f, sightY, 0.027f, s.revolver ? Part::RearSight : (Part::RearSight | Part::Action));
    p.push_back(box({0, 0.0295f, -L + 0.01f}, {0.0035f, 0.005f, 0.006f}, kPolymer, 0, s.revolver ? 0 : Part::Action));
    p.push_back(box({0, -0.055f, -0.035f}, {0.055f, 0.080f, 0.070f}, kGlove, -0.30f, Part::Hand));
    p.push_back(box({-0.020f, -0.065f, -0.045f}, {0.030f, 0.070f, 0.075f}, kGlove, -0.30f, Part::Hand));

    float muzzleZ = s.revolver ? -0.06f - L : -L;
    w.muzzle = {0, 0, muzzleZ};
    w.chamber = {0, 0, s.revolver ? -0.04f : -0.06f};
    w.ejectionPort = {0.016f, 0.015f, -0.07f};
    w.ironSightHeight = sightY;
    w.ironEyeZ = 0.62f;
    w.hipOffset = {0.04f, -0.24f, -0.50f};
    w.reloadOffset = {0.02f, -0.25f, -0.30f};
    w.length = -muzzleZ;
    w.wallBack = 0.55f;
    w.railTopY = 0.027f;
    w.railZ = -0.05f;
    w.rightHand = {0.0f, -0.07f, -0.03f};
    w.leftHand = {-0.02f, -0.07f, -0.04f};
    w.rightElbowFromHand = {0.12f, -0.17f, 0.26f};
    w.leftElbowFromHand = {-0.15f, -0.17f, 0.26f};
    w.forearmThickness = 0.06f;
}

WeaponDef pistolBase(const char* id, const char* name, int cal, uint32_t mask, float vel, BallisticParams bp) {
    WeaponDef w;
    w.id = id;
    w.name = name;
    w.cls = WeaponClass::Pistol;
    w.caliber = cal;
    w.magMask = mask;
    w.projectile = bp;
    w.projectile.muzzleVelocity = vel;
    w.dispersionMoa = 3.0f;
    w.defaultZero = 25.0f;
    w.cyclicRpm = 450.0f;
    w.actionTravel = 0.025f;
    w.reloadTime = 1.7f;
    w.emptyReloadTime = 2.1f;
    w.casing = CasingType::Pistol;
    w.recoilRise = 3.4f;
    w.recoilYaw = 0.6f;
    w.recoilBack = 0.35f;
    w.flashScale = 0.7f;
    w.smokeScale = 0.5f;
    w.armSpeedScale = 1.35f;
    w.armAccelScale = 1.9f;
    w.swayScale = 1.6f;
    w.adsTime = 0.20f;
    w.maxOptic = 1;
    w.gridW = 2;
    w.gridH = 1;
    w.loudness = 157.0f;
    w.oneHanded = true;
    return w;
}

WeaponDef longBase(const char* id, const char* name, WeaponClass cls, int cal, uint32_t mask, BallisticParams bp) {
    WeaponDef w;
    w.id = id;
    w.name = name;
    w.cls = cls;
    w.caliber = cal;
    w.magMask = mask;
    w.projectile = bp;
    w.maxOptic = 5;
    w.loudness = 162.0f;
    return w;
}

const BallisticParams kBp556{910.0f, 0.00356f, 0.0057f, 0.25f};
const BallisticParams kBp545{880.0f, 0.00343f, 0.0056f, 0.24f};
const BallisticParams kBp762x39{715.0f, 0.0079f, 0.0079f, 0.30f};
const BallisticParams kBp300{670.0f, 0.0081f, 0.0078f, 0.27f};
const BallisticParams kBp308{800.0f, 0.0113f, 0.00782f, 0.26f};
const BallisticParams kBp54R{820.0f, 0.0096f, 0.0079f, 0.27f};
const BallisticParams kBp338{900.0f, 0.0162f, 0.0086f, 0.23f};
const BallisticParams kBp45{260.0f, 0.0149f, 0.0115f, 0.30f};
const BallisticParams kBp10{400.0f, 0.0117f, 0.0102f, 0.30f};
const BallisticParams kBp357{430.0f, 0.0102f, 0.0091f, 0.30f};
const BallisticParams kBp46{720.0f, 0.0020f, 0.0046f, 0.24f};
const BallisticParams kBpBuck{400.0f, 0.0035f, 0.0084f, 0.47f};
const BallisticParams kBp57{716.0f, 0.0020f, 0.0057f, 0.25f};
const BallisticParams kBp44{440.0f, 0.0156f, 0.0109f, 0.30f};
const BallisticParams kBp65{820.0f, 0.0093f, 0.0067f, 0.21f};
const BallisticParams kBp50{880.0f, 0.0420f, 0.0130f, 0.22f};

std::vector<WeaponDef> buildCatalogue() {
    std::vector<WeaponDef> v = {makePistol(), makeSmg(), makeShotgun(), makeRifle(), makeSniper()};

    // ---------- Pistols
    {
        WeaponDef w = pistolBase("wren9", "Wren 9 Compact", Cal_9x19, magBit(Mag_Pistol9), 340.0f, nineMm(340.0f));
        w.dispersionMoa = 3.6f;
        w.recoilRise = 3.8f;
        w.armSpeedScale = 1.45f;
        w.value = 350;
        buildPistol(w, {0.165f, 0.028f, kMetal, kTan});
        v.push_back(w);
    }
    {
        WeaponDef w = pistolBase("harlan45", "Harlan .45", Cal_45ACP, magBit(Mag_Pistol45), 255.0f, kBp45);
        w.magazine = 8;
        w.recoilRise = 4.2f;
        w.value = 600;
        w.tier = 1;
        w.loudness = 156.0f;
        buildPistol(w, {0.20f, 0.032f, kSteelBlue, kWalnut});
        v.push_back(w);
    }
    {
        WeaponDef w = pistolBase("kestrel10", "Kestrel 10 Longslide", Cal_10mm, magBit(Mag_Pistol10), 400.0f, kBp10);
        w.dispersionMoa = 2.4f;
        w.recoilRise = 4.4f;
        w.value = 1100;
        w.tier = 2;
        w.loudness = 159.0f;
        buildPistol(w, {0.23f, 0.031f, kMetalLight, kPolymer});
        v.push_back(w);
    }
    {
        WeaponDef w = pistolBase("corvid357", "Corvid .357 Revolver", Cal_357, 0, 430.0f, kBp357);
        w.reload = ReloadType::Tube;
        w.magazine = 6;
        w.chamberSeparate = false;
        w.ejects = false;
        w.reloadTime = 0.55f;
        w.emptyReloadTime = 0.9f; // swing out and dump the empties
        w.casing = CasingType::Magnum;
        w.cyclicRpm = 220.0f;
        w.recoilRise = 5.0f;
        w.dispersionMoa = 2.2f;
        w.actionTravel = 0.0f;
        w.value = 900;
        w.tier = 1;
        w.loudness = 162.0f;
        w.maxOptic = 0;
        buildPistol(w, {0.13f, 0.03f, kMetalLight, kWalnut, true});
        v.push_back(w);
    }

    // ---------- SMGs
    {
        WeaponDef w = longBase("bulldog45", "Bulldog .45 SMG", WeaponClass::Smg, Cal_45ACP, magBit(Mag_Smg45), kBp45);
        w.projectile.muzzleVelocity = 280.0f;
        w.dispersionMoa = 3.0f;
        w.defaultZero = 50.0f;
        w.canAuto = true;
        w.cyclicRpm = 650.0f;
        w.magazine = 25;
        w.reloadTime = 2.2f;
        w.emptyReloadTime = 2.7f;
        w.casing = CasingType::Pistol;
        w.recoilRise = 1.05f;
        w.recoilBack = 0.3f;
        w.armSpeedScale = 1.1f;
        w.armAccelScale = 1.25f;
        w.adsTime = 0.22f;
        w.maxOptic = 3;
        w.value = 1300;
        w.gridW = 3;
        w.loudness = 156.0f;
        GunShape s;
        s.stockLen = 0.20f;
        s.stock = StockStyle::Skeleton;
        s.receiverLen = 0.26f;
        s.handguardLen = 0.10f;
        s.barrelLen = 0.25f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.17f;
        s.magW = 0.025f;
        s.magDepth = 0.035f;
        s.sightH = 0.060f;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("needle46", "Needle 4.6 PDW", WeaponClass::Smg, Cal_46x30, magBit(Mag_Pdw46), kBp46);
        w.dispersionMoa = 2.0f;
        w.defaultZero = 50.0f;
        w.canAuto = true;
        w.cyclicRpm = 900.0f;
        w.magazine = 40;
        w.reloadTime = 2.2f;
        w.emptyReloadTime = 2.7f;
        w.casing = CasingType::Pistol;
        w.recoilRise = 0.6f;
        w.recoilBack = 0.2f;
        w.armSpeedScale = 1.2f;
        w.armAccelScale = 1.4f;
        w.adsTime = 0.20f;
        w.maxOptic = 3;
        w.value = 2600;
        w.tier = 2;
        w.gridW = 3;
        w.loudness = 158.0f;
        GunShape s;
        s.stockLen = 0.18f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.12f;
        s.barrelLen = 0.24f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.20f;
        s.magW = 0.022f;
        s.magDepth = 0.030f;
        s.furniture = kTan;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("whisper9", "Whisper 9SD", WeaponClass::Smg, Cal_9x19, magBit(Mag_Smg9), nineMm(300.0f));
        w.dispersionMoa = 2.5f;
        w.defaultZero = 50.0f;
        w.canAuto = true;
        w.cyclicRpm = 700.0f;
        w.magazine = 30;
        w.reloadTime = 2.1f;
        w.emptyReloadTime = 2.6f;
        w.casing = CasingType::Pistol;
        w.recoilRise = 0.7f;
        w.recoilBack = 0.22f;
        w.armSpeedScale = 1.05f;
        w.armAccelScale = 1.2f;
        w.flashScale = 0.1f;
        w.smokeScale = 0.4f;
        w.suppressed = true;
        w.loudness = 128.0f;
        w.maxOptic = 3;
        w.value = 3000;
        w.tier = 2;
        w.gridW = 4;
        GunShape s;
        s.stockLen = 0.20f;
        s.receiverLen = 0.26f;
        s.handguardLen = 0.05f;
        s.barrelLen = 0.12f;
        s.suppressor = true;
        s.suppressorLen = 0.20f;
        s.mag = MagStyle::Curved;
        s.magLen = 0.16f;
        s.magW = 0.024f;
        s.magDepth = 0.035f;
        buildLongGun(w, s);
        v.push_back(w);
    }

    // ---------- Shotguns
    auto shotgunBase = [](const char* id, const char* name) {
        WeaponDef w = longBase(id, name, WeaponClass::Shotgun, Cal_12ga, 0, kBpBuck);
        w.pellets = 9;
        w.pelletSpread = 0.016f;
        w.dispersionMoa = 4.0f;
        w.defaultZero = 25.0f;
        w.reload = ReloadType::Tube;
        w.reloadTime = 0.5f;
        w.emptyReloadTime = 0.45f;
        w.casing = CasingType::Shell;
        w.recoilRise = 3.0f;
        w.recoilYaw = 0.5f;
        w.recoilBack = 1.1f;
        w.flashScale = 1.5f;
        w.smokeScale = 2.0f;
        w.armSpeedScale = 0.95f;
        w.armAccelScale = 0.85f;
        w.adsTime = 0.28f;
        w.maxOptic = 3;
        w.loudness = 161.0f;
        w.gridW = 5;
        w.gridH = 1;
        return w;
    };
    {
        WeaponDef w = shotgunBase("torrent12", "Torrent 12 Auto");
        w.action = ActionType::SemiAuto;
        w.cyclicRpm = 330.0f;
        w.actionTravel = 0.05f;
        w.magazine = 5;
        w.value = 2400;
        w.tier = 2;
        GunShape s;
        s.stockLen = 0.32f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.22f;
        s.barrelLen = 0.52f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Tube;
        s.sight = SightStyle::Notch;
        s.sightH = 0.030f;
        s.furniture = kPolymer;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = shotgunBase("oldmill12", "Old Mill Double");
        w.action = ActionType::SemiAuto; // two triggers: one barrel each
        w.cyclicRpm = 400.0f;
        w.actionTravel = 0.0f;
        w.magazine = 2;
        w.chamberSeparate = false;
        w.ejects = false;
        w.reloadTime = 0.6f;
        w.emptyReloadTime = 0.5f; // break open, extract
        w.pelletSpread = 0.020f;
        w.value = 700;
        w.tier = 0;
        w.maxOptic = 0;
        w.loudness = 162.0f;
        GunShape s;
        s.stock = StockStyle::Wood;
        s.stockLen = 0.36f;
        s.receiverLen = 0.10f;
        s.handguardLen = 0.25f;
        s.handguardW = 0.06f;
        s.barrelLen = 0.68f;
        s.twinBarrel = true;
        s.mag = MagStyle::None;
        s.sight = SightStyle::Notch;
        s.sightH = 0.028f;
        s.furniture = kBirch;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = shotgunBase("shorty12", "Brennan 12 Shorty");
        w.action = ActionType::Pump;
        w.cyclicRpm = 300.0f;
        w.actionCycleTime = 0.5f;
        w.actionTravel = 0.08f;
        w.magazine = 4;
        w.pelletSpread = 0.022f;
        w.armSpeedScale = 1.05f;
        w.armAccelScale = 1.0f;
        w.value = 750;
        w.gridW = 4;
        GunShape s;
        s.stockLen = 0.0f;
        s.stock = StockStyle::None;
        s.receiverLen = 0.20f;
        s.handguardLen = 0.16f;
        s.barrelLen = 0.34f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Tube;
        s.pump = true;
        s.sight = SightStyle::Notch;
        s.sightH = 0.030f;
        buildLongGun(w, s);
        w.hipOffset = {0.15f, -0.25f, -0.10f};
        w.ironEyeZ = 0.25f; // no stock: held out in front of the face
        v.push_back(w);
    }

    // ---------- Rifles
    auto rifleCommon = [](WeaponDef& w) {
        w.canAuto = true;
        w.reloadTime = 2.3f;
        w.emptyReloadTime = 2.9f;
        w.casing = CasingType::Rifle;
        w.magazine = 30;
    };
    {
        WeaponDef w = longBase("talon556", "Talon 5.56 Bullpup", WeaponClass::Rifle, Cal_556, magBit(Mag_Stanag), kBp556);
        rifleCommon(w);
        w.projectile.muzzleVelocity = 930.0f;
        w.cyclicRpm = 800.0f;
        w.recoilRise = 1.15f;
        w.armSpeedScale = 1.08f;
        w.armAccelScale = 1.1f;
        w.value = 4200;
        w.tier = 2;
        GunShape s;
        s.stock = StockStyle::Bullpup;
        s.receiverLen = 0.52f;
        s.handguardLen = 0.16f;
        s.barrelLen = 0.56f;
        s.mag = MagStyle::Curved;
        s.furniture = kOdGreen;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("volk545", "Volk 5.45", WeaponClass::Rifle, Cal_545, magBit(Mag_545), kBp545);
        rifleCommon(w);
        w.cyclicRpm = 650.0f;
        w.recoilRise = 1.05f;
        w.recoilYaw = 0.25f;
        w.value = 2600;
        w.tier = 1;
        GunShape s;
        s.stockLen = 0.25f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.20f;
        s.barrelLen = 0.42f;
        s.mag = MagStyle::Curved;
        s.magLen = 0.17f;
        s.sight = SightStyle::Notch;
        s.sightH = 0.050f;
        s.furniture = kPlum;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("volk762", "Volk 7.62", WeaponClass::Rifle, Cal_762x39, magBit(Mag_762x39), kBp762x39);
        rifleCommon(w);
        w.cyclicRpm = 600.0f;
        w.recoilRise = 1.6f;
        w.recoilYaw = 0.40f;
        w.defaultZero = 100.0f;
        w.dispersionMoa = 2.5f;
        w.value = 2200;
        w.tier = 1;
        w.loudness = 162.0f;
        GunShape s;
        s.stock = StockStyle::Wood;
        s.stockLen = 0.26f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.20f;
        s.barrelLen = 0.42f;
        s.mag = MagStyle::Curved;
        s.magLen = 0.20f;
        s.magDepth = 0.075f;
        s.sight = SightStyle::Notch;
        s.sightH = 0.050f;
        s.furniture = kWalnut;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("shade300", "Shade .300 SBR", WeaponClass::Rifle, Cal_300BLK, magBit(Mag_Stanag), kBp300);
        rifleCommon(w);
        w.cyclicRpm = 750.0f;
        w.recoilRise = 1.2f;
        w.suppressed = true;
        w.loudness = 135.0f;
        w.flashScale = 0.15f;
        w.smokeScale = 0.6f;
        w.defaultZero = 50.0f;
        w.armSpeedScale = 1.05f;
        w.value = 5200;
        w.tier = 3;
        GunShape s;
        s.stockLen = 0.22f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.18f;
        s.barrelLen = 0.25f;
        s.suppressor = true;
        s.mag = MagStyle::Curved;
        s.furniture = kTan;
        buildLongGun(w, s);
        v.push_back(w);
    }

    // ---------- Battle rifles / marksman rifles
    {
        WeaponDef w = longBase("ironwood308", "Ironwood 7.62 Battle Rifle", WeaponClass::Rifle, Cal_762x51, magBit(Mag_308), kBp308);
        w.canAuto = true;
        w.cyclicRpm = 600.0f;
        w.magazine = 20;
        w.reloadTime = 2.5f;
        w.emptyReloadTime = 3.1f;
        w.casing = CasingType::Magnum;
        w.recoilRise = 2.1f;
        w.recoilYaw = 0.45f;
        w.recoilBack = 0.8f;
        w.flashScale = 1.3f;
        w.smokeScale = 1.4f;
        w.armSpeedScale = 0.85f;
        w.armAccelScale = 0.75f;
        w.adsTime = 0.32f;
        w.dispersionMoa = 1.5f;
        w.value = 5500;
        w.tier = 2;
        w.gridW = 5;
        w.loudness = 166.0f;
        GunShape s;
        s.stock = StockStyle::Wood;
        s.stockLen = 0.28f;
        s.receiverLen = 0.26f;
        s.handguardLen = 0.24f;
        s.barrelLen = 0.53f;
        s.barrelR = 0.010f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.12f;
        s.magW = 0.030f;
        s.magDepth = 0.085f;
        s.furniture = kOdGreen;
        s.sightH = 0.055f;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("sentinel308", "Sentinel 7.62 DMR", WeaponClass::Marksman, Cal_762x51, magBit(Mag_308), kBp308);
        w.projectile.muzzleVelocity = 820.0f;
        w.cyclicRpm = 400.0f;
        w.magazine = 20;
        w.reloadTime = 2.5f;
        w.emptyReloadTime = 3.1f;
        w.casing = CasingType::Magnum;
        w.recoilRise = 1.7f;
        w.recoilBack = 0.75f;
        w.flashScale = 1.2f;
        w.smokeScale = 1.4f;
        w.armSpeedScale = 0.82f;
        w.armAccelScale = 0.7f;
        w.swayScale = 0.85f;
        w.adsTime = 0.34f;
        w.dispersionMoa = 0.8f;
        w.value = 7800;
        w.tier = 3;
        w.gridW = 5;
        w.loudness = 166.0f;
        GunShape s;
        s.stockLen = 0.27f;
        s.receiverLen = 0.26f;
        s.handguardLen = 0.32f;
        s.barrelLen = 0.58f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.12f;
        s.magW = 0.030f;
        s.magDepth = 0.085f;
        s.furniture = kTan;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("taiga54", "Taiga 7.62R Marksman", WeaponClass::Marksman, Cal_762x54R, magBit(Mag_54R), kBp54R);
        w.cyclicRpm = 350.0f;
        w.magazine = 10;
        w.reloadTime = 2.6f;
        w.emptyReloadTime = 3.2f;
        w.casing = CasingType::Magnum;
        w.recoilRise = 1.9f;
        w.recoilBack = 0.8f;
        w.flashScale = 1.3f;
        w.smokeScale = 1.5f;
        w.armSpeedScale = 0.78f;
        w.armAccelScale = 0.65f;
        w.swayScale = 0.85f;
        w.adsTime = 0.36f;
        w.dispersionMoa = 1.1f;
        w.value = 6200;
        w.tier = 2;
        w.gridW = 6;
        w.loudness = 166.0f;
        GunShape s;
        s.stock = StockStyle::Skeleton;
        s.stockLen = 0.30f;
        s.receiverLen = 0.26f;
        s.handguardLen = 0.28f;
        s.barrelLen = 0.62f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.11f;
        s.magW = 0.03f;
        s.magDepth = 0.09f;
        s.sight = SightStyle::Notch;
        s.sightH = 0.050f;
        s.furniture = kWalnut;
        buildLongGun(w, s);
        v.push_back(w);
    }

    // ---------- Bolt actions
    {
        WeaponDef w = makeSniper();
        w.id = "anvil338";
        w.name = "Anvil .338 Magnum";
        w.caliber = Cal_338;
        w.projectile = kBp338;
        w.dispersionMoa = 0.4f;
        w.recoilRise = 2.1f;
        w.recoilBack = 1.2f;
        w.flashScale = 1.6f;
        w.smokeScale = 2.0f;
        w.armSpeedScale = 0.65f;
        w.armAccelScale = 0.5f;
        w.swayScale = 0.7f;
        w.loudness = 170.0f;
        w.value = 11000;
        w.tier = 3;
        w.gridW = 6;
        for (Part& p : w.parts) {
            if (p.color == kOdGreen) p.color = kTan;
        }
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("pioneer308", "Pioneer .308 Bolt", WeaponClass::Sniper, Cal_762x51, magBit(Mag_308Bolt), kBp308);
        w.action = ActionType::Bolt;
        w.cyclicRpm = 60.0f;
        w.actionCycleTime = 0.9f;
        w.actionTravel = 0.08f;
        w.magazine = 5;
        w.reloadTime = 2.4f;
        w.emptyReloadTime = 2.8f;
        w.casing = CasingType::Magnum;
        w.dispersionMoa = 0.7f;
        w.recoilRise = 1.6f;
        w.recoilBack = 0.9f;
        w.flashScale = 1.3f;
        w.smokeScale = 1.6f;
        w.armSpeedScale = 0.78f;
        w.armAccelScale = 0.62f;
        w.swayScale = 0.78f;
        w.adsTime = 0.36f;
        w.value = 3800;
        w.tier = 1;
        w.gridW = 5;
        w.loudness = 166.0f;
        GunShape s;
        s.stock = StockStyle::Wood;
        s.stockLen = 0.34f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.30f;
        s.handguardW = 0.06f;
        s.barrelLen = 0.60f;
        s.barrelR = 0.010f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.06f;
        s.magW = 0.03f;
        s.magDepth = 0.08f;
        s.sight = SightStyle::Notch;
        s.sightH = 0.050f;
        s.furniture = kBirch;
        buildLongGun(w, s);
        // The bolt handle slides with the action.
        w.parts.push_back(box({0.035f, 0.010f, w.chamber.z + 0.05f}, {0.045f, 0.010f, 0.010f}, kMetal, 0, Part::Action));
        w.parts.push_back(box({0.058f, 0.010f, w.chamber.z + 0.05f}, {0.016f, 0.016f, 0.016f}, kPolymer, 0, Part::Action));
        v.push_back(w);
    }

    // ---------- Light machine gun
    {
        WeaponDef w = longBase("hammer556", "Hammer 5.56 LMG", WeaponClass::Lmg, Cal_556, magBit(Mag_Box556) | magBit(Mag_Stanag), kBp556);
        w.projectile.muzzleVelocity = 915.0f;
        w.canAuto = true;
        w.cyclicRpm = 750.0f;
        w.magazine = 100;
        w.reloadTime = 4.5f;
        w.emptyReloadTime = 5.2f;
        w.recoilRise = 0.95f;
        w.recoilBack = 0.45f;
        w.armSpeedScale = 0.62f;
        w.armAccelScale = 0.5f;
        w.swayScale = 0.95f;
        w.adsTime = 0.42f;
        w.dispersionMoa = 2.0f;
        w.value = 9000;
        w.tier = 3;
        w.gridW = 5;
        w.loudness = 162.0f;
        GunShape s;
        s.stockLen = 0.26f;
        s.receiverLen = 0.30f;
        s.receiverH = 0.07f;
        s.handguardLen = 0.22f;
        s.handguardW = 0.06f;
        s.barrelLen = 0.52f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Box;
        s.furniture = kPolymer;
        buildLongGun(w, s);
        v.push_back(w);
    }
#include "game/WeaponCatalog2.inl"
    return v;
}

} // namespace

const std::vector<WeaponDef>& weaponDefs() {
    static const std::vector<WeaponDef> defs = buildCatalogue();
    return defs;
}

int findWeapon(const std::string& id) {
    const auto& d = weaponDefs();
    for (int i = 0; i < int(d.size()); ++i)
        if (d[i].id == id) return i;
    return -1;
}

const char* caliberName(int c) {
    static const char* names[kCaliberCount] = {"9x19mm", ".45 ACP", "10mm", ".357 Mag", "4.6x30mm", "5.56x45mm", "5.45x39mm",
                                               "7.62x39mm", ".300 BLK", "7.62x51mm", "7.62x54R", ".338 Magnum", "12 gauge",
                                               "5.7x28mm", ".44 Magnum", "6.5mm", ".50 BMG"};
    return c >= 0 && c < kCaliberCount ? names[c] : "?";
}
