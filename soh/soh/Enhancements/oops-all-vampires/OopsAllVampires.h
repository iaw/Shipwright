#pragma once

#include <stdint.h>

#ifdef __cplusplus

#include <string>
#include <vector>

struct Actor; // z64 actor (defined in z64.h)

namespace OAV {

// Arena ("field") the run takes place in.
enum class Arena : int32_t {
    HYRULE_FIELD = 0,
    LAKE_HYLIA = 1,
    GERUDO_VALLEY = 2,
    LON_LON_RANCH = 3,
};

// Difficulty multipliers applied to the wave director.
enum class Difficulty : int32_t {
    NORMAL = 0,
    HARD = 1,
};

// High level run state machine.
enum class SessionState : int32_t {
    INACTIVE = 0,   // No run in progress
    RUNNING = 1,    // Arena combat in progress
    LEVEL_UP = 2,   // Frozen while the player picks an upgrade
    GAME_OVER = 3,  // Results screen showing
};

// Upgrade ids offered by the level-up card menu.
enum class Upgrade : int32_t {
    NONE = 0,
    HEART_CONTAINER, // +1 heart container, heals to full
    MAGIC,           // magic meter, then double magic
    SWORD,           // Kokiri -> Master -> Giant's Knife
    MAGNET,          // +gem vacuum radius
    SPIN,            // auto spin attack (B charge)
    BOW,             // auto-firing bow (arrows)
    DINS,            // auto Din's Fire (magic)
    NAYRUS,          // auto Nayru's Love (magic)
    BOMBS,           // auto-dropped bombs
    HOOKSHOT,        // auto hookshot (longshot at max)
    RECOVERY,        // emergency heal (fallback card)
    COUNT_
};

// A single card shown during a level-up.
struct UpgradeChoice {
    Upgrade id;
    const char* title;
    const char* description;
    int32_t levelAfter; // resulting upgrade level (for display)
};

struct Session {
    SessionState state = SessionState::INACTIVE;
    Arena arena = Arena::HYRULE_FIELD;
    Difficulty difficulty = Difficulty::NORMAL;

    // Progression
    uint32_t level = 1;      // current character level
    uint32_t xp = 0;         // XP collected toward the next level
    uint32_t xpToNext = 50;  // XP required for the next level
    uint32_t kills = 0;      // enemies defeated this run
    uint32_t frames = 0;     // elapsed gameplay frames (20 fps)
    uint32_t wave = 0;       // current wave tier (director)
    bool arenaEntered = false; // set once the arena scene is observed
    bool victory = false;      // survived the full 30 minutes

    // Upgrades
    int32_t upgradeLevels[static_cast<int32_t>(Upgrade::COUNT_)] = { 0 };

    // Level-up card state
    std::vector<UpgradeChoice> choices;

    // Director internals
    uint32_t spawnCooldown = 0;
    uint32_t eliteCooldown = 0;

    // Weapon scheduler internals
    uint32_t weaponBusy = 0;        // frames remaining of the current press/hold
    int32_t weaponActive = 0;      // Upgrade id currently holding a button
    uint32_t spinHold = 0;          // frames remaining of the B charge hold
    uint32_t magicTick = 0;         // slow magic regen counter
    uint32_t burstShots = 0;        // remaining burst presses (evolved bow)

    // Great Fairy evolutions (maxed weapon + maxed paired passive, then an
    // elite kill blesses the weapon)
    bool weaponEvolved[static_cast<int32_t>(Upgrade::COUNT_)] = { false };
    Upgrade evolveReady = Upgrade::NONE; // weapon waiting for its elite kill
    uint32_t evolutionTimer = 0;         // blessing banner countdown
    const char* evolutionTitle = nullptr;

    void Reset() {
        state = SessionState::INACTIVE;
        level = 1;
        xp = 0;
        xpToNext = 50;
        kills = 0;
        frames = 0;
        wave = 0;
        arenaEntered = false;
        victory = false;
        for (int32_t i = 0; i < static_cast<int32_t>(Upgrade::COUNT_); i++) {
            upgradeLevels[i] = 0;
        }
        choices.clear();
        spawnCooldown = 0;
        eliteCooldown = 0;
        weaponBusy = 0;
        weaponActive = 0;
        spinHold = 0;
        magicTick = 0;
        burstShots = 0;
        for (int32_t i = 0; i < static_cast<int32_t>(Upgrade::COUNT_); i++) {
            weaponEvolved[i] = false;
        }
        evolveReady = Upgrade::NONE;
        evolutionTimer = 0;
        evolutionTitle = nullptr;
    }
};

// Global session. Valid only when a run is in progress.
Session& GetSession();

bool IsSessionActive();
bool IsRunScene(int32_t sceneNum);

// XP curve: cost of the next level.
uint32_t XpForLevel(uint32_t level);

// HUD helpers: upgrade names/levels and elite detection (director data).
const char* GetUpgradeTitle(Upgrade id);
int32_t GetUpgradeMaxLevel(Upgrade id);
const char* GetEvolutionTitle(Upgrade id); // nullptr if not evolvable
bool IsEliteActor(Actor* actor);           // elite carriers (IK / Flare Dancer)

// Gem vacuum radius derived from the magnet upgrade level.
float GetMagnetRadius();

// Level-up card generation and application (director module).
void GenerateLevelUpChoices();
void ApplyUpgradeChoice(int32_t choiceIndex);

// Per-frame session systems (director, despawner, gems). Called while RUNNING.
void UpdateSessionFrame();

// Auto-weapon scheduler + ammo/magic sustain. Called while RUNNING.
void UpdateWeapons();

// Grants the item(s) backing a weapon upgrade (called on level-up apply).
void GrantWeapon(Upgrade weapon);

// Clears pending weapon presses/cooldowns (called on session reset).
void ResetWeaponScheduler();

// Boot-time registration of the director's hooks.
void RegisterDirector();

} // namespace OAV

// C bridge used from the menu/launch path (see OopsAllVampires.cpp).
void OAV_StartRun();
void OAV_EndRun();

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the save context for an Oops! All Vampires? run.
// Must be called immediately after Save_InitFile(false).
void OAV_InitSave(void);

#ifdef __cplusplus
}
#endif
