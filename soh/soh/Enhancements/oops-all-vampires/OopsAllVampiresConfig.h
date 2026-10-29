#pragma once

#include <stdint.h>

#ifdef __cplusplus

namespace OAV {

// All difficulty/pacing knobs for the mode, loaded from
// oops-all-vampires.json in the game folder (next to shipofharkinian.json).
// A template file is written there if it does not exist yet; any missing or
// invalid key falls back to the defaults below.
struct OavTuning {
    // Victory condition: the run ends in a win after this many seconds.
    uint32_t runLengthSeconds = 1800;

    // XP curve: cost of level N = xpBase + N * xpPerLevel.
    uint32_t xpBase = 25;
    uint32_t xpPerLevel = 25;

    // Spawn pacing: interval shrinks by 1 frame every intervalRamp frames
    // (floor: intervalMin); batch grows by 1 every batchRamp frames
    // (ceiling: batchMax).
    uint32_t spawnIntervalStart = 22;
    uint32_t spawnIntervalRamp = 2400;
    uint32_t spawnIntervalMin = 10;
    uint32_t spawnBatchStart = 2;
    uint32_t spawnBatchRamp = 7000;
    uint32_t spawnBatchMax = 6;
    uint32_t spawnCap = 35;          // max simultaneous non-elite enemies
    float spawnRingRadius = 430.0f;  // spawn distance around the player
    float despawnDistance = 3500.0f; // enemies farther than this are recycled

    // Elites (evolution carriers).
    uint32_t eliteStartSeconds = 240;
    uint32_t eliteIntervalSeconds = 45;
    uint32_t eliteHardCount = 2; // elites per wave on hard difficulty

    // Wave tier shown on the HUD.
    uint32_t waveSecondsPerTier = 300;
    uint32_t waveMaxTier = 7;

    // Roster phase boundaries: second at which each enemy tier unlocks.
    uint32_t phaseSeconds[7] = { 0, 60, 120, 240, 360, 480, 720 };
};

// Loads (or reloads) tuning from oops-all-vampires.json in the game folder.
// Writes the default template if the file does not exist yet. Called at run
// start, so edits apply to the next run without a rebuild.
void ReloadTuning();

// Current tuning (loads once on first use if ReloadTuning was never called).
const OavTuning& GetTuning();

} // namespace OAV

#endif
