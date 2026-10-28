#include <algorithm>
#include <array>
#include <iterator>

#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <spdlog/spdlog.h>
#include <fast/Fast3dGui.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/oops-all-vampires/OopsAllVampires.h"
#include "soh/Enhancements/oops-all-vampires/OopsAllVampiresWindow.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
// SaveManager.h only declares these for C translation units.
void Save_InitFile(int isDebug);
}

// ============================================================================
// Oops! All Vampires? — a Vampire Survivors style time-survival mode.
//
// Phase 1 (this file): session state machine, save bootstrap and launch,
// run bookkeeping (timer, kills, scene detection) and teardown. The wave
// director, XP/leveling and the upgrade system are layered on top of this.
// ============================================================================

namespace OAV {

namespace {

Session sSession;

// Settings (mirrored into CVars by the menu code) -------------------------

#define OAV_CVAR(var) CVAR_ENHANCEMENT("OopsAllVampires." var)

int32_t GetArenaSetting() {
    return CVarGetInteger(OAV_CVAR("Arena"), static_cast<int32_t>(Arena::HYRULE_FIELD));
}

int32_t GetDifficultySetting() {
    return CVarGetInteger(OAV_CVAR("Difficulty"), static_cast<int32_t>(Difficulty::NORMAL));
}

// Arena configuration -------------------------------------------------------

struct ArenaInfo {
    const char* name;
    uint16_t entrance;
    int32_t sceneNum;
};

const ArenaInfo sArenas[] = {
    { "Hyrule Field", ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN, SCENE_HYRULE_FIELD },
    { "Lake Hylia", ENTR_LAKE_HYLIA_0_1, SCENE_LAKE_HYLIA },
    { "Gerudo Valley", ENTR_GERUDO_VALLEY_0_1, SCENE_GERUDO_VALLEY },
    { "Lon Lon Ranch", ENTR_LON_LON_RANCH_ENTRANCE, SCENE_LON_LON_RANCH },
};

const ArenaInfo* GetArenaInfo(Arena arena) {
    int32_t index = static_cast<int32_t>(arena);
    if (index < 0 || index >= static_cast<int32_t>(std::size(sArenas))) {
        index = 0;
    }
    return &sArenas[index];
}

// XP curve -------------------------------------------------------------------

uint32_t XpForLevelImpl(uint32_t level) {
    // Level 1 -> 2 costs 50, each subsequent level costs 25 more.
    return 25 + level * 25;
}

// Run bookkeeping ------------------------------------------------------------

void OnFrameUpdate() {
    // Test hook: OAV_AUTOSTART=1 launches a run a few seconds after boot
    // (used for headless smoke testing; no effect in normal play).
    if (sSession.state == SessionState::INACTIVE) {
        static bool sAutostart = getenv("OAV_AUTOSTART") != nullptr;
        static uint32_t sBootDelay = 400; // ~20s after boot
        if (sAutostart && sBootDelay > 0 && --sBootDelay == 0) {
            SPDLOG_INFO("[OAV] autostart: launching run");
            OAV_StartRun();
        }
        return;
    }

    // Test hook: OAV_AUTOLEVEL=1 auto-picks the first level-up card after a
    // short delay (exercises the full upgrade pipeline headlessly).
    if (sSession.state == SessionState::LEVEL_UP) {
        static bool sAutolevel = getenv("OAV_AUTOLEVEL") != nullptr;
        static uint32_t sCardDelay = 0;
        if (sAutolevel) {
            if (++sCardDelay >= 100) {
                sCardDelay = 0;
                SPDLOG_INFO("[OAV] autolevel: applying card 0 (level {})", sSession.level);
                ApplyUpgradeChoice(0);
            }
        }
        return;
    }

    // Test hook continuation: with OAV_AUTOLEVEL the player is made
    // invulnerable and starts with the spin attack so the whole upgrade
    // pipeline (kills -> gems -> XP -> cards -> weapons) runs headlessly.
    static bool sTestGodmode = getenv("OAV_AUTOLEVEL") != nullptr;
    if (sTestGodmode && sSession.arenaEntered && sSession.frames == 1) {
        SPDLOG_INFO("[OAV] autolevel: test godmode + starting spin attack");
        gSaveContext.healthCapacity = 20 * 16;
        gSaveContext.health = 20 * 16;
        sSession.upgradeLevels[static_cast<int32_t>(Upgrade::SPIN)] = 1;
        GrantWeapon(Upgrade::SPIN);
    }
    if (sTestGodmode) {
        gSaveContext.health = gSaveContext.healthCapacity;
        // Drip-feed XP so the level-up/card pipeline is exercised.
        if ((sSession.frames % 200) == 0 && sSession.state == SessionState::RUNNING) {
            sSession.xp += 300;
            SPDLOG_INFO("[OAV] autolevel: injected xp (total {})", sSession.xp);
        }
    }

    if (sSession.state != SessionState::RUNNING) {
        return;
    }
    if (gPlayState != nullptr && gPlayState->sceneNum == GetArenaInfo(sSession.arena)->sceneNum) {
        // We are inside the arena.
        if (!sSession.arenaEntered) {
            SPDLOG_INFO("[OAV] arena entered (scene {})", gPlayState->sceneNum);
            Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_ALL);
        }
        sSession.arenaEntered = true;
        // Re-assert the HUD in case a cutscene flag hides it again.
        if ((sSession.frames % 60) == 1) {
            Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_ALL);
        }
    } else if (sSession.arenaEntered) {
        // We left the arena without ending the run (e.g. via a grotto or a
        // developer warp). End the session rather than corrupting state.
        OAV_EndRun();
        return;
    } else {
        // Still transitioning into the arena after OAV_StartRun().
        return;
    }
    sSession.frames++;
    if (sSession.evolutionTimer > 0) {
        sSession.evolutionTimer--;
    }
    UpdateSessionFrame();
}

