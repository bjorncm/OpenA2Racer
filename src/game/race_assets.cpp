#include "race_assets.hpp"

#include "menu_assets.hpp"

#include "opena2/menu_data.hpp"

#include <SDL3/SDL_log.h>

namespace opena2::game {

bool RaceAssets::open(const GameDataDirectory& directory, std::string& error)
{
    directory_ = &directory;
    archive_ = IndexedArchive::openNamed(directory, "RCS.IMG", "RCS.IND", error);
    if (!archive_) {
        return false;
    }
    for (std::size_t index = 0; index < archive_->entryCount(); ++index) {
        if (const auto name = archive_->entryName(index)) {
            entries_.emplace(upperCase(*name), index);
        }
    }
    return true;
}

std::optional<std::vector<std::byte>> RaceAssets::bytes(std::string_view name,
                                                        std::string& error) const
{
    const auto key = upperCase(name);
    if (archive_) {
        if (const auto found = entries_.find(key); found != entries_.end()) {
            return archive_->readEntry(found->second, error);
        }
    }
    if (directory_ == nullptr) {
        error = "The race resources are not open.";
        return std::nullopt;
    }
    const auto path = directory_->findFile(std::string(name), error);
    if (!path) {
        return std::nullopt;
    }
    auto data = readFileBytes(*path);
    if (!data) {
        error = "Could not read " + path->string() + ".";
        return std::nullopt;
    }
    std::vector<std::byte> result(data->size());
    for (std::size_t i = 0; i < data->size(); ++i) {
        result[i] = static_cast<std::byte>((*data)[i]);
    }
    return result;
}

const D3DMesh* RaceAssets::mesh(std::string_view name)
{
    const auto key = upperCase(name);
    auto [entry, inserted] = meshes_.try_emplace(key);
    if (inserted) {
        std::string error;
        if (auto data = bytes(name, error)) {
            D3DMesh decoded = decodeD3DMesh(*data, error);
            if (error.empty()) {
                entry->second = std::move(decoded);
            }
        }
        if (!entry->second) {
            SDL_Log("Race mesh %s: %s", key.c_str(), error.c_str());
        }
    }
    return entry->second ? &*entry->second : nullptr;
}

const IndexedImage* RaceAssets::texture(std::string_view name)
{
    const auto key = upperCase(name);
    auto [entry, inserted] = textures_.try_emplace(key);
    if (inserted) {
        std::string error;
        if (auto data = bytes(name, error)) {
            entry->second = decodePcx(*data, error);
        }
        if (!entry->second) {
            SDL_Log("Race texture %s: %s", key.c_str(), error.c_str());
        }
    }
    return entry->second ? &*entry->second : nullptr;
}

const PcmWave* RaceAssets::sound(std::string_view name)
{
    const auto key = upperCase(name);
    auto [entry, inserted] = sounds_.try_emplace(key);
    if (inserted) {
        std::string error;
        if (auto data = bytes(name, error)) {
            entry->second = decodePcmWave(*data, error);
        }
        if (!entry->second) {
            SDL_Log("Race sound %s: %s", key.c_str(), error.c_str());
        }
    }
    return entry->second ? &*entry->second : nullptr;
}

} // namespace opena2::game
