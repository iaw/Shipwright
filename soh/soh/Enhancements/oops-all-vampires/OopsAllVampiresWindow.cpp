#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/oops-all-vampires/OopsAllVampires.h"
#include "soh/Enhancements/oops-all-vampires/OopsAllVampiresWindow.h"

#include <imgui.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// ============================================================================
// Oops! All Vampires? — HUD, level-up cards and results overlay.
// ============================================================================

namespace {

using namespace OAV;

// Full-screen radar margin for the elite indicators (px).
constexpr float ELITE_EDGE_PADDING = 40.0f;

// Draws edge arrows pointing at every live elite so nothing important ever
// lives silently off-screen. Elites are the mode's evolution carriers.
void DrawEliteIndicators() {
    if (gPlayState == nullptr) {
        return;
    }
    Camera* cam = GET_ACTIVE_CAM(gPlayState);
    Player* player = GET_PLAYER(gPlayState);
    if (cam == nullptr || player == nullptr) {
        return;
    }

    // Camera facing yaw, then each elite's bearing relative to it.
    float camYaw = Math_Atan2S(cam->at.x - cam->eye.x, cam->at.z - cam->eye.z) * (M_PI / 180.0f);
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float cx = displaySize.x * 0.5f;
    float cy = displaySize.y * 0.5f;
    float radius = cx - ELITE_EDGE_PADDING;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; actor != nullptr;
         actor = actor->next) {
        if (!IsEliteActor(actor)) {
            continue;
        }
        float bearing = Math_Atan2S(actor->world.pos.x - player->actor.world.pos.x,
                                     actor->world.pos.z - player->actor.world.pos.z) *
                        (M_PI / 180.0f);
        float angle = bearing - camYaw;

        // Arrow head on the ring, pointing outward toward the elite.
        float px = cx + sinf(angle) * radius;
        float py = cy - cosf(angle) * radius;
        float tipX = cx + sinf(angle) * (radius + 14.0f);
        float tipY = cy - cosf(angle) * (radius + 14.0f);
        float perpX = cosf(angle) * 9.0f;
        float perpY = sinf(angle) * 9.0f;
        ImU32 color = IM_COL32(255, 80, 120, 220);
        dl->AddTriangleFilled(ImVec2(tipX, tipY), ImVec2(px - perpX, py - perpY),
                              ImVec2(px + perpX, py + perpY), color);
    }
}

