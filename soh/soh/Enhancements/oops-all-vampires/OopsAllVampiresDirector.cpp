#include <cmath>

#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/oops-all-vampires/OopsAllVampires.h"
#include "soh/Enhancements/oops-all-vampires/OopsAllVampiresConfig.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// ============================================================================
// Oops! All Vampires? — wave director, spawner, XP gems and level-ups.
// ============================================================================

namespace OAV {

namespace {

// ---------------------------------------------------------------------------
// Roster (actor ids/params verified against the EnemyRandomizer spawn table)
// ---------------------------------------------------------------------------

struct EnemyDef {
    s16 actorId;
    s16 params;
    uint32_t tier;     // roster phase (1..7), unlocked at phaseSeconds[tier-1]
    s16 gemType;       // ITEM00_* gem dropped on defeat
    uint32_t weight;   // spawn weight
    bool elite;        // spawned by the elite timer only
};

// Earliest spawn frame for a tier, from the tuning file. The game clock
// ticks at 20 fps (defined before the C headers provide FRAMES_PER_SECOND).
uint32_t TierMinFrame(uint32_t tier) {
    if (tier < 1) {
        tier = 1;
    }
    if (tier > 7) {
        tier = 7;
    }
    return GetTuning().phaseSeconds[tier - 1] * 20;
}

// clang-format off
const EnemyDef sRoster[] = {
    // 0:00 - 2:00 — chaff
    { ACTOR_EN_SKB,       1,     1,      ITEM00_RUPEE_GREEN,  10, false }, // Stalchild (small)
    { ACTOR_EN_FIREFLY,   2,     1,      ITEM00_RUPEE_GREEN,  10, false }, // Keese
    { ACTOR_EN_CROW,      0,     1,      ITEM00_RUPEE_GREEN,  10, false }, // Guay
    // 2:00 - 5:00
    { ACTOR_EN_FIREFLY,   1,     3,      ITEM00_RUPEE_GREEN,   8, false }, // Fire Keese
    { ACTOR_EN_BB,       -1,     2,      ITEM00_RUPEE_GREEN,   8, false }, // Bubble
    { ACTOR_EN_TITE,     -1,     2,      ITEM00_RUPEE_GREEN,   8, false }, // Red Tektite
    { ACTOR_EN_GOMA,      7,     2,      ITEM00_RUPEE_GREEN,   6, false }, // Gohma Larva
    // 2:00 - 4:00
    { ACTOR_EN_TITE,     -2,     3,      ITEM00_RUPEE_BLUE,    8, false }, // Blue Tektite
    { ACTOR_EN_PEEHAT,    1,     3,      ITEM00_RUPEE_BLUE,    6, false }, // Peahat Larva
    { ACTOR_EN_WF,        0,     3,      ITEM00_RUPEE_BLUE,    8, false }, // Wolfos
    { ACTOR_EN_DEKUBABA,  0,     3,      ITEM00_RUPEE_BLUE,    8, false }, // Deku Baba (small)
    // 4:00 - 6:00
    { ACTOR_EN_TEST,      2,     4,      ITEM00_RUPEE_RED,     8, false }, // Stalfos
    { ACTOR_EN_RD,        1,     4,      ITEM00_RUPEE_RED,     8, false }, // ReDead
    { ACTOR_EN_RD,    32766,     4,      ITEM00_RUPEE_RED,     6, false }, // Gibdo
    { ACTOR_EN_SKB,      20,     4,      ITEM00_RUPEE_RED,     6, false }, // Stalchild (big)
    // 6:00 - 8:00
    { ACTOR_EN_ZF,       -2,     5,      ITEM00_RUPEE_RED,     8, false }, // Dinolfos
    { ACTOR_EN_RR,        0,     5,      ITEM00_RUPEE_RED,     6, false }, // Like-Like
    { ACTOR_EN_ST,        1,     5,      ITEM00_RUPEE_RED,     6, false }, // Big Skulltula
    { ACTOR_EN_IK,        2,     5,      ITEM00_RUPEE_PURPLE,  2, true  }, // Iron Knuckle (black)
    // 8:00 - 12:00
    { ACTOR_EN_IK,        3,     6,      ITEM00_RUPEE_PURPLE,  4, true  }, // Iron Knuckle (white)
    { ACTOR_EN_FD,        0,     6,      ITEM00_RUPEE_PURPLE,  2, true  }, // Flare Dancer
    // 12:00+ — everything, heavier weights (final 18 minutes)
    { ACTOR_EN_TEST,      2,     7,      ITEM00_RUPEE_RED,    10, false }, // Stalfos (endless)
    { ACTOR_EN_ZF,       -2,     7,      ITEM00_RUPEE_RED,    10, false }, // Dinolfos (endless)
    { ACTOR_EN_IK,        2,     7,      ITEM00_RUPEE_PURPLE,  4, true  }, // Iron Knuckle storm
};
// clang-format on

constexpr uint32_t RUN_LENGTH_SECONDS = 30 * 60;
constexpr uint32_t FRAMES_PER_SECOND = 20;

// Settings ---------------------------------------------------------------

int32_t GetSpawnCap() {
    return (int32_t)GetTuning().spawnCap;
}

// Helpers -----------------------------------------------------------------

float PlayerDistance2D(Actor* actor) {
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return 0.0f;
    }
    f32 dx = actor->world.pos.x - player->actor.world.pos.x;
    f32 dz = actor->world.pos.z - player->actor.world.pos.z;
    return sqrtf(dx * dx + dz * dz);
}

int32_t CountEnemies() {
    int32_t count = 0;
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; actor != nullptr;
         actor = actor->next) {
        count++;
    }
    return count;
}

