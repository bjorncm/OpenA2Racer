cmake_minimum_required(VERSION 3.24)

# The oldest macOS the game is built for (also LSMinimumSystemVersion of the
# app). Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(OpenA2Racer VERSION 1.0.0 LANGUAGES C CXX)

# Windows builds made with a cross-compiler (cmake/toolchains/) build SDL3
# and FreeType from their release sources and link everything statically,
# so the game is one .exe.
if(WIN32 AND CMAKE_CROSSCOMPILING)
    set(opena2_bundled_default ON)
else()
    set(opena2_bundled_default OFF)
endif()
option(OPENA2_BUNDLED_DEPENDENCIES "Build SDL3 and FreeType from source, statically" ${opena2_bundled_default})

if(OPENA2_BUNDLED_DEPENDENCIES)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    # Only the game is installed, not the libraries' files.
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
        URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    )
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype
        URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz
        URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
    )
    FetchContent_MakeAvailable(SDL3 freetype)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(Freetype_FOUND TRUE)
else()
    find_package(SDL3 3.2 CONFIG REQUIRED)
    find_package(Freetype)
endif()
find_package(OpenGL REQUIRED)

# libsmacker (LGPL-2.1) decodes the original Smacker movies. Set
# OPENA2_LIBSMACKER_SOURCE_DIR to a local checkout to build without a download.
set(OPENA2_LIBSMACKER_SOURCE_DIR "" CACHE PATH "Local libsmacker source checkout")
if(OPENA2_LIBSMACKER_SOURCE_DIR)
    set(libsmacker_SOURCE_DIR "${OPENA2_LIBSMACKER_SOURCE_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(libsmacker
        GIT_REPOSITORY https://github.com/greg-kennedy/libsmacker.git
        GIT_TAG 76094fb9c8e98bd5fac982c504e8d9aeff3ece01
        # libsmacker has no CMake project; only fetch its sources.
        SOURCE_SUBDIR no-cmake-project
    )
    FetchContent_MakeAvailable(libsmacker)
endif()
add_library(smacker STATIC "${libsmacker_SOURCE_DIR}/smacker.c")
target_include_directories(smacker PUBLIC "${libsmacker_SOURCE_DIR}")
set_target_properties(smacker PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

add_library(opena2_data
    src/d3d_mesh.cpp
    src/game_data_directory.cpp
    src/indexed_archive.cpp
    src/indexed_image.cpp
    src/installation.cpp
    src/menu_data.cpp
    src/pcm_wave.cpp
    src/screen_transform.cpp
    src/smacker_movie.cpp
    src/vehicle.cpp
    src/race/collision.cpp
    src/race/hud.cpp
    src/race/pickups.cpp
    src/race/player_car.cpp
    src/race/race_sound.cpp
    src/race/race_state.cpp
    src/race/scene.cpp
    src/race/stage.cpp
    src/race/track.cpp
    src/race/traffic.cpp
)
target_include_directories(opena2_data PUBLIC include)
target_compile_features(opena2_data PUBLIC cxx_std_20)
target_link_libraries(opena2_data PRIVATE smacker)

add_executable(opena2_asset_browser
    src/main.cpp
)

add_executable(opena2
    src/game_main.cpp
    src/game/gl_functions.cpp
    src/game/menu_assets.cpp
    src/game/display.cpp
    src/game/sound.cpp
    src/game/keys.cpp
    src/game/runtime.cpp
    src/game/forms.cpp
    src/game/menu_program.cpp
    src/game/menu_main.cpp
    src/game/menu_players.cpp
    src/game/menu_requesters.cpp
    src/game/menu_settings.cpp
    src/game/menu_scores.cpp
    src/game/launcher.cpp
    src/game/race_assets.cpp
    src/game/race_audio.cpp
    src/game/race_hud.cpp
    src/game/race_program.cpp
    src/game/race_renderer.cpp
    src/game/start_window.cpp
    src/game/platform.cpp
)
target_include_directories(opena2 PRIVATE src)

foreach(target opena2 opena2_asset_browser)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE opena2_data SDL3::SDL3 OpenGL::GL)
    if(APPLE)
        target_compile_definitions(${target} PRIVATE GL_SILENCE_DEPRECATION)
    endif()
