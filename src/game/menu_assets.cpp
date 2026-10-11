#include "menu_assets.hpp"

#include "opena2/indexed_archive.hpp"

#include <algorithm>
#include <cctype>

namespace opena2::game {

std::string upperCase(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::toupper(character));
                   });
    return result;
}

bool MenuAssets::load(const GameDataDirectory& directory, std::string& error)
{
    auto named = IndexedArchive::openNamed(directory, "A2MRCS.IMG", "A2MRCS.IND", error);
    if (!named) {
        return false;
    }
    for (std::size_t index = 0; index < named->entryCount(); ++index) {
        const auto name = named->entryName(index);
        if (!name) {
            continue;
        }
        const auto key = upperCase(*name);
        const bool image = key.ends_with(".PCX");
        const bool sound = key.ends_with(".WAV");
        if (!image && !sound) {
            continue;
        }
        auto bytes = named->readEntry(index, error);
        if (!bytes) {
            return false;
        }
        if (image) {
            auto decoded = decodePcx(*bytes, error);
            if (!decoded) {
                return false;
            }
            screens.emplace(key, std::move(*decoded));
        } else if (auto wave = decodePcmWave(*bytes, error)) {
            sounds.emplace(key, std::move(*wave));
        }
    }
    for (const auto* required : {"HOOFD.PCX", "NSPEL_1.PCX", "NSPEL_2.PCX", "OPTIES.PCX",
                                 "BEDIEN.PCX", "SCORE.PCX", "LOGO256.PCX"}) {
        if (!screens.contains(required)) {
            error = std::string("The A2MRCS archive does not contain ") + required + ".";
            return false;
        }
    }

    auto archive = IndexedArchive::open(directory, "A2MRC.IMG", "A2MRC.IND", error);
    if (!archive) {
        return false;
    }
    sprites.resize(archive->entryCount());
    for (std::size_t index = 0; index < archive->entryCount(); ++index) {
        auto bytes = archive->readEntry(index, error);
        if (!bytes) {
            return false;
        }
        sprites[index] = decodeIndexedImage(*bytes, error);
    }
    error.clear();
    return true;
}

const PcmWave* MenuAssets::sound(std::string_view name) const
{
    const auto found = sounds.find(upperCase(name));
    return found == sounds.end() ? nullptr : &found->second;
}

} // namespace opena2::game
