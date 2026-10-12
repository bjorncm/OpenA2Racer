#pragma once

#include "gl_functions.hpp"
#include "menu_assets.hpp"

#include "opena2/screen_transform.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opena2::game {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// Win32 COLORREF (0x00BBGGRR), as the original passes colours.
constexpr Rgb colorRef(std::uint32_t value)
{
    return {static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>((value >> 8) & 0xFF),
            static_cast<std::uint8_t>((value >> 16) & 0xFF)};
}

// The original's DirectDraw surfaces.
enum class Surface { front = 0, back = 1, buffer = 2 };

struct DrawOp;
using OpList = std::vector<DrawOp>;
using OpSnapshot = std::shared_ptr<const OpList>;

struct DrawOp {
    enum class Kind { clear, image, sprite, text, grid, copy, wipe };

    Kind kind = Kind::clear;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int index = 0;
    std::string text;
    int font = 0;
    Rgb color;
    OpSnapshot source;
    OpSnapshot target;
    int step = 0;
    bool opaque = false;
};

class FontSet;

// Retained emulation of the original 640x480 8-bit display, rasterized at
// the window's real pixel resolution. Drawing calls mirror the original
// xvga library: blits of PCX screens and A2MRC sprites through one global
// palette, GDI text (Arial Black), rectangle copies between surfaces, the
// 50% help grid and the 4x4 pixel-dissolve wipe.
class Display {
public:
    static constexpr int width = 640;
    static constexpr int height = 480;

    Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    ~Display();

    bool initialize(const MenuAssets& assets, std::string& error);
    void destroy();

    // clear_screen(surface, colour)
    void clear(Surface surface, int paletteIndex = 0);
    // Empties a surface without painting it, so the scene layer shows through.
    void discard(Surface surface);
    // load_pcx_file: a full-screen PCX; `setPalette` also makes its palette
    // the global palette.
    void loadImage(Surface surface, const std::string& name, bool setPalette);
    // set_mem_image with the top-left flag.
    void sprite(Surface surface, int index, int x, int y);
    // win_print_xy: (x, y) is the top-left of the text cell.
    void text(Surface surface, int font, Rgb color, int x, int y, std::string_view text);
    // draw_grid: darkens every other pixel with palette index 0.
    void grid(Surface surface);
    // blt_scr_2_scr: copies a rectangle to the same place on another surface.
    void copy(Surface source, Surface target, int left, int top, int right, int bottom);
    // wipe_pixelfade_full from `source` onto the front surface, one of four
    // steps per call (0..3). Step 3 leaves the front equal to the source.
    void wipeStep(Surface source, int step);

    // Appends sprites after the menu's (the race's GAMESPR images) and
    // returns the index of the first; removeSprites drops `first` and all
    // after it. updateSprite replaces a sprite's pixels.
    int addSprites(std::span<const std::optional<IndexedImage>> images);
    void updateSprite(int index, const IndexedImage& image);
    void removeSprites(int first);
    int spriteCount() const noexcept { return static_cast<int>(sprites_.size()); }

    // The global palette (768 bytes of RGB).
    void setPalette(std::span<const std::uint8_t> rgb);
    const std::vector<std::uint8_t>& palette() const noexcept { return palette_; }

    int spriteWidth(int index) const;
    int spriteHeight(int index) const;
    // GetTextExtentPoint32 of the original fonts, in screen pixels.
    int textWidth(int font, std::string_view text) const;
    int textHeight(int font) const;

    // A Win32 EDIT control drawn over the front surface.
    struct EditBox {
        bool visible = false;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        int font = 2;
        std::string text;
        std::size_t caret = 0;
        bool focused = false;
        bool caretVisible = false;
        bool selected = false;
    };
    EditBox editBox;

    // Movie frames replace the menu while a movie plays.
    void showMovieFrame(std::span<const std::uint8_t> pixels, int width, int height,
                        std::span<const std::uint8_t> paletteRgb);
    void hideMovie();