const EnemyDef* PickEnemy(bool elite) {
    std::vector<const EnemyDef*> candidates;
    uint64_t totalWeight = 0;
    for (const auto& def : sRoster) {
        if (def.elite == elite && GetSession().frames >= TierMinFrame(def.tier)) {
            candidates.push_back(&def);
            totalWeight += def.weight;
        }
    }
    if (candidates.empty() || totalWeight == 0) {
        return nullptr;
    }
    uint64_t roll = static_cast<uint64_t>(Rand_ZeroOne() * static_cast<f32>(totalWeight));
    for (const auto& def : candidates) {
        if (roll < def->weight) {
            return def;
        }
        roll -= def->weight;
    }
    return candidates.back();
}

// Set while the director itself spawns an actor, so the native-spawn
// suppression hook does not block our own spawns.
bool sDirectorSpawning = false;

void SpawnEnemy(const EnemyDef* def) {
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || def == nullptr) {
        return;
    }

    // Spawn on a ring around the player, just outside the visible screen
    // (the field camera sees roughly 300-400 units around Link) so enemies
    // stream in from the screen edge instead of popping in far off-screen.
    const f32 radius = GetTuning().spawnRingRadius;
    f32 angle = Rand_ZeroOne() * 6.28318f;
    Vec3f pos;
    pos.x = player->actor.world.pos.x + cosf(angle) * radius;
    pos.y = player->actor.world.pos.y + 60.0f;
    pos.z = player->actor.world.pos.z + sinf(angle) * radius;

    sDirectorSpawning = true;
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, def->actorId, pos.x, pos.y, pos.z, 0, 0, 0,
                def->params);
    sDirectorSpawning = false;
}

void SpawnGem(Actor* fromActor, s16 gemType) {
    if (gPlayState == nullptr || fromActor == nullptr) {
        return;
    }
    Vec3f pos = fromActor->world.pos;
    Item_DropCollectible(gPlayState, &pos, gemType);
}

// Director ----------------------------------------------------------------

