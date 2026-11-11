// Included inside buildCatalogue() (WeaponCatalog.inl): the third batch of guns, built with
// attachments in mind (threads, rails, stocks). Rarity in loot comes from value.

    // ---------- Pistols
    {
        WeaponDef w = pistolBase("finch9", "Finch 9 Micro", Cal_9x19, magBit(Mag_Pistol9), 330.0f, nineMm(330.0f));
        w.dispersionMoa = 4.0f;
        w.recoilRise = 4.0f;
        w.armSpeedScale = 1.5f;
        w.value = 350;
        buildPistol(w, {0.15f, 0.027f, kMetalLight, kPolymer});
        v.push_back(w);
    }
    {
        WeaponDef w = pistolBase("vanta9ls", "Vanta 9 Long-slide", Cal_9x19, magBit(Mag_Pistol9), 390.0f, nineMm(390.0f));
        w.dispersionMoa = 2.2f;
        w.recoilRise = 3.0f;
        w.armSpeedScale = 1.25f;
        w.swayScale = 1.4f;
        w.value = 950;
        w.tier = 1;
        buildPistol(w, {0.23f, 0.030f, kMetal, kTan});
        v.push_back(w);
    }

    // ---------- Carbines, shotguns
    {
        WeaponDef w = longBase("marlowe44", "Marlowe .44 Lever Carbine", WeaponClass::Rifle, Cal_44Mag, 0, kBp44);
        w.projectile.muzzleVelocity = 530.0f;
        w.action = ActionType::Pump; // lever: the hand works the action each shot
        w.actionCycleTime = 0.45f;
        w.actionTravel = 0.0f;
        w.reload = ReloadType::Tube;
        w.magazine = 9;
        w.reloadTime = 0.55f;
        w.emptyReloadTime = 0.4f;
        w.casing = CasingType::Magnum;
        w.dispersionMoa = 2.5f;
        w.defaultZero = 50.0f;
        w.recoilRise = 1.6f;
        w.recoilBack = 0.5f;
        w.maxOptic = 3;
        w.value = 2100;
        w.tier = 1;
        w.gridW = 4;
        w.loudness = 161.0f;
        GunShape s;
        s.stock = StockStyle::Wood;
        s.stockLen = 0.30f;
        s.receiverLen = 0.16f;
        s.handguardLen = 0.20f;
        s.barrelLen = 0.46f;
        s.mag = MagStyle::Tube;
        s.sight = SightStyle::Notch;
        s.furniture = kWalnut;
        s.metal = kSteelBlue;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("brennan12s", "Brennan 12 Semi", WeaponClass::Shotgun, Cal_12ga, 0, kBpBuck);
        w.pellets = 9;
        w.pelletSpread = 0.024f;
        w.dispersionMoa = 4.0f;
        w.defaultZero = 25.0f;
        w.reload = ReloadType::Tube;
        w.magazine = 6;
        w.reloadTime = 0.5f;
        w.emptyReloadTime = 0.6f;
        w.casing = CasingType::Shell;
        w.cyclicRpm = 300.0f;
        w.recoilRise = 2.0f;
        w.recoilBack = 0.8f;
        w.maxOptic = 3;
        w.value = 2800;
        w.tier = 1;
        w.loudness = 161.0f;
        GunShape s;
        s.stockLen = 0.30f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.24f;
        s.barrelLen = 0.48f;
        s.barrelR = 0.012f;
        s.mag = MagStyle::Tube;
        s.sight = SightStyle::Notch;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("hornet9sd", "Hornet 9 Integral SD", WeaponClass::Smg, Cal_9x19, magBit(Mag_Smg9), nineMm(300.0f));
        w.dispersionMoa = 2.2f;
        w.defaultZero = 50.0f;
        w.canAuto = true;
        w.cyclicRpm = 800.0f;
        w.magazine = 30;
        w.reloadTime = 2.2f;
        w.emptyReloadTime = 2.7f;
        w.casing = CasingType::Pistol;
        w.recoilRise = 0.6f;
        w.recoilBack = 0.2f;
        w.suppressed = true;
        w.loudness = 128.0f;
        w.flashScale = 0.08f;
        w.maxOptic = 3;
        w.value = 3300;
        w.tier = 2;
        GunShape s;
        s.stockLen = 0.24f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.14f;
        s.barrelLen = 0.20f;
        s.suppressor = true;
        s.suppressorLen = 0.22f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.17f;
        s.magW = 0.024f;
        s.magDepth = 0.035f;
        buildLongGun(w, s);
        v.push_back(w);
    }

    // ---------- Rifles
    {
        WeaponDef w = longBase("ridgeline300sd", "Ridgeline .300 SD", WeaponClass::Rifle, Cal_300BLK, magBit(Mag_Stanag), kBp300);
        rifleCommon(w);
        w.projectile.muzzleVelocity = 660.0f;
        w.cyclicRpm = 750.0f;
        w.recoilRise = 1.0f;
        w.suppressed = true;
        w.loudness = 133.0f;
        w.flashScale = 0.06f;
        w.value = 5600;
        w.tier = 2;
        GunShape s;
        s.stockLen = 0.24f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.24f;
        s.barrelLen = 0.24f;
        s.suppressor = true;
        s.suppressorLen = 0.18f;
        s.mag = MagStyle::Curved;
        s.furniture = kTan;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("volk545lmg", "Volk 5.45 Light MG", WeaponClass::Lmg, Cal_545, magBit(Mag_545), kBp545);
        rifleCommon(w);
        w.projectile.muzzleVelocity = 960.0f;
        w.magazine = 45;
        w.cyclicRpm = 600.0f;
        w.recoilRise = 0.85f;
        w.armSpeedScale = 0.78f;
        w.armAccelScale = 0.68f;
        w.swayScale = 0.9f;
        w.dispersionMoa = 1.4f;
        w.value = 6800;
        w.tier = 2;
        w.gridW = 5;
        GunShape s;
        s.stock = StockStyle::Wood;
        s.stockLen = 0.27f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.22f;
        s.barrelLen = 0.58f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Curved;
        s.magLen = 0.22f;
        s.furniture = kBirch;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("corsair65bp", "Corsair 6.5 Bullpup", WeaponClass::Rifle, Cal_65, magBit(Mag_65), kBp65);
        rifleCommon(w);
        w.magazine = 20;
        w.projectile.muzzleVelocity = 800.0f;
        w.cyclicRpm = 620.0f;
        w.recoilRise = 1.35f;
        w.recoilBack = 0.6f;
        w.dispersionMoa = 1.0f;
        w.value = 7200;
        w.tier = 3;
        GunShape s;
        s.stock = StockStyle::Bullpup;
        s.receiverLen = 0.54f;
        s.handguardLen = 0.18f;
        s.barrelLen = 0.56f;
        s.mag = MagStyle::Straight;
        s.furniture = kPlum;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("harrow308", "Harrow 7.62 Battle Rifle", WeaponClass::Rifle, Cal_762x51, magBit(Mag_308), kBp308);
        rifleCommon(w);
        w.magazine = 20;
        w.projectile.muzzleVelocity = 820.0f;
        w.cyclicRpm = 650.0f;
        w.recoilRise = 1.9f;
        w.recoilBack = 0.75f;
        w.armSpeedScale = 0.85f;
        w.armAccelScale = 0.8f;
        w.dispersionMoa = 1.3f;
        w.value = 8400;
        w.tier = 3;
        w.gridW = 5;
        w.loudness = 164.0f;
        GunShape s;
        s.stockLen = 0.28f;
        s.receiverLen = 0.28f;
        s.receiverH = 0.065f;
        s.handguardLen = 0.28f;
        s.barrelLen = 0.52f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Straight;
        s.furniture = kOdGreen;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("anvil338sa", "Anvil .338 Semi-Auto DMR", WeaponClass::Marksman, Cal_338, magBit(Mag_338), kBp338);
        w.projectile.muzzleVelocity = 880.0f;
        w.magazine = 10;
        w.reloadTime = 2.8f;
        w.emptyReloadTime = 3.4f;
        w.casing = CasingType::Magnum;
        w.cyclicRpm = 300.0f;
        w.recoilRise = 2.6f;
        w.recoilBack = 1.0f;
        w.armSpeedScale = 0.72f;
        w.armAccelScale = 0.62f;
        w.swayScale = 0.85f;
        w.adsTime = 0.42f;
        w.dispersionMoa = 0.7f;
        w.defaultZero = 200.0f;
        w.value = 14000;
        w.tier = 3;
        w.gridW = 6;
        w.loudness = 168.0f;
        GunShape s;
        s.stockLen = 0.30f;
        s.receiverLen = 0.30f;
        s.receiverH = 0.07f;
        s.handguardLen = 0.30f;
        s.barrelLen = 0.62f;
        s.barrelR = 0.012f;
        s.mag = MagStyle::Box;
        s.furniture = kTan;
        buildLongGun(w, s);
        v.push_back(w);
    }
