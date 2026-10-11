#pragma once

#include "display.hpp"
#include "race_assets.hpp"

#include "opena2/game_data_directory.hpp"
#include "opena2/race/hud.hpp"

#include <span>
#include <string>
#include <utility>
#include <vector>

namespace opena2::game {

// The race HUD on the 8-bit surfaces: the GAMESPR images (load_menu_sprites,
// 0x408f1a), the dashboard picture SURSCRN.PCX ("load dashboard to SCREEN")
// and the radar (radar_dispatch, 0x40f7c1). Drawn in the game palette.
class RaceHud {
public:
    RaceHud() = default;
    RaceHud(const RaceHud&) = delete;
    RaceHud& operator=(const RaceHud&) = delete;
    ~RaceHud();

    bool load(Display& display, const GameDataDirectory& directory, RaceAssets& assets,
              bool dashboard, std::string& error);

    // radar_dispatch then draw_hud, onto the front surface.
    void draw(const race::HudValues& values, bool radar, const race::Vec3& player, int heading,
              std::span<const race::RadarCar> cars);
    // The state_dispatch overlays, after draw.
    void drawBlits(std::span<const race::HudBlit> blits);
    bool dashboard() const noexcept { return dashboard_; }

private:
    Display* display_ = nullptr;
    bool dashboard_ = false;
    // Sprite index of GAMESPR image 1; the dashboard and radar sprites.
    int first_ = -1;
    int dashboardSprite_ = -1;
    int radarSprite_ = -1;
    // The x and y origin of each GAMESPR image, for blits at the origin.
    std::vector<std::pair<int, int>> origins_;
    race::RadarLayout radar_;
    IndexedImage radarImage_{};
};

} // namespace opena2::game
