#pragma once

#include "opena2/game_data_directory.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opena2 {

class IndexedArchive {
public:
    static std::optional<IndexedArchive> open(
        const GameDataDirectory& gameData,
        const std::filesystem::path& imagePath,
        const std::filesystem::path& indexPath,
        std::string& error);
    static std::optional<IndexedArchive> openNamed(
        const GameDataDirectory& gameData,
        const std::filesystem::path& imagePath,
        const std::filesystem::path& indexPath,
        std::string& error);

    std::size_t entryCount() const noexcept;
    std::optional<std::string_view> entryName(std::size_t index) const noexcept;
    std::optional<std::vector<std::byte>> readEntry(
        std::size_t index,
        std::string& error) const;

private:
    IndexedArchive(
        std::filesystem::path imagePath,
        std::uint64_t imageSize,
        std::vector<std::uint32_t> offsets,
        std::vector<std::string> names = {});

    std::filesystem::path imagePath_;
    std::uint64_t imageSize_;
    std::vector<std::uint32_t> offsets_;
    std::vector<std::string> names_;
};

} // namespace opena2
