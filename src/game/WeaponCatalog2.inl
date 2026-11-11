// Included inside buildCatalogue() (WeaponCatalog.inl): the second batch of guns.
// Rarity in loot comes from value: the more a gun is worth, the less often it turns up.

    // ---------- Pistols
    {
        WeaponDef w = pistolBase("moth57", "Moth 5.7 Pistol", Cal_57x28, magBit(Mag_Pistol57), 625.0f, kBp57);
        w.magazine = 20;
        w.dispersionMoa = 2.6f;
        w.recoilRise = 2.6f;
        w.value = 1900;
        w.tier = 2;
        buildPistol(w, {0.21f, 0.030f, kPolymer, kTan});
        v.push_back(w);
    }
    {
        WeaponDef w = pistolBase("brute44", "Brute .44 Revolver", Cal_44Mag, 0, 440.0f, kBp44);
        w.reload = ReloadType::Tube;
        w.magazine = 6;
        w.chamberSeparate = false;
        w.ejects = false;
        w.reloadTime = 0.6f;
        w.emptyReloadTime = 0.9f;
        w.casing = CasingType::Magnum;
        w.cyclicRpm = 180.0f;
        w.recoilRise = 6.5f;
        w.recoilBack = 0.6f;
        w.dispersionMoa = 2.0f;
        w.actionTravel = 0.0f;
        w.maxOptic = 0;
        w.flashScale = 1.2f;
        w.loudness = 165.0f;
        w.value = 2300;
        w.tier = 2;
        buildPistol(w, {0.19f, 0.032f, kSteelBlue, kWalnut, true});
        v.push_back(w);
    }
    {
        WeaponDef w = pistolBase("vanta9auto", "Vanta 9 Auto", Cal_9x19, magBit(Mag_Pistol9), 355.0f, nineMm(355.0f));
        w.canAuto = true;
        w.cyclicRpm = 1100.0f;
        w.recoilRise = 3.0f;
        w.recoilYaw = 0.9f;
        w.value = 1300;
        w.tier = 1;
        buildPistol(w, {0.19f, 0.030f, kMetal, kOdGreen});
        v.push_back(w);
    }
    {
        WeaponDef w = pistolBase("harlan45c", "Harlan .45 Compact", Cal_45ACP, magBit(Mag_Pistol45), 240.0f, kBp45);
        w.magazine = 8;
        w.recoilRise = 4.8f;
        w.dispersionMoa = 3.6f;
        w.armSpeedScale = 1.45f;
        w.value = 520;
        buildPistol(w, {0.17f, 0.031f, kMetalLight, kPolymer});
        v.push_back(w);
    }
    {
        WeaponDef w = pistolBase("sparrow9", "Sparrow 9 Pocket", Cal_9x19, magBit(Mag_Pistol9), 320.0f, nineMm(320.0f));
        w.dispersionMoa = 4.5f;
        w.recoilRise = 4.2f;
        w.armSpeedScale = 1.5f;
        w.value = 300;
        buildPistol(w, {0.14f, 0.026f, kMetal, kPolymer});
        v.push_back(w);
    }

    // ---------- SMGs and carbines
    {
        WeaponDef w = longBase("moth57pdw", "Moth 5.7 PDW", WeaponClass::Smg, Cal_57x28, magBit(Mag_Pdw57), kBp57);
        w.dispersionMoa = 1.8f;
        w.defaultZero = 50.0f;
        w.canAuto = true;
        w.cyclicRpm = 900.0f;
        w.magazine = 50;
        w.reloadTime = 2.4f;
        w.emptyReloadTime = 2.9f;
        w.casing = CasingType::Pistol;
        w.recoilRise = 0.55f;
        w.recoilBack = 0.2f;
        w.armSpeedScale = 1.2f;
        w.armAccelScale = 1.4f;
        w.adsTime = 0.2f;
        w.maxOptic = 3;
        w.value = 4800;
        w.tier = 2;
        w.gridW = 3;
        w.loudness = 158.0f;
        GunShape s;
        s.stock = StockStyle::Bullpup;
        s.receiverLen = 0.40f;
        s.handguardLen = 0.10f;
        s.barrelLen = 0.30f;
        s.mag = MagStyle::None;
        buildLongGun(w, s);
        w.parts.push_back(box({0, 0.050f, -0.20f}, {0.03f, 0.012f, 0.24f}, kMetalLight)); // top-mounted magazine
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("hornet9c", "Hornet 9 Carbine", WeaponClass::Smg, Cal_9x19, magBit(Mag_Smg9), nineMm(450.0f));
        w.dispersionMoa = 1.8f;
        w.defaultZero = 50.0f;
        w.cyclicRpm = 600.0f;
        w.magazine = 30;
        w.reloadTime = 2.2f;
        w.emptyReloadTime = 2.7f;
        w.casing = CasingType::Pistol;
        w.recoilRise = 0.6f;
        w.recoilBack = 0.2f;
        w.maxOptic = 4;
        w.value = 1100;
        w.loudness = 156.0f;
        GunShape s;
        s.stockLen = 0.24f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.26f;
        s.barrelLen = 0.40f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.17f;
        s.magW = 0.024f;
        s.magDepth = 0.035f;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("bulldog45sd", "Bulldog .45 SD", WeaponClass::Smg, Cal_45ACP, magBit(Mag_Smg45), kBp45);
        w.projectile.muzzleVelocity = 270.0f;
        w.dispersionMoa = 3.0f;
        w.defaultZero = 50.0f;
        w.canAuto = true;
        w.cyclicRpm = 650.0f;
        w.magazine = 25;
        w.reloadTime = 2.2f;
        w.emptyReloadTime = 2.7f;
        w.casing = CasingType::Pistol;
        w.recoilRise = 0.9f;
        w.recoilBack = 0.25f;
        w.suppressed = true;
        w.loudness = 125.0f;
        w.flashScale = 0.1f;
        w.maxOptic = 3;
        w.value = 3600;
        w.tier = 2;
        GunShape s;
        s.stockLen = 0.20f;
        s.stock = StockStyle::Skeleton;
        s.receiverLen = 0.26f;
        s.handguardLen = 0.08f;
        s.barrelLen = 0.16f;
        s.suppressor = true;
        s.suppressorLen = 0.22f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.17f;
        s.magDepth = 0.035f;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("riveter9", "Riveter 9 SMG", WeaponClass::Smg, Cal_9x19, magBit(Mag_Smg9), nineMm(380.0f));
        w.dispersionMoa = 3.2f;
        w.defaultZero = 50.0f;
        w.canAuto = true;
        w.cyclicRpm = 1150.0f;
        w.magazine = 30;
        w.reloadTime = 2.0f;
        w.emptyReloadTime = 2.5f;
        w.casing = CasingType::Pistol;
        w.recoilRise = 0.9f;
        w.recoilYaw = 0.5f;
        w.armSpeedScale = 1.25f;
        w.armAccelScale = 1.5f;
        w.maxOptic = 1;
        w.value = 1700;
        w.tier = 1;
        w.gridW = 3;
        GunShape s;
        s.stockLen = 0.0f;
        s.stock = StockStyle::None;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.06f;
        s.barrelLen = 0.16f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.20f;
        s.magW = 0.022f;
        s.magDepth = 0.03f;
        s.furniture = kPlum;
        buildLongGun(w, s);
        w.hipOffset = {0.12f, -0.26f, -0.18f};
        w.ironEyeZ = 0.30f;
        v.push_back(w);
    }

    // ---------- Shotguns
    {
        WeaponDef w = shotgunBase("torrent12box", "Torrent 12 Box");
        w.reload = ReloadType::Magazine;
        w.magMask = magBit(Mag_12Box);
        w.action = ActionType::SemiAuto;
        w.cyclicRpm = 330.0f;
        w.actionTravel = 0.05f;
        w.magazine = 8;
        w.reloadTime = 2.6f;
        w.emptyReloadTime = 3.2f;
        w.value = 5200;
        w.tier = 3;
        GunShape s;
        s.stockLen = 0.26f;
        s.receiverLen = 0.28f;
        s.handguardLen = 0.24f;
        s.barrelLen = 0.48f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Box;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = shotgunBase("brennan12riot", "Brennan 12 Riot");
        w.action = ActionType::Pump;
        w.cyclicRpm = 300.0f;
        w.actionCycleTime = 0.55f;
        w.actionTravel = 0.09f;
        w.magazine = 8;
        w.value = 1300;
        w.tier = 1;
        GunShape s;
        s.stockLen = 0.32f;
        s.receiverLen = 0.20f;
        s.handguardLen = 0.20f;
        s.barrelLen = 0.50f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Tube;
        s.pump = true;
        s.sight = SightStyle::Notch;
        s.sightH = 0.030f;
        s.furniture = kOdGreen;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = shotgunBase("sawnoff12", "Sawn-off Double");
        w.action = ActionType::SemiAuto;
        w.cyclicRpm = 400.0f;
        w.actionTravel = 0.0f;
        w.magazine = 2;
        w.chamberSeparate = false;
        w.ejects = false;
        w.reloadTime = 0.6f;
        w.emptyReloadTime = 0.5f;
        w.pelletSpread = 0.035f;
        w.recoilRise = 4.0f;
        w.armSpeedScale = 1.15f;
        w.maxOptic = 0;
        w.value = 450;
        w.tier = 0;
        w.gridW = 3;
        GunShape s;
        s.stock = StockStyle::None;
        s.stockLen = 0.0f;
        s.receiverLen = 0.12f;
        s.handguardLen = 0.15f;
        s.barrelLen = 0.30f;
        s.twinBarrel = true;
        s.mag = MagStyle::None;
        s.sight = SightStyle::Notch;
        s.sightH = 0.028f;
        s.furniture = kBirch;
        buildLongGun(w, s);
        w.parts.push_back(box({0, -0.07f, -0.04f}, {0.035f, 0.11f, 0.05f}, kBirch, -0.4f)); // grip stub
        w.hipOffset = {0.13f, -0.26f, -0.15f};
        w.ironEyeZ = 0.30f;
        v.push_back(w);
    }

    // ---------- Rifles
    {
        WeaponDef w = longBase("ridgeline556sbr", "Ridgeline 5.56 SBR", WeaponClass::Rifle, Cal_556, magBit(Mag_Stanag), kBp556);
        rifleCommon(w);
        w.projectile.muzzleVelocity = 820.0f;
        w.cyclicRpm = 850.0f;
        w.recoilRise = 1.35f;
        w.armSpeedScale = 1.12f;
        w.armAccelScale = 1.15f;
        w.flashScale = 1.4f;
        w.value = 3700;
        w.tier = 1;
        w.loudness = 164.0f;
        GunShape s;
        s.stockLen = 0.20f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.16f;
        s.barrelLen = 0.30f;
        s.mag = MagStyle::Curved;
        s.furniture = kTan;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("ridgeline556spr", "Ridgeline 5.56 Marksman", WeaponClass::Marksman, Cal_556, magBit(Mag_Stanag), kBp556);
        w.projectile.muzzleVelocity = 960.0f;
        w.cyclicRpm = 500.0f;
        w.magazine = 20;
        w.reloadTime = 2.3f;
        w.emptyReloadTime = 2.9f;
        w.dispersionMoa = 0.7f;
        w.recoilRise = 1.0f;
        w.armSpeedScale = 0.9f;
        w.swayScale = 0.85f;
        w.adsTime = 0.3f;
        w.value = 6400;
        w.tier = 2;
        w.gridW = 5;
        GunShape s;
        s.stockLen = 0.27f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.34f;
        s.barrelLen = 0.55f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.11f;
        s.furniture = kOdGreen;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("volk545s", "Volk 5.45 Short", WeaponClass::Rifle, Cal_545, magBit(Mag_545), kBp545);
        rifleCommon(w);
        w.projectile.muzzleVelocity = 735.0f;
        w.cyclicRpm = 700.0f;
        w.recoilRise = 1.2f;
        w.armSpeedScale = 1.1f;
        w.flashScale = 1.5f;
        w.value = 2300;
        w.tier = 1;
        w.gridW = 3;
        GunShape s;
        s.stock = StockStyle::Skeleton;
        s.stockLen = 0.22f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.12f;
        s.barrelLen = 0.24f;
        s.mag = MagStyle::Curved;
        s.magLen = 0.17f;
        s.sight = SightStyle::Notch;
        s.sightH = 0.050f;
        s.furniture = kPlum;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("volk762f", "Volk 7.62 Folding", WeaponClass::Rifle, Cal_762x39, magBit(Mag_762x39), kBp762x39);
        rifleCommon(w);
        w.cyclicRpm = 600.0f;
        w.recoilRise = 1.75f;
        w.recoilYaw = 0.45f;
        w.dispersionMoa = 2.8f;
        w.value = 1900;
        w.tier = 0;
        GunShape s;
        s.stock = StockStyle::Skeleton;
        s.stockLen = 0.24f;
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
        WeaponDef w = longBase("talon308", "Talon 7.62 Bullpup", WeaponClass::Rifle, Cal_762x51, magBit(Mag_308), kBp308);
        w.canAuto = true;
        w.cyclicRpm = 650.0f;
        w.magazine = 20;
        w.reloadTime = 2.6f;
        w.emptyReloadTime = 3.2f;
        w.casing = CasingType::Magnum;
        w.recoilRise = 2.2f;
        w.recoilBack = 0.8f;
        w.dispersionMoa = 1.3f;
        w.armSpeedScale = 0.92f;
        w.armAccelScale = 0.85f;
        w.flashScale = 1.3f;
        w.value = 8600;
        w.tier = 3;
        w.loudness = 166.0f;
        GunShape s;
        s.stock = StockStyle::Bullpup;
        s.receiverLen = 0.56f;
        s.handguardLen = 0.20f;
        s.barrelLen = 0.50f;
        s.barrelR = 0.010f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.12f;
        s.magW = 0.03f;
        s.magDepth = 0.085f;
        s.furniture = kTan;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("corsair65", "Corsair 6.5 Rifle", WeaponClass::Rifle, Cal_65, magBit(Mag_65), kBp65);
        w.canAuto = true;
        w.cyclicRpm = 620.0f;
        w.magazine = 20;
        w.reloadTime = 2.4f;
        w.emptyReloadTime = 3.0f;
        w.casing = CasingType::Magnum;
        w.recoilRise = 1.6f;
        w.recoilBack = 0.65f;
        w.dispersionMoa = 1.0f;
        w.armSpeedScale = 0.95f;
        w.value = 7400;
        w.tier = 3;
        w.loudness = 165.0f;
        GunShape s;
        s.stockLen = 0.26f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.28f;
        s.barrelLen = 0.46f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.13f;
        s.magW = 0.029f;
        s.magDepth = 0.08f;
        s.furniture = kSteelBlue;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("wraith556", "Wraith 5.56 SD", WeaponClass::Rifle, Cal_556, magBit(Mag_Stanag), kBp556);
        rifleCommon(w);
        w.projectile.muzzleVelocity = 860.0f;
        w.cyclicRpm = 750.0f;
        w.recoilRise = 1.05f;
        w.suppressed = true;
        w.loudness = 140.0f;
        w.flashScale = 0.15f;
        w.smokeScale = 0.6f;
        w.value = 7900;
        w.tier = 3;
        w.gridW = 5;
        GunShape s;
        s.stockLen = 0.24f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.22f;
        s.barrelLen = 0.36f;
        s.suppressor = true;
        s.suppressorLen = 0.18f;
        s.mag = MagStyle::Curved;
        s.furniture = kOdGreen;
        buildLongGun(w, s);
        v.push_back(w);
    }

    // ---------- Marksman / bolt
    {
        WeaponDef w = longBase("sentinel65", "Sentinel 6.5 DMR", WeaponClass::Marksman, Cal_65, magBit(Mag_65), kBp65);
        w.projectile.muzzleVelocity = 850.0f;
        w.cyclicRpm = 400.0f;
        w.magazine = 20;
        w.reloadTime = 2.5f;
        w.emptyReloadTime = 3.1f;
        w.casing = CasingType::Magnum;
        w.recoilRise = 1.4f;
        w.recoilBack = 0.6f;
        w.dispersionMoa = 0.6f;
        w.armSpeedScale = 0.82f;
        w.armAccelScale = 0.72f;
        w.swayScale = 0.82f;
        w.adsTime = 0.34f;
        w.value = 9800;
        w.tier = 3;
        w.gridW = 5;
        w.loudness = 165.0f;
        GunShape s;
        s.stockLen = 0.28f;
        s.receiverLen = 0.25f;
        s.handguardLen = 0.34f;
        s.barrelLen = 0.60f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.12f;
        s.magW = 0.03f;
        s.magDepth = 0.08f;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("pioneer65", "Pioneer 6.5 Bolt", WeaponClass::Sniper, Cal_65, magBit(Mag_65), kBp65);
        w.projectile.muzzleVelocity = 860.0f;
        w.action = ActionType::Bolt;
        w.cyclicRpm = 60.0f;
        w.actionCycleTime = 0.85f;
        w.actionTravel = 0.08f;
        w.magazine = 10;
        w.reloadTime = 2.4f;
        w.emptyReloadTime = 2.8f;
        w.casing = CasingType::Magnum;
        w.dispersionMoa = 0.45f;
        w.recoilRise = 1.3f;
        w.recoilBack = 0.7f;
        w.armSpeedScale = 0.8f;
        w.armAccelScale = 0.65f;
        w.swayScale = 0.75f;
        w.adsTime = 0.36f;
        w.value = 6900;
        w.tier = 2;
        w.gridW = 5;
        w.loudness = 164.0f;
        GunShape s;
        s.stockLen = 0.32f;
        s.receiverLen = 0.22f;
        s.handguardLen = 0.32f;
        s.handguardW = 0.06f;
        s.barrelLen = 0.62f;
        s.barrelR = 0.011f;
        s.mag = MagStyle::Straight;
        s.magLen = 0.07f;
        s.magW = 0.03f;
        s.magDepth = 0.08f;
        s.furniture = kTan;
        buildLongGun(w, s);
        w.parts.push_back(box({0.035f, 0.010f, w.chamber.z + 0.05f}, {0.045f, 0.010f, 0.010f}, kMetal, 0, Part::Action));
        w.parts.push_back(box({0.058f, 0.010f, w.chamber.z + 0.05f}, {0.016f, 0.016f, 0.016f}, kPolymer, 0, Part::Action));
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("taiga54b", "Taiga 7.62R Bolt", WeaponClass::Sniper, Cal_762x54R, 0, kBp54R);
        w.action = ActionType::Bolt;
        w.reload = ReloadType::Tube; // internal magazine, loaded round by round
        w.magazine = 5;
        w.cyclicRpm = 60.0f;
        w.actionCycleTime = 1.1f;
        w.actionTravel = 0.09f;
        w.reloadTime = 0.55f;
        w.emptyReloadTime = 0.6f;
        w.casing = CasingType::Magnum;
        w.dispersionMoa = 1.2f;
        w.recoilRise = 2.0f;
        w.recoilBack = 0.9f;
        w.armSpeedScale = 0.78f;
        w.armAccelScale = 0.62f;
        w.swayScale = 0.8f;
        w.adsTime = 0.38f;
        w.value = 1600;
        w.tier = 0;
        w.gridW = 6;
        w.loudness = 166.0f;
        GunShape s;
        s.stock = StockStyle::Wood;
        s.stockLen = 0.36f;
        s.receiverLen = 0.20f;
        s.handguardLen = 0.50f;
        s.handguardW = 0.055f;
        s.barrelLen = 0.72f;
        s.mag = MagStyle::None;
        s.sight = SightStyle::Notch;
        s.sightH = 0.045f;
        s.furniture = kWalnut;
        buildLongGun(w, s);
        w.parts.push_back(box({0.040f, 0.000f, w.chamber.z + 0.05f}, {0.05f, 0.010f, 0.010f}, kMetal, 0, Part::Action));
        w.parts.push_back(cyl({0.065f, -0.005f, w.chamber.z + 0.05f}, {0.018f, 0.018f, 0.018f}, kMetal, Part::Action));
        v.push_back(w);
    }

    // ---------- Heavy
    {
        WeaponDef w = makeSniper();
        w.id = "behemoth50";
        w.name = "Behemoth .50 Anti-Materiel";
        w.caliber = Cal_50BMG;
        w.projectile = kBp50;
        w.dispersionMoa = 0.6f;
        w.recoilRise = 2.6f;
        w.recoilBack = 1.6f;
        w.flashScale = 2.4f;
        w.smokeScale = 3.0f;
        w.armSpeedScale = 0.5f;
        w.armAccelScale = 0.38f;
        w.swayScale = 0.65f;
        w.adsTime = 0.5f;
        w.loudness = 175.0f;
        w.value = 28000;
        w.tier = 3;
        w.gridW = 7;
        for (Part& p : w.parts) {
            if (p.color == kOdGreen) p.color = kSteelBlue;
            if (p.cylinder && p.center.z < -0.8f) { // longer, fatter barrel and brake
                p.center.z -= 0.15f;
                p.size.x *= 1.4f;
                p.size.y *= 1.4f;
            }
        }
        w.muzzle.z -= 0.15f;
        w.length += 0.15f;
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("hammer762", "Hammer 7.62 GPMG", WeaponClass::Lmg, Cal_762x51, magBit(Mag_Box308), kBp308);
        w.projectile.muzzleVelocity = 840.0f;
        w.canAuto = true;
        w.cyclicRpm = 650.0f;
        w.magazine = 100;
        w.reloadTime = 5.0f;
        w.emptyReloadTime = 5.8f;
        w.casing = CasingType::Magnum;
        w.recoilRise = 1.5f;
        w.recoilBack = 0.7f;
        w.armSpeedScale = 0.55f;
        w.armAccelScale = 0.42f;
        w.swayScale = 0.95f;
        w.adsTime = 0.48f;
        w.dispersionMoa = 2.2f;
        w.value = 16000;
        w.tier = 3;
        w.gridW = 6;
        w.loudness = 166.0f;
        GunShape s;
        s.stockLen = 0.28f;
        s.receiverLen = 0.34f;
        s.receiverH = 0.075f;
        s.handguardLen = 0.24f;
        s.handguardW = 0.065f;
        s.barrelLen = 0.56f;
        s.barrelR = 0.012f;
        s.mag = MagStyle::Box;
        s.furniture = kOdGreen;
        buildLongGun(w, s);
        v.push_back(w);
    }
    {
        WeaponDef w = longBase("ridgeline556ar", "Ridgeline 5.56 Auto Rifle", WeaponClass::Lmg, Cal_556, magBit(Mag_Stanag), kBp556);
        rifleCommon(w);
        w.projectile.muzzleVelocity = 940.0f;
        w.cyclicRpm = 700.0f;
        w.recoilRise = 0.9f;
        w.armSpeedScale = 0.8f;
        w.armAccelScale = 0.7f;
        w.swayScale = 0.9f;
        w.dispersionMoa = 1.2f;
        w.value = 6200;
        w.tier = 2;
        w.gridW = 5;
        GunShape s;
        s.stockLen = 0.27f;
        s.receiverLen = 0.24f;
        s.handguardLen = 0.30f;
        s.handguardW = 0.06f;
        s.barrelLen = 0.56f;
        s.barrelR = 0.012f;
        s.mag = MagStyle::Curved;
        buildLongGun(w, s);
        v.push_back(w);
    }
