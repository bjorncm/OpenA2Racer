#include "game/platform.hpp"

#include <SDL3/SDL.h>

#include <string>

namespace opena2::game {

std::filesystem::path appBundle()
{
    // In a bundle SDL_GetBasePath is <App>.app/Contents/Resources/.
    const char* base = SDL_GetBasePath();
    if (base == nullptr) {
        return {};
    }
    for (std::filesystem::path path(reinterpret_cast<const char8_t*>(base)); path.has_relative_path();
         path = path.parent_path()) {
        if (path.extension() == ".app") {
            return path;
        }
    }
    return {};
}

#ifndef __APPLE__
bool moveToTrash(const std::filesystem::path&)
{
    return false;
}
#endif

} // namespace opena2::game
