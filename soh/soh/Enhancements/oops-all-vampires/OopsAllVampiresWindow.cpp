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
        uint32_t totalSeconds = session.frames / 20;
        uint32_t minutes = totalSeconds / 60;
        uint32_t seconds = totalSeconds % 60;

        ImGui::Text("Oops! All Vampires?");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "%u:%02u", minutes, seconds);

        ImGui::SameLine();
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
    }
    ImGui::End();

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

        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "You were overrun!");
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

void OavHudWindow::Draw() {
    if (!IsSessionActive()) {
        return;
    }

    Session& session = GetSession();
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