endforeach()

# On macOS the game is an app bundle (OpenA2Racer.app), so it starts without a
# Terminal window. build/opena2 links to the program inside it, for the
# command line. `cmake --install build --prefix <dir>` puts a copy with its
# SDL3 and FreeType libraries inside into <dir>, to run on other Macs, and
# the installer disk image OpenA2Racer.dmg beside it. The icon is
# assets/OpenA2Racer.icns, made with tools/make_icons.py.
if(APPLE)
    enable_language(OBJCXX)
    target_sources(opena2 PRIVATE src/game/platform_mac.mm assets/OpenA2Racer.icns)
    set_source_files_properties(assets/OpenA2Racer.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_link_libraries(opena2 PRIVATE "-framework Foundation")
    set_target_properties(opena2 PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME OpenA2Racer
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME OpenA2Racer
        MACOSX_BUNDLE_GUI_IDENTIFIER org.opena2.OpenA2Racer
        MACOSX_BUNDLE_ICON_FILE OpenA2Racer.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    add_custom_command(TARGET opena2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "OpenA2Racer.app/Contents/MacOS/OpenA2Racer" "${CMAKE_CURRENT_BINARY_DIR}/opena2"
        VERBATIM
    )
    install(TARGETS opena2 BUNDLE DESTINATION .)
    install(CODE "execute_process(COMMAND \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle-mac-libraries.sh\"
                                  \"\${CMAKE_INSTALL_PREFIX}/OpenA2Racer.app\" COMMAND_ERROR_IS_FATAL ANY)")
    install(CODE "execute_process(COMMAND \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/make-mac-dmg.sh\"
                                  \"\${CMAKE_INSTALL_PREFIX}/OpenA2Racer.app\" \"\${CMAKE_INSTALL_PREFIX}/OpenA2Racer.dmg\"
                                  COMMAND_ERROR_IS_FATAL ANY)")
endif()

# Windows: a GUI program (no console window) with the icon, linked
# statically so it needs no DLLs beside it.
if(WIN32)
    set(OPENA2_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/OpenA2Racer.ico")
    configure_file(cmake/opena2.rc.in opena2.rc @ONLY)
    target_sources(opena2 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/opena2.rc")
    set_target_properties(opena2 PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME OpenA2Racer)
    if(MINGW)
        foreach(target opena2 opena2_asset_browser)
            target_link_options(${target} PRIVATE -static)
        endforeach()
    endif()
    install(TARGETS opena2 RUNTIME DESTINATION .)
endif()

if(Freetype_FOUND)
    target_link_libraries(opena2 PRIVATE Freetype::Freetype)
    target_compile_definitions(opena2 PRIVATE OPENA2_HAS_FREETYPE)
else()
    message(WARNING "FreeType was not found; the game menu will be built without text.")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(game_data_directory_test
        tests/game_data_directory_test.cpp
    )
    target_link_libraries(game_data_directory_test PRIVATE opena2_data)
    add_test(NAME game_data_directory COMMAND game_data_directory_test)

    add_executable(menu_test
        tests/menu_test.cpp
    )
    target_link_libraries(menu_test PRIVATE opena2_data)
    add_test(NAME menu COMMAND menu_test)

    add_executable(race_test
        tests/race_test.cpp
    )
    target_link_libraries(race_test PRIVATE opena2_data)
    add_test(NAME race COMMAND race_test)

    add_executable(installation_test
        tests/installation_test.cpp
    )
    target_link_libraries(installation_test PRIVATE opena2_data)
    add_test(NAME installation COMMAND installation_test)
endif()
