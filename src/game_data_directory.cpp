#include "opena2/game_data_directory.hpp"

#include <system_error>
#include <utility>

namespace opena2 {
namespace {

bool isWithinRoot(const std::filesystem::path& root, const std::filesystem::path& path)
{
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

GameDataDirectory::GameDataDirectory(std::filesystem::path root)
    : root_(std::move(root))
{
}

std::optional<GameDataDirectory> GameDataDirectory::open(
    const std::filesystem::path& path,
    std::string& error)
{
    error.clear();
    std::error_code filesystemError;
    const auto canonicalRoot = std::filesystem::canonical(path, filesystemError);
    if (filesystemError) {
        error = "Could not resolve selected folder: " + filesystemError.message();
        return std::nullopt;
    }

    if (!std::filesystem::is_directory(canonicalRoot, filesystemError)) {
        error = filesystemError
            ? "Could not inspect selected folder: " + filesystemError.message()
            : "The selected path is not a folder.";
        return std::nullopt;
    }

    if (!std::filesystem::is_regular_file(canonicalRoot / "A2.EXE", filesystemError)) {
        error = filesystemError
            ? "Could not inspect A2.EXE: " + filesystemError.message()
            : "The selected folder does not contain A2.EXE.";
        return std::nullopt;
    }

    return GameDataDirectory(canonicalRoot);
}

const std::filesystem::path& GameDataDirectory::root() const noexcept
{
    return root_;
}

std::optional<std::filesystem::path> GameDataDirectory::findFile(
    const std::filesystem::path& relativePath,
    std::string& error) const
{
    error.clear();
    if (relativePath.empty() || relativePath.is_absolute() || relativePath.has_root_name()) {
        error = "Game data paths must be non-empty relative paths.";
        return std::nullopt;
    }

    std::error_code filesystemError;
    const auto candidate = std::filesystem::weakly_canonical(root_ / relativePath, filesystemError);
    if (filesystemError) {
        error = "Could not resolve game data path: " + filesystemError.message();
        return std::nullopt;
    }

    if (!isWithinRoot(root_, candidate)) {
        error = "Game data path resolves outside the selected folder.";
        return std::nullopt;
    }

    if (!std::filesystem::is_regular_file(candidate, filesystemError)) {
        error = filesystemError
            ? "Could not inspect game data file: " + filesystemError.message()
            : "Game data file does not exist.";
        return std::nullopt;
    }

    return candidate;
}

} // namespace opena2