void OnExitGameHandler(int32_t) {
    // Leaving to the title screen always tears the session down.
    sSession.Reset();
    ResetWeaponScheduler();
}

void RegisterHooks() {
    // Session state drives these; keep them registered permanently and let
    // the state machine decide what to do (hooks are cheap no-ops otherwise).
    COND_HOOK(OnGameFrameUpdate, true, OnFrameUpdate);
    COND_HOOK(OnExitGame, true, OnExitGameHandler);
}

} // namespace

Session& GetSession() {
    return sSession;
}

bool IsSessionActive() {
    return sSession.state != SessionState::INACTIVE;
}

bool IsRunScene(int32_t sceneNum) {
    return IsSessionActive() && sceneNum == GetArenaInfo(sSession.arena)->sceneNum;
}

uint32_t XpForLevel(uint32_t level) {
    return XpForLevelImpl(level);
}

} // namespace OAV

// ============================================================================
// Launch / teardown (called from the menu)
// ============================================================================

void OAV_StartRun() {
    if (gGameState == nullptr) {
        return;
    }

    // Reset any previous session.
    OAV::sSession.Reset();
    // The built-in HUD gameplay timer (Interface_DrawTotalGameplayTimer)
    // shows stats.playTimer; zero it so it tracks this run from the start.
    gSaveContext.ship.stats.playTimer = 0;
    gSaveContext.ship.stats.gameComplete = 0;
    OAV::ResetWeaponScheduler();
    OAV::sSession.arena = static_cast<OAV::Arena>(OAV::GetArenaSetting());
    OAV::sSession.difficulty = static_cast<OAV::Difficulty>(OAV::GetDifficultySetting());
    OAV::sSession.state = OAV::SessionState::RUNNING;

    // Fresh save, then apply the mode's starting state. fileNum 0xFE makes
    // SaveManager skip all writes (same sentinel Boss Rush uses).
    Save_InitFile(false);
    OAV_InitSave();

    // Boot into the arena.
    gGameState->running = false;
    SET_NEXT_GAMESTATE(gGameState, Play_Init, PlayState);
}