    // The race's 3D view. It is drawn after the screen is cleared and before
    // the surfaces, so the 8-bit drawing lies on top of it. The layer gets
    // the logical-to-pixel transform and the window size in pixels and may
    // change any GL state except the bound framebuffer.
    using SceneLayer = std::function<void(const ScreenTransform& transform, int pixelWidth, int pixelHeight)>;
    void setSceneLayer(SceneLayer layer);

    // The start window's render resolution: the 640x480 area is drawn at
    // most `width` x `height` pixels into an offscreen buffer, which is then
    // scaled up to the window. 0 x 0 draws at the window's own resolution.
    void setRenderResolution(int width, int height);
    // Shows the frames per second in the top-left corner.
    void setFpsCounter(bool shown) { fpsShown_ = shown; }

    void render(int pixelWidth, int pixelHeight);
    // Saves the last frame as shown in the window (window pixels).
    bool capture(const std::filesystem::path& path) const;

private:
    struct Texture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    struct CachedText {
        Texture texture;
        int left = 0;
        int top = 0;
    };

    OpList& mutableOps(Surface surface);
    OpSnapshot snapshot(Surface surface) const;
    void append(Surface surface, DrawOp op);

    void renderOps(const OpList& ops, const PixelRect& clip);
    void setScissor(const PixelRect& clip) const;
    void setRect(GLint uniform, const PixelRect& rect) const;
    void drawIndexed(const Texture& texture, GLuint palette, const PixelRect& rect, bool transparent);
    void drawSolid(Rgb color, const PixelRect& rect, int pattern, int step);
    void drawText(const DrawOp& op);
    void drawEditBox();
    void drawFpsCounter();
    // Binds an offscreen buffer of this size, (re)creating it when needed.
    bool bindOffscreen(int width, int height);
    void destroyOffscreen();
    Rgb paletteColor(int index) const;
    const CachedText& textTexture(int font, const std::string& text);
    void clearTextCache();

    GLuint indexedProgram_ = 0;
    GLuint textProgram_ = 0;
    GLuint solidProgram_ = 0;
    GLuint vertexArray_ = 0;
    GLuint vertexBuffer_ = 0;
    GLint indexedRect_ = -1;
    GLint indexedTransparent_ = -1;
    GLint textRect_ = -1;
    GLint textColor_ = -1;
    GLint solidRect_ = -1;
    GLint solidColor_ = -1;
    GLint solidPattern_ = -1;
    GLint solidStep_ = -1;
    GLint solidTransform_ = -1;

    std::map<std::string, Texture> screens_;
    std::map<std::string, std::vector<std::uint8_t>> screenPalettes_;
    std::vector<Texture> sprites_;
    std::vector<bool> spriteOpaque_;
    std::vector<std::uint8_t> palette_;
    GLuint paletteTexture_ = 0;
    bool paletteDirty_ = true;

    std::array<std::shared_ptr<OpList>, 3> surfaces_;

    SceneLayer sceneLayer_;

    bool movieVisible_ = false;
    Texture movieFrame_;
    GLuint moviePalette_ = 0;

    std::unique_ptr<FontSet> fonts_;
    std::map<std::pair<int, std::string>, CachedText> textCache_;
    float textCacheScale_ = 0.0F;

    ScreenTransform transform_;
    // The size drawn at (the offscreen buffer when one is used).
    int pixelWidth_ = 1;
    int pixelHeight_ = 1;
    // The window's size in pixels.
    int windowWidth_ = 1;
    int windowHeight_ = 1;

    int renderWidth_ = 0;
    int renderHeight_ = 0;
    GLuint offscreenFramebuffer_ = 0;
    GLuint offscreenColor_ = 0;
    GLuint offscreenDepth_ = 0;
    int offscreenWidth_ = 0;
    int offscreenHeight_ = 0;

    bool fpsShown_ = false;
    std::uint64_t fpsStart_ = 0;
    int fpsFrames_ = 0;
    std::string fpsText_;
};

} // namespace opena2::game