void UpdateDirector() {
    Session& session = GetSession();

    // Surviving the full 30 minutes is a victory; the results screen takes
    // over from here (world freeze + stats, same as death).
    if (session.frames >= GetTuning().runLengthSeconds * FRAMES_PER_SECOND) {
        session.victory = true;
        session.state = SessionState::GAME_OVER;
        SPDLOG_INFO("[OAV] survived the full run! ({} kills, level {})", session.kills, session.level);
        return;
    }

    // Telemetry (rate-limited): state of the world for headless testing.
    if ((session.frames % 100) == 0) {
        Player* dbgPlayer = GET_PLAYER(gPlayState);
        int32_t dbgEnemies = 0;
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
            dbgEnemies++;
        }
        SPDLOG_INFO("[OAV] t={}s wave={} enemies={} health={} xp={} player={}",
                    session.frames / 20, session.wave, dbgEnemies, gSaveContext.health, session.xp,
                    dbgPlayer != nullptr ? "ok" : "null");
    }

    // Wave tier for the HUD.
    uint32_t seconds = session.frames / FRAMES_PER_SECOND;
    session.wave = 1 + seconds / GetTuning().waveSecondsPerTier;
    if (session.wave > GetTuning().waveMaxTier) {
        session.wave = GetTuning().waveMaxTier;
    }

    const bool hard = session.difficulty == Difficulty::HARD;

    // Spawn cadence ramps up over the run. The phase schedule was compressed
    // 2x for the 30-minute arc, so the ramp is stretched to compensate: the
    // same per-minute intensity as the original pacing, floor at ~24 minutes.
    uint32_t interval = 22 - session.frames / 2400; // 22 -> 10 frames
    if (interval < 10) {
        interval = 10;
    }
    if (hard) {
        interval = interval * 7 / 10;
    }
    uint32_t batch = GetTuning().spawnBatchStart + session.frames / GetTuning().spawnBatchRamp;
    if (batch > GetTuning().spawnBatchMax) {
        batch = GetTuning().spawnBatchMax;
    }
    if (hard) {
        batch += 1;
    }

    if (session.spawnCooldown > 0) {
        session.spawnCooldown--;
    } else {
        session.spawnCooldown = interval;
        int32_t cap = GetSpawnCap();
        for (uint32_t i = 0; i < batch; i++) {
            if (CountEnemies() >= cap) {
                break;
            }
            SpawnEnemy(PickEnemy(false));
        }
    }

    // Elites: evolution carriers, on the tuned schedule.
    if (session.frames >= GetTuning().eliteStartSeconds * FRAMES_PER_SECOND) {
        if (session.eliteCooldown > 0) {
            session.eliteCooldown--;
        } else {
            // Finale ("wall of death"): the last minutes bring elites faster.
            uint32_t interval = GetTuning().eliteIntervalSeconds;
            uint32_t seconds = session.frames / FRAMES_PER_SECOND;
            if (seconds >= GetTuning().finaleStartSeconds) {
                interval = GetTuning().finaleEliteIntervalSeconds;
            }
            session.eliteCooldown = interval * FRAMES_PER_SECOND;
            for (uint32_t i = 0; i < (hard ? GetTuning().eliteHardCount : 1); i++) {
                SpawnEnemy(PickEnemy(true));
            }
            SPDLOG_INFO("[OAV] elite spawned (t={}s)", session.frames / FRAMES_PER_SECOND);
        }
    }
}

// Despawn far-away enemies and recycle the actor budget.
void DespawnDistantEnemies() {
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; actor != nullptr;
         actor = actor->next) {
        if (PlayerDistance2D(actor) > GetTuning().despawnDistance) {
            Actor_Kill(actor);
        }
    }
}

// Gem maintenance: vacuum + prevent the auto-despawn of XP gems.
bool IsXpGemParams(s16 params) {
    switch (params & 0x00FF) {
        case ITEM00_RUPEE_GREEN:
        case ITEM00_RUPEE_BLUE:
        case ITEM00_RUPEE_RED:
        case ITEM00_RUPEE_PURPLE:
        case ITEM00_RUPEE_ORANGE:
            return true;
        default:
            return false;
    }
}

void UpdateGems() {
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }
    const f32 magnetRadius = GetMagnetRadius();
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_MISC].head; actor != nullptr;
         actor = actor->next) {
        if (actor->id != ACTOR_EN_ITEM00 || !IsXpGemParams(actor->params)) {
            continue;
        }
        EnItem00* item = (EnItem00*)actor;

        f32 dx = player->actor.world.pos.x - actor->world.pos.x;
        f32 dz = player->actor.world.pos.z - actor->world.pos.z;
        f32 dist = sqrtf(dx * dx + dz * dz);

        // Within pickup range: hands off. The game collects the gem on its
        // next update (which resets the timer and kills the actor, firing
        // our XP hook). Touching it here would keep it alive forever.
        if (dist <= 30.0f) {
            continue;
        }

        // Keep the gem alive until collected (never auto-despawn), as long
        // as it is anywhere near the play area.
        if (dist < 800.0f) {
            item->unk_15A = 220;
        }

        // Vacuum: pull gems toward the player within the magnet radius.
        if (dist < magnetRadius) {
            f32 speed = 60.0f;
            actor->world.pos.x += (dx / dist) * speed;
            actor->world.pos.z += (dz / dist) * speed;
            actor->world.pos.y = player->actor.world.pos.y + 20.0f;
        }
    }
}

