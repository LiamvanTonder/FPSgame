#pragma once

// Surface patterns evaluated procedurally in the lit shader (placeholder "textures").
enum class Pattern : int {
    Plain = 0,
    Grid = 1,        // 1 m / 10 m ground grid
    TargetRings = 2, // scoring rings around param.xyz, target facing +Z
    Unlit = 3,       // flat emissive colour
    Glow = 4,        // additive light (flashes, tracers, sparks); param.w = intensity
    Smoke = 5,       // lit, alpha blended; param.w = opacity
    Terrain = 6,     // ground: mottled grass / dirt from the vertex colour
    Foliage = 7,     // leaves: noisy dark green
    Planks = 8,      // wooden boards
    Bricks = 9,      // masonry courses
};
