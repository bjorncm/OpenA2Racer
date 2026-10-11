#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace opena2 {

class GameDataDirectory {
public:
    static std::optional<GameDataDirectory> open(
        const std::filesystem::path& path,
        std::string& error);

    const std::filesystem::path& root() const noexcept;
    std::optional<std::filesystem::path> findFile(
        const std::filesystem::path& relativePath,
        std::string& error) const;

private:
    explicit GameDataDirectory(std::filesystem::path root);

    std::filesystem::path root_;
};

} // namespace opena2