// XP / level-ups -------------------------------------------------------------

uint32_t GemValue(s16 gemParams) {
    switch (gemParams & 0x00FF) {
        case ITEM00_RUPEE_GREEN:
            return 1;
        case ITEM00_RUPEE_BLUE:
            return 5;
        case ITEM00_RUPEE_RED:
            return 20;
        case ITEM00_RUPEE_PURPLE:
        case ITEM00_RUPEE_ORANGE:
            return 100;
        default:
            return 0;
    }
}

void CheckLevelUp() {
    Session& session = GetSession();
    if (session.state == SessionState::RUNNING && session.xp >= session.xpToNext) {
        SPDLOG_INFO("[OAV] level up! level {} (xp {}/{})", session.level, session.xp, session.xpToNext);
        GenerateLevelUpChoices();
        session.state = SessionState::LEVEL_UP;
    }
}

void AwardXp(uint32_t amount) {
    Session& session = GetSession();
    session.xp += amount;
    CheckLevelUp();
}

// ---------------------------------------------------------------------------
// Great Fairy evolutions (weapon + paired passive both maxed, then an elite kill)
// ---------------------------------------------------------------------------

void PerformEvolution(Upgrade weapon) {
    Session& session = GetSession();
    session.weaponEvolved[static_cast<int32_t>(weapon)] = true;
    session.evolveReady = Upgrade::NONE;
    session.evolutionTimer = 150;
    session.evolutionTitle = (weapon == Upgrade::SPIN) ? "Great Spin Storm" : "Light Arrow Barrage";
    SPDLOG_INFO("[OAV] Great Fairy's Blessing: {} evolved", session.evolutionTitle);
    Audio_PlaySfxGeneral(NA_SE_EV_GREAT_FAIRY_APPEAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

void CheckEvolutionReady() {
    Session& session = GetSession();
    if (session.evolveReady != Upgrade::NONE) {
        return;
    }
    auto lvl = [&session](Upgrade u) { return session.upgradeLevels[static_cast<int32_t>(u)]; };
    bool spinEvolved = session.weaponEvolved[static_cast<int32_t>(Upgrade::SPIN)];
    bool bowEvolved = session.weaponEvolved[static_cast<int32_t>(Upgrade::BOW)];
    if (!spinEvolved && lvl(Upgrade::SPIN) >= 3 && lvl(Upgrade::SWORD) >= 3) {
        session.evolveReady = Upgrade::SPIN;
        SPDLOG_INFO("[OAV] spin fully maxed; the next elite kill grants the Great Spin Storm");
    } else if (!bowEvolved && lvl(Upgrade::BOW) >= 3 && lvl(Upgrade::MAGIC) >= 2) {
        session.evolveReady = Upgrade::BOW;
        SPDLOG_INFO("[OAV] bow fully maxed; the next elite kill grants the Light Arrow Barrage");
    }
}

// Hooks -----------------------------------------------------------------------

void OnEnemyDefeatHandler(void* refActor) {
    if (GetSession().state != SessionState::RUNNING || gPlayState == nullptr) {
        return;
    }
    Actor* actor = (Actor*)refActor;
    Session& session = GetSession();
    session.kills++;

    // Gem value by enemy type: elites drop the big gem, others by tier.
    s16 gem = ITEM00_RUPEE_GREEN;
    bool eliteKill = false;
    for (const auto& def : sRoster) {
        if (def.actorId == actor->id && def.params == actor->params) {
            gem = def.gemType;
            eliteKill = def.elite;
            break;
        }
    }
    SpawnGem(actor, gem);

    // Occasional sustain heart; elites always drop one.
    if (eliteKill || Rand_ZeroOne() < 0.04f) {
        SpawnGem(actor, ITEM00_HEART);
    }

    // The Great Fairy blesses a fully-maxed weapon when its paired elite falls.
    if (eliteKill && session.evolveReady != Upgrade::NONE) {
        PerformEvolution(session.evolveReady);
    }
}

void OnActorKillHandler(void* refActor) {
    Session& session = GetSession();
    if (session.state != SessionState::RUNNING && session.state != SessionState::LEVEL_UP) {
        return;
    }
    Actor* actor = (Actor*)refActor;
    if (actor->id != ACTOR_EN_ITEM00) {
        return;
    }
    uint32_t value = GemValue(actor->params);
    if (value > 0) {
        AwardXp(value);
    }
}

void OnPlayerHealthChangeHandler(int16_t amount) {
    Session& session = GetSession();
    if (session.state != SessionState::RUNNING || amount >= 0) {
        return;
    }
    // The hook fires after gSaveContext.health has been updated.
    if (gSaveContext.health <= 0) {
        // Bottled fairy: auto-revive instead of dying.
        if (session.fairiesRemaining > 0) {
            session.fairiesRemaining--;
            gSaveContext.health = gSaveContext.healthCapacity;
            SPDLOG_INFO("[OAV] bottled fairy consumed ({} left)", session.fairiesRemaining);
            return;
        }
        // Death: freeze the world and show the results screen.
        SPDLOG_INFO("[OAV] game over: survived {} frames, {} kills, level {}", session.frames,
                    session.kills, session.level);
        session.state = SessionState::GAME_OVER;
    }
}

// World freeze while the level-up menu or the results screen is up.
void ShouldActorUpdateHandler(void* refActor, bool* result) {
    Session& session = GetSession();
    if (session.state == SessionState::LEVEL_UP || session.state == SessionState::GAME_OVER) {
        *result = false;
    }
}

// Block scene-native enemies in the arena; the director owns all spawns.
void ShouldActorInitHandler(void* refActor, bool* result) {
    if (GetSession().state == SessionState::INACTIVE || gPlayState == nullptr) {
        return;
    }
    if (sDirectorSpawning) {
        return; // the director's own spawn; allow it
    }
    Actor* actor = (Actor*)refActor;
    if (actor->category == ACTORCAT_ENEMY) {
        *result = false;
    }
}

void RegisterDirectorHooks() {
    COND_HOOK(OnEnemyDefeat, true, OnEnemyDefeatHandler);
    COND_HOOK(OnActorKill, true, OnActorKillHandler);
    COND_HOOK(OnPlayerHealthChange, true, OnPlayerHealthChangeHandler);
    COND_HOOK(ShouldActorUpdate, true, ShouldActorUpdateHandler);
    COND_HOOK(ShouldActorInit, true, ShouldActorInitHandler);

    // Show the built-in HUD gameplay timer (run time) while a run is active.
    REGISTER_VB_SHOULD(VB_SHOW_GAMEPLAY_TIMER, {
        if (IsSessionActive()) {
            *should = true;
        }
    });

    // Never persist a mode save (the run uses scratch save slot 0xFE).
    REGISTER_VB_SHOULD(VB_BE_ABLE_TO_SAVE, {
        if (IsSessionActive()) {
            *should = false;
        }
    });

    // The kaleido pause menu is disabled during a run; the mode HUD and the
    // ImGui menubar (ESC) remain available.
    REGISTER_VB_SHOULD(VB_OPEN_PAUSE_MENU, {
        if (IsSessionActive()) {
            *should = false;
        }
    });
}

} // namespace

float GetMagnetRadius() {
    // Base pickup pull; each Magnet upgrade widens it.
    return 120.0f + 150.0f * GetSession().upgradeLevels[static_cast<int32_t>(Upgrade::MAGNET)];
}

// ---------------------------------------------------------------------------
// Level-up cards
// ---------------------------------------------------------------------------

namespace {

struct UpgradeDef {
    Upgrade id;
    const char* title;
    const char* description;
    int32_t maxLevel;
};

const UpgradeDef sUpgrades[] = {
    { Upgrade::HEART_CONTAINER, "Heart Container", "+1 heart container and a full heal.",
      17 },
    { Upgrade::MAGIC, "Magic Meter", "Grants a magic meter, then double magic (bigger effects).", 2 },
    { Upgrade::SWORD, "Sword", "Kokiri -> Master Sword -> Giant's Knife.", 3 },
    { Upgrade::MAGNET, "Magnet", "Greatly increases the range gems are vacuumed from.", 5 },
    { Upgrade::SPIN, "Spin Attack", "Link periodically charges a spin attack automatically.", 3 },
    { Upgrade::BOW, "Bow", "Arrows fire automatically from the bow. Faster with levels.", 3 },
    { Upgrade::DINS, "Din's Fire", "A wave of fire erupts around Link periodically.", 3 },
    { Upgrade::NAYRUS, "Nayru's Love", "A damaging barrier shields Link whenever it is down.", 3 },
    { Upgrade::BOMBS, "Bombs", "Bombs drop automatically around Link. Mind the blast!", 3 },
    { Upgrade::HOOKSHOT, "Hookshot", "The hookshot lunges at nearby enemies. Longshot at max.",
      3 },
    { Upgrade::BOOTS, "Pegasus Boots", "Link runs faster. Stack for more speed.", 2 },
    { Upgrade::TUNIC, "Fiber Tunic", "Damage taken is reduced. Stack for less.", 2 },
    { Upgrade::FAIRY, "Bottled Fairy", "A fairy auto-revives Link when defeated. One use per level.",
      3 },
};

} // namespace

// HUD helpers: names and levels of the run's upgrades.
const char* GetUpgradeTitle(Upgrade id) {
    for (const auto& def : sUpgrades) {
        if (def.id == id) {
            return def.title;
        }
    }
    return "";
}

int32_t GetUpgradeMaxLevel(Upgrade id) {
    for (const auto& def : sUpgrades) {
        if (def.id == id) {
            return def.maxLevel;
        }
    }
    return 0;
}

const char* GetEvolutionTitle(Upgrade id) {
    switch (id) {
        case Upgrade::SPIN:
            return "Great Spin Storm";
        case Upgrade::BOW:
            return "Light Arrow Barrage";
        default:
            return nullptr;
    }
}

// Elites are the only Iron Knuckles / Flare Dancers in the mode.
bool IsEliteActor(Actor* actor) {
    return actor != nullptr && (actor->id == ACTOR_EN_IK || actor->id == ACTOR_EN_FD);
}

void GenerateLevelUpChoices() {
    Session& session = GetSession();
    session.choices.clear();

    // Prefer unmaxed upgrades; pad with the recovery card.
    std::vector<const UpgradeDef*> available;
    for (const auto& def : sUpgrades) {
        if (session.upgradeLevels[static_cast<int32_t>(def.id)] < def.maxLevel) {
            available.push_back(&def);
        }
    }

    while (session.choices.size() < 4 && !available.empty()) {
        size_t index = static_cast<size_t>(Rand_ZeroOne() * available.size());
        if (index >= available.size()) {
            index = available.size() - 1;
        }
        const UpgradeDef* def = available[index];
        available.erase(available.begin() + index);
        int32_t levelAfter = session.upgradeLevels[static_cast<int32_t>(def->id)] + 1;
        session.choices.push_back(
            { def->id, def->title, def->description, levelAfter });
    }
    while (session.choices.size() < 4) {
        session.choices.push_back({ Upgrade::RECOVERY, "Recovery Heart", "Heal four hearts.", 0 });
    }
}

void ApplyUpgradeChoice(int32_t choiceIndex) {
    Session& session = GetSession();
    if (choiceIndex < 0 || static_cast<size_t>(choiceIndex) >= session.choices.size()) {
        return;
    }
    UpgradeChoice& choice = session.choices[choiceIndex];

    switch (choice.id) {
        case Upgrade::HEART_CONTAINER:
            if (session.upgradeLevels[static_cast<int32_t>(Upgrade::HEART_CONTAINER)] < 17) {
                session.upgradeLevels[static_cast<int32_t>(Upgrade::HEART_CONTAINER)]++;
                gSaveContext.healthCapacity += 16;
                gSaveContext.health = gSaveContext.healthCapacity;
            }
            break;
        case Upgrade::MAGIC: {
            int32_t level = ++session.upgradeLevels[static_cast<int32_t>(Upgrade::MAGIC)];
            if (level == 1) {
                gSaveContext.isMagicAcquired = 1;
                gSaveContext.magicLevel = 1;
                gSaveContext.magic = 48;
            } else {
                gSaveContext.isDoubleMagicAcquired = 1;
                gSaveContext.magicLevel = 2;
                gSaveContext.magic = 96;
            }
            break;
        }
        case Upgrade::SWORD: {
            int32_t level = ++session.upgradeLevels[static_cast<int32_t>(Upgrade::SWORD)];
            if (level == 1) {
                gSaveContext.inventory.equipment |= 1 << 1; // Master Sword
                gSaveContext.equips.buttonItems[0] = ITEM_SWORD_MASTER;
                Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER);
            } else {
                gSaveContext.inventory.equipment |= 1 << 2; // Giant's Knife
                gSaveContext.bgsFlag = 1;
                gSaveContext.equips.buttonItems[0] = ITEM_SWORD_BGS;
                Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON);
            }
            break;
        }
        case Upgrade::MAGNET:
            session.upgradeLevels[static_cast<int32_t>(Upgrade::MAGNET)]++;
            break;
        case Upgrade::SPIN:
        case Upgrade::BOW:
        case Upgrade::DINS:
        case Upgrade::NAYRUS:
        case Upgrade::BOMBS:
        case Upgrade::HOOKSHOT:
            session.upgradeLevels[static_cast<int32_t>(choice.id)]++;
            GrantWeapon(choice.id);
            break;
        case Upgrade::BOOTS:
        case Upgrade::TUNIC:
            session.upgradeLevels[static_cast<int32_t>(choice.id)]++;
            ApplyPassiveEffects();
            break;
        case Upgrade::FAIRY:
            session.upgradeLevels[static_cast<int32_t>(Upgrade::FAIRY)]++;
            session.fairiesRemaining++;
            break;
        case Upgrade::RECOVERY: {
            s32 healed = gSaveContext.health + 16 * 4;
            gSaveContext.health = healed > gSaveContext.healthCapacity ? gSaveContext.healthCapacity : healed;
            break;
        }
        default:
            break;
    }

    // Consume the level and either re-offer (banked XP) or resume.
    session.level++;
    session.xp -= session.xpToNext;
    session.xpToNext = XpForLevel(session.level);
    session.choices.clear();
    if (session.xp >= session.xpToNext) {
        GenerateLevelUpChoices();
        // remain in LEVEL_UP state with a fresh set of cards
    } else {
        session.state = SessionState::RUNNING;
    }
}

