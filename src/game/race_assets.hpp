#pragma once

#include "opena2/d3d_mesh.hpp"
#include "opena2/game_data_directory.hpp"
#include "opena2/indexed_archive.hpp"
#include "opena2/indexed_image.hpp"
#include "opena2/pcm_wave.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opena2::game {

// Race resources. Like the original's file_open,
// a name is looked up in RCS.IMG/RCS.IND first, case-insensitively, and then
// as a loose file in the game folder (the .TRK and .OMP files).
class RaceAssets {
public:
    bool open(const GameDataDirectory& directory, std::string& error);

    std::optional<std::vector<std::byte>> bytes(std::string_view name, std::string& error) const;
    // Decoded once and cached; nullptr when missing or invalid.
    const D3DMesh* mesh(std::string_view name);
    const IndexedImage* texture(std::string_view name);
    const PcmWave* sound(std::string_view name);

private:
    const GameDataDirectory* directory_ = nullptr;
    std::optional<IndexedArchive> archive_;
    std::map<std::string, std::size_t> entries_;
    std::map<std::string, std::optional<D3DMesh>> meshes_;
    std::map<std::string, std::optional<IndexedImage>> textures_;
    std::map<std::string, std::optional<PcmWave>> sounds_;
};

} // namespace opena2::game
