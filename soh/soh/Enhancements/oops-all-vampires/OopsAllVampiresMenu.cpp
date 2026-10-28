#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/oops-all-vampires/OopsAllVampires.h"
#include "soh/SohGui/MenuTypes.h"
#include "soh/SohGui/SohMenu.h"

extern "C" {
#include "variables.h"
}

// ============================================================================
// Oops! All Vampires? — menubar integration.
//
// Registers a dedicated top-level menu section with the run launcher and the
// mode's settings. Uses the MenuInit registry so no existing menu file has
// to be modified.
// ============================================================================

namespace SohGui {

extern std::shared_ptr<SohMenu> mSohMenu;

using namespace UIWidgets;

namespace {

const std::map<int32_t, const char*> oavArenas = {
    { 0, "Hyrule Field" },
    { 1, "Lake Hylia" },
    { 2, "Gerudo Valley" },
    { 3, "Lon Lon Ranch" },
};

const std::map<int32_t, const char*> oavDifficulties = {
    { 0, "Normal" },
    { 1, "Hard" },
};

void RegisterOopsAllVampiresMenu() {
    if (mSohMenu == nullptr) {
        return;
    }

    // Top-level section
    mSohMenu->AddMenuEntry("Oops! All Vampires?", CVAR_SETTING("Menu.OopsAllVampiresSidebarSection"));

    WidgetPath path = { "Oops! All Vampires?", "Run", SECTION_COLUMN_1 };
    mSohMenu->AddSidebarEntry("Oops! All Vampires?", path.sidebarName, 1);

    mSohMenu->AddWidget(path, "A Vampire Survivors style time-survival mode", WIDGET_SEPARATOR_TEXT);
    mSohMenu->AddWidget(path, "Survive endless waves of Hyrule's monsters in an open field. "
                              "Collect XP, level up, and build an automatic loadout out of Ocarina of Time's "
                              "weapons and equipment.",
                        WIDGET_TEXT);

    mSohMenu->AddWidget(path, "Start Run", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            OAV_StartRun();
        })
        .Options(ButtonOptions().Tooltip("Start a new Oops! All Vampires? run. Any unsaved progress in the "
                                         "current session is discarded."));

    mSohMenu->AddWidget(path, "Arena", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ENHANCEMENT("OopsAllVampires.Arena"))
        .Options(ComboboxOptions()
                     .ComboMap(oavArenas)
                     .DefaultIndex(0)
                     .Tooltip("The field the run takes place in.")
                     .Color(Colors::Gray));

    mSohMenu->AddWidget(path, "Difficulty", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ENHANCEMENT("OopsAllVampires.Difficulty"))
        .Options(ComboboxOptions()
                     .ComboMap(oavDifficulties)
                     .DefaultIndex(0)
                     .Tooltip("Hard multiplies spawn rates and enemy damage.")
                     .Color(Colors::Gray));
}

} // namespace
} // namespace SohGui

static RegisterMenuInitFunc menuInitFunc([]() { SohGui::RegisterOopsAllVampiresMenu(); });
