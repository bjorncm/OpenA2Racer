#pragma once

#include <filesystem>

namespace opena2::game {

// The .app bundle the program runs from (macOS), or an empty path.
std::filesystem::path appBundle();

// Moves `path` to the Trash, so it can still be put back. False when this
// is not possible (only macOS has it here).
bool moveToTrash(const std::filesystem::path& path);

} // namespace opena2::game
