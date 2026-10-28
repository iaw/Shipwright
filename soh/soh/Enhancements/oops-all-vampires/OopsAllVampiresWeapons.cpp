#include <cmath>

#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/oops-all-vampires/OopsAllVampires.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// ============================================================================
// Oops! All Vampires? — auto-weapon scheduler.
//
// Weapons "fire" themselves by emulating controller presses and hot-swapping
// the C-button item before each press, so any number of auto-weapons can
// coexist on top of the vanilla player actor without new actors or code.
// ============================================================================

namespace OAV {

namespace {

// Press duration for C-button items (frames).
constexpr uint32_t PRESS_FRAMES = 2;
// B-button hold time to charge a spin attack (frames).
constexpr uint32_t SPIN_CHARGE_FRAMES = 22;

struct WeaponDef {
    Upgrade id;
    int32_t cooldown[3]; // cooldown by weapon level (1..3)
};

// Cooldowns in 20 fps frames.
const WeaponDef sWeapons[] = {
    { Upgrade::SPIN,     { 100,  80,  64 } },
    { Upgrade::BOW,      {  40,  32,  24 } },
    { Upgrade::DINS,     { 200, 160, 120 } },
    { Upgrade::NAYRUS,   { 150, 130, 110 } },
    { Upgrade::BOMBS,    { 130, 105,  85 } },
    { Upgrade::HOOKSHOT, { 100,  85,  70 } },
};

// Per-weapon cooldown timers (indexed by Upgrade).
uint32_t sCooldowns[static_cast<int32_t>(Upgrade::COUNT_)] = { 0 };

int32_t WeaponLevel(Upgrade id) {
    return GetSession().upgradeLevels[static_cast<int32_t>(id)];
}

void PressButton(uint32_t button) {
    static bool sLogged = false;
    if (!sLogged) {
        sLogged = true;
        SPDLOG_INFO("[OAV] weapon scheduler issued its first button press (0x{:x})", button);
    }
    GameInteractor::RawAction::EmulateButtonPress((int32_t)button);
}

// Hot-swaps the shared C-button slot to `item` and presses it.
void FireCItem(s16 item, s16 slot) {
    gSaveContext.equips.buttonItems[1] = item;   // C-left slot
    gSaveContext.equips.cButtonSlots[0] = slot;
    PressButton(BTN_CLEFT);
}

// ---------------------------------------------------------------------------
// Sustain: infinite ammo + slow magic regeneration
// ---------------------------------------------------------------------------

void UpdateSustain() {
    Session& session = GetSession();

    // Infinite ammo for owned weapons.
    if (WeaponLevel(Upgrade::BOW) > 0) {
        gSaveContext.inventory.ammo[SLOT_BOW] = 30;
    }
    if (WeaponLevel(Upgrade::BOMBS) > 0) {
        gSaveContext.inventory.ammo[SLOT_BOMB] = 15;
    }

    // Slow magic regeneration while magic weapons are owned.
    bool needsMagic = WeaponLevel(Upgrade::DINS) > 0 || WeaponLevel(Upgrade::NAYRUS) > 0;
    if (needsMagic && gSaveContext.magicLevel > 0) {
        session.magicTick++;
        if (session.magicTick >= 20) {
            session.magicTick = 0;
            int32_t cap = gSaveContext.magicLevel * 48;
            if (gSaveContext.magic < cap) {
                gSaveContext.magic += 8;
                if (gSaveContext.magic > cap) {
                    gSaveContext.magic = cap;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------

// Starts a weapon press if nothing else is being pressed. Returns true when
// the weapon actually fired (and its cooldown should start).
bool TryFire(Upgrade id) {
    Session& session = GetSession();
    if (session.weaponBusy > 0) {
        return false;
    }

    int32_t level = WeaponLevel(id);
    if (level <= 0) {
        return false;
    }

    switch (id) {
        case Upgrade::SPIN:
            // Hold B to charge, then the release triggers the spin.
            if (session.spinHold > 0) {
                return false; // already charging (handled below)
            }
            session.weaponActive = static_cast<int32_t>(Upgrade::SPIN);
            session.spinHold = SPIN_CHARGE_FRAMES;
            session.weaponBusy = SPIN_CHARGE_FRAMES + PRESS_FRAMES;
            return true;

        case Upgrade::BOW:
            if (gSaveContext.inventory.ammo[SLOT_BOW] <= 0) {
                return false;
            }
            FireCItem(ITEM_BOW, SLOT_BOW);
            session.weaponBusy = PRESS_FRAMES;
            return true;

        case Upgrade::DINS:
            // Din's Fire costs magic; let the game deduct it.
            if (gSaveContext.magic < 12) {
                return false;
            }
            FireCItem(ITEM_DINS_FIRE, SLOT_DINS_FIRE);
            session.weaponBusy = PRESS_FRAMES;
            return true;

        case Upgrade::NAYRUS:
            // Only re-cast when the barrier is down.
            if (gSaveContext.nayrusLoveTimer > 0) {
                return false;
            }
            FireCItem(ITEM_NAYRUS_LOVE, SLOT_NAYRUS_LOVE);
            session.weaponBusy = PRESS_FRAMES;
            return true;

        case Upgrade::BOMBS:
            if (gSaveContext.inventory.ammo[SLOT_BOMB] <= 0) {
                return false;
            }
            FireCItem(ITEM_BOMB, SLOT_BOMB);
            session.weaponBusy = PRESS_FRAMES;
            return true;

        case Upgrade::HOOKSHOT:
            FireCItem(WeaponLevel(Upgrade::HOOKSHOT) >= 3 ? ITEM_LONGSHOT : ITEM_HOOKSHOT,
                      SLOT_HOOKSHOT);
            session.weaponBusy = PRESS_FRAMES;
            return true;

        default:
            return false;
    }
}

} // namespace

namespace {

void ResetCooldowns() {
    for (int32_t i = 0; i < static_cast<int32_t>(Upgrade::COUNT_); i++) {
        sCooldowns[i] = 0;
    }
}

} // namespace

void UpdateWeapons() {
    Session& session = GetSession();
    if (gPlayState == nullptr || session.state != SessionState::RUNNING) {
        return;
    }

    UpdateSustain();

    // Continue an in-flight hold/press.
    if (session.weaponBusy > 0) {
        session.weaponBusy--;
        if (session.spinHold > 0) {
            session.spinHold--;
            PressButton(BTN_B);
        }
        return;
    }
    session.spinHold = 0;
    session.weaponActive = 0;

    // Tick cooldowns.
    for (int32_t i = 0; i < static_cast<int32_t>(Upgrade::COUNT_); i++) {
        if (sCooldowns[i] > 0) {
            sCooldowns[i]--;
        }
    }

    // Try to fire each owned weapon when its cooldown expires.
    bool fired = false;
    for (const auto& weapon : sWeapons) {
        int32_t index = static_cast<int32_t>(weapon.id);
        if (WeaponLevel(weapon.id) <= 0 || sCooldowns[index] > 0) {
            continue;
        }
        if (TryFire(weapon.id)) {
            int32_t level = WeaponLevel(weapon.id);
            sCooldowns[index] = weapon.cooldown[CLAMP(level, 1, 3) - 1];
            fired = true;
            break; // one emulated press at a time
        }
    }
    (void)fired;
}

// ---------------------------------------------------------------------------
// Item grants on level-up
// ---------------------------------------------------------------------------

void GrantWeapon(Upgrade weapon) {
    switch (weapon) {
        case Upgrade::BOW:
            gSaveContext.inventory.items[SLOT_BOW] = ITEM_BOW;
            gSaveContext.inventory.ammo[SLOT_BOW] = 30;
            break;
        case Upgrade::DINS:
            gSaveContext.inventory.items[SLOT_DINS_FIRE] = ITEM_DINS_FIRE;
            break;
        case Upgrade::NAYRUS:
            gSaveContext.inventory.items[SLOT_NAYRUS_LOVE] = ITEM_NAYRUS_LOVE;
            break;
        case Upgrade::BOMBS:
            gSaveContext.inventory.items[SLOT_BOMB] = ITEM_BOMB;
            gSaveContext.inventory.ammo[SLOT_BOMB] = 15;
            break;
        case Upgrade::HOOKSHOT:
            gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_HOOKSHOT;
            if (GetSession().upgradeLevels[static_cast<int32_t>(Upgrade::HOOKSHOT)] >= 3) {
                gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_LONGSHOT;
            }
            break;
        default:
            break;
    }
    ResetCooldowns();
}

// Exposed for the session reset path.
void ResetWeaponScheduler() {
    ResetCooldowns();
}

} // namespace OAV