// Inventory panel (top-left): every owned weapon and passive with its level,
// plus the evolved state of blessed weapons.
void DrawInventory(Session& session) {
    bool anyOwned = false;
    for (int32_t i = 0; i < static_cast<int32_t>(Upgrade::COUNT_); i++) {
        if (session.upgradeLevels[i] > 0 || session.weaponEvolved[i]) {
            anyOwned = true;
            break;
        }
    }
    if (!anyOwned) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

    if (ImGui::Begin("OAV Inventory", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        for (int32_t i = 0; i < static_cast<int32_t>(Upgrade::COUNT_); i++) {
            Upgrade id = static_cast<Upgrade>(i);
            bool evolved = session.weaponEvolved[i];
            if (session.upgradeLevels[i] <= 0 && !evolved) {
                continue;
            }
            if (evolved) {
                const char* evo = GetEvolutionTitle(id);
                ImGui::TextColored(ImVec4(0.7f, 0.6f, 1.0f, 1.0f), "* %s", evo != nullptr ? evo : "");
            } else {
                ImGui::Text("%s %d/%d", GetUpgradeTitle(id), session.upgradeLevels[i],
                            GetUpgradeMaxLevel(id));
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void DrawHud(Session& session) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, 12.0f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

    if (ImGui::Begin("OAV HUD", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        // Hearts and the run timer come from the built-in HUD (vanilla hearts +
        // Interface_DrawTotalGameplayTimer); this strip only carries the stats
        // that have no built-in widget.
        ImGui::Text("LV %u", session.level);

        ImGui::SameLine();
        char xpLabel[64];
        snprintf(xpLabel, sizeof(xpLabel), "XP %u/%u", session.xp, session.xpToNext);
        float barWidth = 160.0f;
        ImGui::SetNextItemWidth(barWidth);
        ImGui::ProgressBar((float)session.xp / (float)session.xpToNext, ImVec2(barWidth, 14.0f), xpLabel);

        ImGui::SameLine();
        ImGui::Text("Kills %u", session.kills);

        ImGui::SameLine();
        ImGui::Text("Wave %u", session.wave);

        // Evolution hint: a maxed pair is waiting on its elite kill.
        if (session.evolveReady != Upgrade::NONE) {
            const char* evo = GetEvolutionTitle(session.evolveReady);
            ImGui::TextColored(ImVec4(0.7f, 0.6f, 1.0f, 1.0f), "EVOLUTION READY: slay an ELITE! (%s)",
                               evo != nullptr ? evo : "");
        }
    }
    ImGui::End();

    DrawInventory(session);
    DrawEliteIndicators();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void DrawLevelUp(Session& session) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // Dim the frozen game behind the card menu.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::SetNextWindowBgAlpha(0.55f);
    if (ImGui::Begin("OAV LevelUp Dim", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

    if (ImGui::Begin("OAV Level Up", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "LEVEL UP!  (Level %u)", session.level);
        ImGui::Separator();
        ImGui::Text("Choose an upgrade:");
        ImGui::Spacing();

        for (size_t i = 0; i < session.choices.size(); i++) {
            const UpgradeChoice& choice = session.choices[i];
            ImGui::PushID((int)i);
            if (ImGui::Button(choice.title, ImVec2(520, 0))) {
                ApplyUpgradeChoice((int32_t)i);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", choice.description);
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void DrawGameOver(Session& session) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

    if (ImGui::Begin("OAV Game Over", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        uint32_t totalSeconds = session.frames / 20;
        uint32_t minutes = totalSeconds / 60;
        uint32_t seconds = totalSeconds % 60;

        if (session.victory) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "You survived the night!");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "You were overrun!");
        }
        ImGui::Separator();
        ImGui::Text("Survived:  %u:%02u", minutes, seconds);
        ImGui::Text("Level:     %u", session.level);
        ImGui::Text("Kills:     %u", session.kills);
        ImGui::Text("Wave:      %u", session.wave);
        ImGui::Separator();
        {
            uint32_t bt = (uint32_t)CVarGetInteger(CVAR_ENHANCEMENT("OopsAllVampires.BestTime"), 0);
            uint32_t bk = (uint32_t)CVarGetInteger(CVAR_ENHANCEMENT("OopsAllVampires.BestKills"), 0);
            ImGui::TextDisabled("Best: %u:%02u, %u kills", bt / 60, bt % 60, bk);
        }
        ImGui::Separator();

        if (ImGui::Button("End Run", ImVec2(180, 0))) {
            // Back to the file select screen; the session tears down on exit.
            gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
            SET_NEXT_GAMESTATE(gGameState, FileChoose_Init, FileChooseContext);
            gGameState->running = false;
            OAV_EndRun();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Playing", ImVec2(180, 0))) {
            // Sandbox: keep fighting in the arena with the run ended.
            OAV_EndRun();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace

void DrawEvolutionBanner(Session& session) {
    if (session.evolutionTimer <= 0 || session.evolutionTitle == nullptr) {
        return;
    }
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.3f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.05f, 0.2f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

    if (ImGui::Begin("OAV Evolution", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav)) {
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.8f, 1.0f), "Great Fairy's Blessing!");
        ImGui::Separator();
        ImGui::Text("%s evolved", session.evolutionTitle);
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void OavHudWindow::Draw() {
    if (!IsSessionActive()) {
        return;
    }

    Session& session = GetSession();
    DrawEvolutionBanner(session);
    switch (session.state) {
        case SessionState::RUNNING:
            DrawHud(session);
            break;
        case SessionState::LEVEL_UP:
            DrawHud(session);
            DrawLevelUp(session);
            break;
        case SessionState::GAME_OVER:
            DrawGameOver(session);
            break;
        default:
            break;
    }
}