void OAV_EndRun() {
    if (OAV::sSession.state == OAV::SessionState::INACTIVE) {
        return;
    }

    // Record local bests before tearing the session down.
    uint32_t seconds = OAV::sSession.frames / 20;
    if (seconds > (uint32_t)CVarGetInteger(CVAR_ENHANCEMENT("OopsAllVampires.BestTime"), 0)) {
        CVarSetInteger(CVAR_ENHANCEMENT("OopsAllVampires.BestTime"), (int32_t)seconds);
    }
    if (OAV::sSession.kills >
        (uint32_t)CVarGetInteger(CVAR_ENHANCEMENT("OopsAllVampires.BestKills"), 0)) {
        CVarSetInteger(CVAR_ENHANCEMENT("OopsAllVampires.BestKills"),
                       (int32_t)OAV::sSession.kills);
    }
    if (OAV::sSession.level >
        (uint32_t)CVarGetInteger(CVAR_ENHANCEMENT("OopsAllVampires.BestLevel"), 0)) {
        CVarSetInteger(CVAR_ENHANCEMENT("OopsAllVampires.BestLevel"),
                       (int32_t)OAV::sSession.level);
    }

    // The player is wherever the game left them; the world unfreezes.
    OAV::sSession.Reset();
    OAV::ResetWeaponScheduler();
}

// ============================================================================
// Save bootstrap (BossRush_InitSave pattern)
// ============================================================================

extern "C" void OAV_InitSave(void) {
    // Player name: "Link" (PAL charset), like Boss Rush.
    static const u8 oavPlayerName[] = { 0x15, 0x12, 0x17, 0x14, 0x3E, 0x3E, 0x3E, 0x3E };
    static_assert(sizeof(oavPlayerName) == sizeof(gSaveContext.playerName));
    std::copy(std::begin(oavPlayerName), std::end(oavPlayerName), std::begin(gSaveContext.playerName));
    gSaveContext.ship.filenameLanguage = NAME_LANGUAGE_PAL;

    // Save sentinel: never write this session to disk.
    gSaveContext.fileNum = 0xFE;

    // Adult Link in the chosen arena.
    gSaveContext.linkAge = LINK_AGE_ADULT;
    gSaveContext.entranceIndex =
        OAV::GetArenaInfo(static_cast<OAV::Arena>(OAV::GetArenaSetting()))->entrance;
    gSaveContext.cutsceneIndex = 0x8000; // no cutscene
    gSaveContext.isMagicAcquired = 0;
    gSaveContext.magicLevel = 0;
    gSaveContext.magic = 0;

    // Starting vitals: three hearts.
    gSaveContext.healthCapacity = 16 * 3;
    gSaveContext.health = 16 * 3;

    // Empty inventory; weapons are granted by the upgrade system.
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.items); i++) {
        gSaveContext.inventory.items[i] = ITEM_NONE;
    }
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
        gSaveContext.inventory.ammo[i] = 0;
    }

    // Melee basics: Kokiri Sword on B, no shield (dodging is the defense).
    gSaveContext.inventory.equipment = 0;
    gSaveContext.inventory.equipment |= 1 << 0; // Kokiri Sword
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
    Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);

    // B button: sword. No C-button items yet.
    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;
    for (int button = 1; button < ARRAY_COUNT(gSaveContext.equips.buttonItems); button++) {
        gSaveContext.equips.buttonItems[button] = ITEM_NONE;
    }
    for (int slot = 0; slot < ARRAY_COUNT(gSaveContext.equips.cButtonSlots); slot++) {
        gSaveContext.equips.cButtonSlots[slot] = SLOT_NONE;
    }

    // Day-dusk lighting for the arena, frozen (time does not advance).
    gSaveContext.dayTime = 0xB000;
}

// Boot-time hook registration.
static void RegisterOopsAllVampires() {
    OAV::RegisterHooks();
    OAV::RegisterDirector();

    // HUD / level-up / results overlay.
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    gui->AddGuiWindow(std::make_shared<OavHudWindow>(CVAR_WINDOW("OopsAllVampiresHud"),
                                                    "Oops! All Vampires?"));
}

static RegisterShipInitFunc initFunc(RegisterOopsAllVampires);
