#pragma once

#include "opena2/game_data_directory.hpp"
#include "opena2/indexed_image.hpp"
#include "opena2/pcm_wave.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opena2::game {

// Original menu resources, loaded from the user's game folder: the
// full-screen PCX backgrounds and WAV sounds of A2MRCS, and the A2MRC sprites.
struct MenuAssets {
    std::map<std::string, IndexedImage> screens;
    std::map<std::string, PcmWave> sounds;
    std::vector<std::optional<IndexedImage>> sprites;

    bool load(const GameDataDirectory& directory, std::string& error);

    // Sound by case-insensitive archive name, or nullptr.
    const PcmWave* sound(std::string_view name) const;
};

std::string upperCase(std::string_view text);

} // namespace opena2::game