void UpdateSessionFrame() {
    if (gPlayState == nullptr) {
        return;
    }
    CheckLevelUp(); // catch level-ups from any XP source
    CheckEvolutionReady();

    // Test hook: OAV_AUTOEVOLVE=1 maxes both evolution pairs shortly after
    // the run starts and blesses the weapons directly (headless verification).
    static bool sAutoEvolve = getenv("OAV_AUTOEVOLVE") != nullptr;
    if (sAutoEvolve) {
        Session& session = GetSession();
        if (session.frames == 600) {
            session.upgradeLevels[static_cast<int32_t>(Upgrade::SPIN)] = 3;
            session.upgradeLevels[static_cast<int32_t>(Upgrade::SWORD)] = 3;
            session.upgradeLevels[static_cast<int32_t>(Upgrade::BOW)] = 3;
            session.upgradeLevels[static_cast<int32_t>(Upgrade::MAGIC)] = 2;
            GrantWeapon(Upgrade::BOW);
            gSaveContext.isMagicAcquired = 1;
            gSaveContext.magicLevel = 1;
            gSaveContext.magic = 48;
            if (session.evolveReady != Upgrade::NONE) {
                PerformEvolution(session.evolveReady);
            }
        } else if (session.frames == 700 || session.frames == 800) {
            if (session.evolveReady != Upgrade::NONE) {
                PerformEvolution(session.evolveReady);
            }
        }
    }
    UpdateDirector();
    DespawnDistantEnemies();
    UpdateGems();
    UpdateWeapons();
}

void RegisterDirector() {
    RegisterDirectorHooks();
}

} // namespace OAV
