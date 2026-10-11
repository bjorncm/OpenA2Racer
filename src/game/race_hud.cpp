#include "race_hud.hpp"

#include "opena2/indexed_archive.hpp"

#include <optional>
#include <vector>

namespace opena2::game {
namespace {

// Rows `top`..end of a picture as a sprite.
IndexedImage bottomRows(const IndexedImage& image, int top)
{
    IndexedImage part{image.width, static_cast<std::uint16_t>(image.height - top), 0, 0, {}, {}};
    part.pixels.assign(image.pixels.begin() + static_cast<std::ptrdiff_t>(top) * image.width,
                       image.pixels.end());
    return part;
}

} // namespace

RaceHud::~RaceHud()
{
    if (display_ != nullptr && first_ >= 0) {
        display_->removeSprites(first_);
    }
}

bool RaceHud::load(Display& display, const GameDataDirectory& directory, RaceAssets& assets,
                   bool dashboard, std::string& error)
{
    auto archive = IndexedArchive::open(directory, "GAMESPR.IMG", "GAMESPR.IND", error);
    if (!archive) {
        return false;
    }
    std::vector<std::optional<IndexedImage>> images(archive->entryCount());
    origins_.assign(images.size(), {0, 0});
    for (std::size_t i = 0; i < images.size(); ++i) {
        std::string entryError;
        if (const auto bytes = archive->readEntry(i, entryError)) {
            images[i] = decodeIndexedImage(*bytes, entryError);
        }
        if (images[i]) {
            origins_[i] = {images[i]->anchorX, images[i]->anchorY};
        }
    }

    // With the dashboard the 3D view covers the top 300 rows, so only the
    // rows below it show.
    if (dashboard) {
        const IndexedImage* picture = assets.texture("surscrn.pcx");
        if (picture == nullptr || picture->height <= race::viewportHeight(true)) {
            error = "SURSCRN.PCX is missing.";
            return false;
        }
        images.push_back(bottomRows(*picture, race::viewportHeight(true)));
    }
    radar_ = race::radarLayout(dashboard);
    radarImage_ = {static_cast<std::uint16_t>(radar_.width), static_cast<std::uint16_t>(radar_.height),
                   0, 0, {}, {}};
    radarImage_.pixels.assign(static_cast<std::size_t>(radar_.width * radar_.height), 0);
    images.push_back(radarImage_);

    display_ = &display;
    dashboard_ = dashboard;
    first_ = display.addSprites(images);
    const int count = static_cast<int>(images.size());
    radarSprite_ = first_ + count - 1;
    dashboardSprite_ = dashboard ? first_ + count - 2 : -1;
    return true;
}

void RaceHud::draw(const race::HudValues& values, bool radar, const race::Vec3& player,
                   int heading, std::span<const race::RadarCar> cars)
{
    if (display_ == nullptr) {
        return;
    }
    if (dashboardSprite_ >= 0) {
        display_->sprite(Surface::front, dashboardSprite_, 0, race::viewportHeight(true));
    }
    // DAT_00458c24, F5.
    if (radar) {
        radarImage_.pixels = race::radarPixels(radar_, player, heading, cars);
        display_->updateSprite(radarSprite_, radarImage_);
        display_->sprite(Surface::front, radarSprite_, radar_.x, radar_.y);
    }
    drawBlits(race::hudBlits(dashboard_, values));
}

void RaceHud::drawBlits(std::span<const race::HudBlit> blits)
{
    if (display_ == nullptr) {
        return;
    }
    // blit_surface draws the off-screen images with their colour key 0xff
    // in every mode.
    for (const auto& blit : blits) {
        const auto index = static_cast<std::size_t>(blit.image - 1);
        const auto [originX, originY] = blit.atOrigin && index < origins_.size()
            ? origins_[index] : std::pair<int, int>{0, 0};
        display_->sprite(Surface::front, first_ + blit.image - 1, blit.x - originX, blit.y - originY);
    }
}

} // namespace opena2::game
