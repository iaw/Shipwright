#pragma once

#include <ship/window/gui/GuiWindow.h>

// ImGui overlay for Oops! All Vampires?: HUD strip, level-up cards and the
// results screen. The window force-renders itself whenever a run is active,
// independent of the normal window visibility state.
class OavHudWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override{};
    void DrawElement() override{};
    void Draw() override;
    void UpdateElement() override{};
};
