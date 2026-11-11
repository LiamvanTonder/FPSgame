#pragma once

#include <vector>

// Every sound in the game is synthesised at startup: no asset files.
enum Clip {
    Clip_ShotPistol, Clip_ShotRifle, Clip_ShotMagnum, Clip_ShotShotgun, Clip_ShotSuppressed, Clip_Crack, Clip_DistantTail,
    Clip_StepGrass, Clip_StepDirt, Clip_StepConcrete, Clip_StepWood, Clip_StepMetal,
    Clip_HitDirt, Clip_HitConcrete, Clip_HitWood, Clip_HitMetal, Clip_HitFlesh, Clip_HitFoliage,
    Clip_MagOut, Clip_MagIn, Clip_Bolt, Clip_ShellIn, Clip_DryFire,
    Clip_Bandage, Clip_Pills, Clip_Rustle, Clip_Beacon, Clip_Flare, Clip_BodyFall, Clip_Pain,
    Clip_Heartbeat, Clip_Breath, Clip_Wind, Clip_Leaves, Clip_StepLeaves, Clip_Gear, Clip_Groan,
    kClipCount
};

struct ClipData {
    std::vector<float> samples; // mono
};

constexpr int kSampleRate = 48000;

// Generates all clips (deterministic).
void synthesizeClips(std::vector<ClipData>& out);
