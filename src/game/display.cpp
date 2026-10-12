#include "display.hpp"

#include <SDL3/SDL.h>

#ifdef OPENA2_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#endif

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace opena2::game {

// GDI cell heights of the five fonts the menu creates (win_open_menu
// 0x4014c8): CreateFont(h, ..., "Arial Black").
constexpr std::array<int, 5> fontCellHeights{13, 20, 23, 18, 18};

// Arial Black as GDI uses it: a positive CreateFont height is the cell
// height (winAscent + winDescent), text is placed by the cell's top-left
// and glyphs advance by whole pixels.
class FontSet {
public:
    struct Bitmap {
        std::vector<std::uint8_t> alpha;
        int width = 0;
        int height = 0;
        int left = 0;
        int top = 0;
    };

    FontSet() = default;
    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;

    ~FontSet()
    {
#ifdef OPENA2_HAS_FREETYPE
        if (face_ != nullptr) FT_Done_Face(face_);
        if (library_ != nullptr) FT_Done_FreeType(library_);
#endif
    }

    bool load()
    {
#ifdef OPENA2_HAS_FREETYPE
        if (FT_Init_FreeType(&library_) != 0) {
            return false;
        }
        static constexpr const char* candidates[] = {
            "/System/Library/Fonts/Supplemental/Arial Black.ttf",
            "/Library/Fonts/Arial Black.ttf",
            "C:/Windows/Fonts/ariblk.ttf",
            "/usr/share/fonts/truetype/msttcorefonts/Arial_Black.ttf",
            "/usr/share/fonts/truetype/msttcorefonts/ariblk.ttf",
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
            "C:/Windows/Fonts/arialbd.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        };
        for (const auto* path : candidates) {
            if (FT_New_Face(library_, path, 0, &face_) == 0) {
                measure();
                return true;
            }
        }
        face_ = nullptr;
#endif
        return false;
    }

    int width(int font, std::string_view text) const
    {
        int total = 0;
        for (const auto character : text) {
            total += metrics_[static_cast<std::size_t>(font)].advance[static_cast<unsigned char>(character)];
        }
        return total;
    }

    Bitmap rasterize(int font, std::string_view text, float scale)
    {
        Bitmap bitmap;
#ifdef OPENA2_HAS_FREETYPE
        if (face_ == nullptr || text.empty()) {
            return bitmap;
        }
        const auto& metrics = metrics_[static_cast<std::size_t>(font)];
        FT_Set_Char_Size(face_, 0, static_cast<FT_F26Dot6>(std::lround(metrics.em * scale * 64.0F)), 72, 72);
        constexpr int padding = 4;
        bitmap.left = -padding;
        bitmap.top = 0;
        bitmap.width = static_cast<int>(std::ceil(static_cast<float>(width(font, text)) * scale)) + padding * 3;
        bitmap.height = static_cast<int>(std::ceil(static_cast<float>(fontCellHeights[static_cast<std::size_t>(font)]) * scale)) + padding;
        bitmap.alpha.assign(static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height), 0);
        const int baseline = static_cast<int>(std::lround(metrics.ascent * scale));
        int pen = 0;
        for (const auto character : text) {
            const auto code = static_cast<unsigned char>(character);
            if (FT_Load_Char(face_, code, FT_LOAD_RENDER) == 0) {
                const auto* glyph = face_->glyph;
                const int originX = static_cast<int>(std::lround(static_cast<float>(pen) * scale)) + padding;
                for (unsigned row = 0; row < glyph->bitmap.rows; ++row) {
                    const int y = baseline - glyph->bitmap_top + static_cast<int>(row);
                    if (y < 0 || y >= bitmap.height) continue;
                    for (unsigned column = 0; column < glyph->bitmap.width; ++column) {
                        const int x = originX + glyph->bitmap_left + static_cast<int>(column);
                        if (x < 0 || x >= bitmap.width) continue;
                        auto& target = bitmap.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width) + static_cast<std::size_t>(x)];
                        target = std::max(target, glyph->bitmap.buffer[row * static_cast<unsigned>(glyph->bitmap.pitch) + column]);
                    }
                }
            }
            pen += metrics.advance[code];
        }
#else
        (void)font;
        (void)text;
        (void)scale;
#endif
        return bitmap;
    }

private:
    struct Metrics {
        float em = 0.0F;
        float ascent = 0.0F;
        std::array<int, 256> advance{};
    };

    void measure()
    {
#ifdef OPENA2_HAS_FREETYPE
        float ascentUnits = static_cast<float>(face_->ascender);
        float descentUnits = static_cast<float>(-face_->descender);
        if (const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face_, FT_SFNT_OS2))) {
            ascentUnits = static_cast<float>(os2->usWinAscent);
            descentUnits = static_cast<float>(os2->usWinDescent);
        }
        const auto unitsPerEm = static_cast<float>(face_->units_per_EM);
        for (std::size_t font = 0; font < fontCellHeights.size(); ++font) {
            auto& metrics = metrics_[font];
            const auto cell = static_cast<float>(fontCellHeights[font]);
            metrics.em = cell * unitsPerEm / (ascentUnits + descentUnits);
            metrics.ascent = metrics.em * ascentUnits / unitsPerEm;
            FT_Set_Char_Size(face_, 0, static_cast<FT_F26Dot6>(std::lround(metrics.em * 64.0F)), 72, 72);
            for (int code = 32; code < 256; ++code) {
                if (FT_Load_Char(face_, static_cast<FT_ULong>(code), FT_LOAD_DEFAULT) == 0) {
                    metrics.advance[static_cast<std::size_t>(code)] =
                        static_cast<int>((face_->glyph->advance.x + 32) >> 6);
                }
            }
        }
#endif
    }

#ifdef OPENA2_HAS_FREETYPE
    FT_Library library_ = nullptr;
    FT_Face face_ = nullptr;
#endif
    std::array<Metrics, fontCellHeights.size()> metrics_{};
};

namespace {

constexpr std::size_t textCacheLimit = 1024;

void deleteTexture(GLuint& texture)
{
    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
}

GLuint uploadTexture(GLenum internalFormat, GLenum format, int width, int height,
                     const void* pixels)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format,
                 GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

PixelRect intersect(const PixelRect& a, const PixelRect& b)
{
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

bool emptyRect(const PixelRect& rect)
{
    return rect.x1 <= rect.x0 || rect.y1 <= rect.y0;
}

bool inside(const DrawOp& inner, const DrawOp& outer)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

constexpr const char* vertexSource = R"glsl(#version 150 core
in vec2 position;
uniform vec4 rect;
out vec2 uv;
void main()
{
    gl_Position = vec4(mix(rect.xy, rect.zw, position), 0.0, 1.0);
    uv = position;
}
)glsl";

constexpr const char* indexedSource = R"glsl(#version 150 core
uniform sampler2D indices;
uniform sampler2D palette;
uniform int transparent;
in vec2 uv;
out vec4 color;
void main()
{
    float index = texture(indices, uv).r * 255.0;
    // Sprites use palette index 255 as their transparent colour.
    if (transparent == 1 && index > 254.5) {
        discard;
    }
    color = vec4(texture(palette, vec2((index + 0.5) / 256.0, 0.5)).rgb, 1.0);
}
)glsl";

constexpr const char* textSource = R"glsl(#version 150 core
uniform sampler2D glyphs;
uniform vec3 textColor;
in vec2 uv;
out vec4 color;
void main()
{
    color = vec4(textColor, texture(glyphs, uv).r);
}
)glsl";

// Solid fills with the original's pixel patterns, evaluated per 640x480
// screen pixel: 1 = draw_grid's checkerboard, 2 = the cells the pixel-fade
// wipe has copied after `step`.
constexpr const char* solidSource = R"glsl(#version 150 core
uniform vec3 fillColor;
uniform int pattern;
uniform int step;
uniform vec4 transform;   // scale, offset x, offset y, window pixel height
out vec4 color;
void main()
{
    if (pattern != 0) {
        float top = transform.w - gl_FragCoord.y;
        int x = int(floor((gl_FragCoord.x - transform.y) / transform.x));
        int y = int(floor((top - transform.z) / transform.x));
        if (pattern == 1) {
            if (((x + y) & 1) == 0) discard;
        } else {
            // wipe_draw_pixel_grid raster (0x436330): step of each 4x4 cell.
            const int cellStep[16] = int[16](0, 3, 0, 3,
                                             2, 1, 2, 1,
                                             1, 2, 1, 2,
                                             3, 0, 3, 0);
            if (cellStep[(y & 3) * 4 + (x & 3)] > step) discard;
        }
    }
    color = vec4(fillColor, 1.0);
}
)glsl";

} // namespace

Display::Display()
    : fonts_(std::make_unique<FontSet>())
{
    for (auto& surface : surfaces_) {
        surface = std::make_shared<OpList>();
    }
}

Display::~Display()
{
    destroy();
}

bool Display::initialize(const MenuAssets& assets, std::string& error)
{
    indexedProgram_ = linkProgram(vertexSource, indexedSource, {"position"}, error);
    textProgram_ = indexedProgram_ == 0 ? 0 : linkProgram(vertexSource, textSource, {"position"}, error);
    solidProgram_ = textProgram_ == 0 ? 0 : linkProgram(vertexSource, solidSource, {"position"}, error);
    if (solidProgram_ == 0) {
        return false;
    }
    indexedRect_ = gl.getUniformLocation(indexedProgram_, "rect");
    indexedTransparent_ = gl.getUniformLocation(indexedProgram_, "transparent");
    textRect_ = gl.getUniformLocation(textProgram_, "rect");
    textColor_ = gl.getUniformLocation(textProgram_, "textColor");
    solidRect_ = gl.getUniformLocation(solidProgram_, "rect");
    solidColor_ = gl.getUniformLocation(solidProgram_, "fillColor");
    solidPattern_ = gl.getUniformLocation(solidProgram_, "pattern");
    solidStep_ = gl.getUniformLocation(solidProgram_, "step");
    solidTransform_ = gl.getUniformLocation(solidProgram_, "transform");
    gl.useProgram(indexedProgram_);
    gl.uniform1i(gl.getUniformLocation(indexedProgram_, "indices"), 0);
    gl.uniform1i(gl.getUniformLocation(indexedProgram_, "palette"), 1);
    gl.useProgram(textProgram_);
    gl.uniform1i(gl.getUniformLocation(textProgram_, "glyphs"), 0);
    gl.useProgram(0);

    constexpr GLfloat vertices[] = {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
    gl.genVertexArrays(1, &vertexArray_);
    gl.bindVertexArray(vertexArray_);
    gl.genBuffers(1, &vertexBuffer_);
    gl.bindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    gl.bufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
    gl.bindVertexArray(0);

    for (const auto& [name, image] : assets.screens) {
        screens_.emplace(name, Texture{uploadTexture(GL_R8, GL_RED, image.width, image.height,
                                                     image.pixels.data()),
                                       image.width, image.height});
        auto palette = image.paletteRgb;
        palette.resize(768);
        screenPalettes_.emplace(name, std::move(palette));
    }
    addSprites(assets.sprites);
    palette_.assign(768, 0);
    paletteTexture_ = uploadTexture(GL_RGB8, GL_RGB, 256, 1, palette_.data());
    if (glGetError() != GL_NO_ERROR) {
        error = "Could not upload the menu images.";
        return false;
    }
    if (!fonts_->load()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "No suitable font was found; menu text will not be shown.");
    }
    return true;
}

void Display::destroy()
{
    destroyOffscreen();
    clearTextCache();
    for (auto& [name, texture] : screens_) deleteTexture(texture.id);
    for (auto& texture : sprites_) deleteTexture(texture.id);
    screens_.clear();
    sprites_.clear();
    deleteTexture(paletteTexture_);
    deleteTexture(movieFrame_.id);
    deleteTexture(moviePalette_);
    if (vertexBuffer_ != 0) gl.deleteBuffers(1, &vertexBuffer_);
    if (vertexArray_ != 0) gl.deleteVertexArrays(1, &vertexArray_);
    for (auto* program : {&indexedProgram_, &textProgram_, &solidProgram_}) {
        if (*program != 0) gl.deleteProgram(*program);
        *program = 0;
    }
    vertexBuffer_ = vertexArray_ = 0;
}

// --- Surfaces ----------------------------------------------------------------

OpList& Display::mutableOps(Surface surface)
{
    auto& ops = surfaces_[static_cast<std::size_t>(surface)];
    if (ops.use_count() > 1) {
        ops = std::make_shared<OpList>(*ops);
    }
    return *ops;
}

OpSnapshot Display::snapshot(Surface surface) const
{
    return surfaces_[static_cast<std::size_t>(surface)];
}

// Appends a drawing operation. An opaque operation hides everything
// completely underneath it, so those operations are dropped.
void Display::append(Surface surface, DrawOp op)
{
    auto& ops = mutableOps(surface);
    if (op.opaque) {
        std::erase_if(ops, [&op](const DrawOp& earlier) { return inside(earlier, op); });
    }
    ops.push_back(std::move(op));
}

void Display::clear(Surface surface, int paletteIndex)
{
    DrawOp op;
    op.kind = DrawOp::Kind::clear;
    op.width = width;
    op.height = height;
    op.index = paletteIndex;
    op.opaque = true;
    append(surface, std::move(op));
}

void Display::discard(Surface surface)
{
    surfaces_[static_cast<std::size_t>(surface)] = std::make_shared<OpList>();
}

void Display::loadImage(Surface surface, const std::string& name, bool setPalette)
{
    const auto key = upperCase(name);
    if (setPalette) {
        if (const auto found = screenPalettes_.find(key); found != screenPalettes_.end()) {
            palette_ = found->second;
            paletteDirty_ = true;
        }
    }
    DrawOp op;
    op.kind = DrawOp::Kind::image;
    op.text = key;
    op.width = width;
    op.height = height;
    op.opaque = true;
    append(surface, std::move(op));
}

void Display::sprite(Surface surface, int index, int x, int y)
{
    if (index < 0 || static_cast<std::size_t>(index) >= sprites_.size()) {
        return;
    }
    DrawOp op;
    op.kind = DrawOp::Kind::sprite;
    op.index = index;
    op.x = x;
    op.y = y;
    op.width = sprites_[static_cast<std::size_t>(index)].width;
    op.height = sprites_[static_cast<std::size_t>(index)].height;
    op.opaque = spriteOpaque_[static_cast<std::size_t>(index)];
    append(surface, std::move(op));
}

int Display::addSprites(std::span<const std::optional<IndexedImage>> images)
{
    const int base = static_cast<int>(sprites_.size());
    sprites_.reserve(sprites_.size() + images.size());
    for (const auto& sprite : images) {
        sprites_.push_back({});
        spriteOpaque_.push_back(false);
        if (sprite) {
            updateSprite(static_cast<int>(sprites_.size()) - 1, *sprite);
        }
    }
    return base;
}

void Display::updateSprite(int index, const IndexedImage& image)
{
    if (index < 0 || static_cast<std::size_t>(index) >= sprites_.size()) {
        return;
    }
    auto& texture = sprites_[static_cast<std::size_t>(index)];
    if (texture.id != 0 && texture.width == image.width && texture.height == image.height) {
        glBindTexture(GL_TEXTURE_2D, texture.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width, image.height, GL_RED, GL_UNSIGNED_BYTE,
                        image.pixels.data());
    } else {
        deleteTexture(texture.id);
        texture = {uploadTexture(GL_R8, GL_RED, image.width, image.height, image.pixels.data()),
                   image.width, image.height};
    }
    spriteOpaque_[static_cast<std::size_t>(index)] =
        std::find(image.pixels.begin(), image.pixels.end(), 255) == image.pixels.end();
}

void Display::removeSprites(int first)
{
    if (first < 0) {
        return;
    }
    while (sprites_.size() > static_cast<std::size_t>(first)) {
        deleteTexture(sprites_.back().id);
        sprites_.pop_back();
        spriteOpaque_.pop_back();
    }
}

void Display::setPalette(std::span<const std::uint8_t> rgb)
{
    palette_.assign(rgb.begin(), rgb.end());
    palette_.resize(768);
    paletteDirty_ = true;
}

void Display::text(Surface surface, int font, Rgb color, int x, int y, std::string_view value)
{
    if (value.empty()) {
        return;
    }
    DrawOp op;
    op.kind = DrawOp::Kind::text;
    op.font = font;
    op.color = color;
    op.x = x;
    op.y = y;
    op.width = textWidth(font, value);
    op.height = textHeight(font);
    op.text = std::string(value);
    append(surface, std::move(op));
}

void Display::grid(Surface surface)
{
    DrawOp op;
    op.kind = DrawOp::Kind::grid;
    op.width = width;
    op.height = height;
    append(surface, std::move(op));
}

void Display::copy(Surface source, Surface target, int left, int top, int right, int bottom)
{
    DrawOp op;
    op.kind = DrawOp::Kind::copy;
    op.x = left;
    op.y = top;
    op.width = right - left;
    op.height = bottom - top;
    op.source = snapshot(source);
    op.opaque = true;
    if (op.width > 0 && op.height > 0) {
        append(target, std::move(op));
    }
}

void Display::wipeStep(Surface source, int step)
{
    if (step >= 3) {
        copy(source, Surface::front, 0, 0, width, height);
        return;
    }
    // Keep the screen the wipe started from underneath every step.
    OpSnapshot previous = snapshot(Surface::front);
    auto& ops = surfaces_[static_cast<std::size_t>(Surface::front)];
    if (!ops->empty() && ops->back().kind == DrawOp::Kind::wipe) {
        previous = ops->back().source;
    }
    DrawOp op;
    op.kind = DrawOp::Kind::wipe;
    op.width = width;
    op.height = height;
    op.source = previous;
    op.target = snapshot(source);
    op.step = step;
    op.opaque = true;
    append(Surface::front, std::move(op));
}

int Display::spriteWidth(int index) const
{
    return index >= 0 && static_cast<std::size_t>(index) < sprites_.size()
        ? sprites_[static_cast<std::size_t>(index)].width : 0;
}

int Display::spriteHeight(int index) const
{
    return index >= 0 && static_cast<std::size_t>(index) < sprites_.size()
        ? sprites_[static_cast<std::size_t>(index)].height : 0;
}

int Display::textWidth(int font, std::string_view value) const
{
    return fonts_->width(font, value);
}

int Display::textHeight(int font) const
{
    return fontCellHeights.at(static_cast<std::size_t>(font));
}

void Display::showMovieFrame(std::span<const std::uint8_t> pixels, int frameWidth,
                             int frameHeight, std::span<const std::uint8_t> paletteRgb)
{
    if (frameWidth <= 0 || frameHeight <= 0 || paletteRgb.size() < 768 ||
        pixels.size() < static_cast<std::size_t>(frameWidth) * static_cast<std::size_t>(frameHeight)) {
        return;
    }
    if (movieFrame_.id == 0 || movieFrame_.width != frameWidth || movieFrame_.height != frameHeight) {
        deleteTexture(movieFrame_.id);
        movieFrame_ = {uploadTexture(GL_R8, GL_RED, frameWidth, frameHeight, nullptr), frameWidth,
                       frameHeight};
    }
    if (moviePalette_ == 0) {
        moviePalette_ = uploadTexture(GL_RGB8, GL_RGB, 256, 1, nullptr);
    }
    // The movie player sets 6-bit VGA palette values shifted left by two.
    std::array<std::uint8_t, 768> palette{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        palette[i] = static_cast<std::uint8_t>(((paletteRgb[i] * 63 + 127) / 255) << 2);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, movieFrame_.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frameWidth, frameHeight, GL_RED, GL_UNSIGNED_BYTE,
                    pixels.data());
    glBindTexture(GL_TEXTURE_2D, moviePalette_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, palette.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    movieVisible_ = true;
}

void Display::hideMovie()
{
    movieVisible_ = false;
}

// --- Rendering ---------------------------------------------------------------

Rgb Display::paletteColor(int index) const
{
    const auto offset = static_cast<std::size_t>(std::clamp(index, 0, 255)) * 3;
    return {palette_[offset], palette_[offset + 1], palette_[offset + 2]};
}

void Display::setScissor(const PixelRect& clip) const
{
    glScissor(clip.x0, pixelHeight_ - clip.y1, std::max(0, clip.x1 - clip.x0),
              std::max(0, clip.y1 - clip.y0));
}

void Display::setRect(GLint uniform, const PixelRect& rect) const
{
    const auto toNdcX = [this](int x) {
        return static_cast<float>(x) / static_cast<float>(pixelWidth_) * 2.0F - 1.0F;
    };
    const auto toNdcY = [this](int y) {
        return 1.0F - static_cast<float>(y) / static_cast<float>(pixelHeight_) * 2.0F;
    };
    gl.uniform4f(uniform, toNdcX(rect.x0), toNdcY(rect.y0), toNdcX(rect.x1), toNdcY(rect.y1));
}

void Display::drawIndexed(const Texture& texture, GLuint palette, const PixelRect& rect,
                          bool transparent)
{
    if (texture.id == 0) {
        return;
    }
    gl.useProgram(indexedProgram_);
    gl.uniform1i(indexedTransparent_, transparent ? 1 : 0);
    setRect(indexedRect_, rect);
    gl.activeTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, palette);
    gl.activeTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    gl.bindVertexArray(vertexArray_);
    gl.drawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Display::drawSolid(Rgb color, const PixelRect& rect, int pattern, int step)
{
    gl.useProgram(solidProgram_);
    gl.uniform3f(solidColor_, color.r / 255.0F, color.g / 255.0F, color.b / 255.0F);
    gl.uniform1i(solidPattern_, pattern);
    gl.uniform1i(solidStep_, step);
    gl.uniform4f(solidTransform_, transform_.scale, transform_.offsetX, transform_.offsetY,
                 static_cast<float>(pixelHeight_));
    setRect(solidRect_, rect);
    gl.bindVertexArray(vertexArray_);
    gl.drawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Display::drawText(const DrawOp& op)
{
    const auto& cached = textTexture(op.font, op.text);
    if (cached.texture.id == 0) {
        return;
    }
    const int left = transform_.toTargetX(static_cast<float>(op.x)) + cached.left;
    const int top = transform_.toTargetY(static_cast<float>(op.y)) + cached.top;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.useProgram(textProgram_);
    gl.uniform3f(textColor_, op.color.r / 255.0F, op.color.g / 255.0F, op.color.b / 255.0F);
    setRect(textRect_, {left, top, left + cached.texture.width, top + cached.texture.height});
    gl.activeTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cached.texture.id);
    gl.bindVertexArray(vertexArray_);
    gl.drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
}

void Display::renderOps(const OpList& ops, const PixelRect& clip)
{
    const auto full = transform_.toTarget(0.0F, 0.0F, width, height);
    for (const auto& op : ops) {
        switch (op.kind) {
        case DrawOp::Kind::clear:
            drawSolid(paletteColor(op.index), full, 0, 0);
            break;
        case DrawOp::Kind::image:
            if (const auto found = screens_.find(op.text); found != screens_.end()) {
                drawIndexed(found->second, paletteTexture_, full, false);
            }
            break;
        case DrawOp::Kind::sprite:
            if (static_cast<std::size_t>(op.index) >= sprites_.size()) {
                break;
            }
            drawIndexed(sprites_[static_cast<std::size_t>(op.index)], paletteTexture_,
                        transform_.toTarget(static_cast<float>(op.x), static_cast<float>(op.y),
                                            static_cast<float>(op.width), static_cast<float>(op.height)),
                        true);
            break;
        case DrawOp::Kind::text:
            drawText(op);
            break;
        case DrawOp::Kind::grid:
            drawSolid(paletteColor(0), full, 1, 0);
            break;
        case DrawOp::Kind::copy: {
            const auto area = intersect(clip, transform_.toTarget(static_cast<float>(op.x), static_cast<float>(op.y),
                                                                   static_cast<float>(op.width), static_cast<float>(op.height)));
            if (!emptyRect(area) && op.source) {
                setScissor(area);
                renderOps(*op.source, area);
                setScissor(clip);
            }
            break;
        }
        case DrawOp::Kind::wipe:
            if (op.source) {
                renderOps(*op.source, clip);
            }
            if (op.target) {
                glEnable(GL_STENCIL_TEST);
                glClearStencil(0);
                glClear(GL_STENCIL_BUFFER_BIT);
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                drawSolid({}, full, 2, op.step);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glStencilFunc(GL_EQUAL, 1, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                renderOps(*op.target, clip);
                glDisable(GL_STENCIL_TEST);
            }
            break;
        }
    }
}

void Display::drawEditBox()
{
    if (!editBox.visible) {
        return;
    }
    const auto& box = editBox;
    setScissor(transform_.toTarget(static_cast<float>(box.x), static_cast<float>(box.y),
                                   static_cast<float>(box.width), static_cast<float>(box.height)));
    // set_windows_edit_box_colors: white on red while focused, else on black.
    drawSolid(box.focused ? Rgb{255, 0, 0} : Rgb{0, 0, 0},
              transform_.toTarget(static_cast<float>(box.x), static_cast<float>(box.y),
                                  static_cast<float>(box.width), static_cast<float>(box.height)),
              0, 0);
    constexpr int margin = 2;
    DrawOp op;
    op.kind = DrawOp::Kind::text;
    op.font = box.font;
    op.color = {255, 255, 255};
    op.x = box.x + margin;
    op.y = box.y;
    op.text = box.text;
    if (box.selected && box.focused && !op.text.empty()) {
        // Selected text: inverted colours.
        drawSolid({255, 255, 255},
                  transform_.toTarget(static_cast<float>(op.x), static_cast<float>(box.y),
                                      static_cast<float>(textWidth(box.font, op.text)),
                                      static_cast<float>(textHeight(box.font))),
                  0, 0);
        op.color = {255, 0, 0};
    }
    if (!op.text.empty()) {
        drawText(op);
    }
    if (box.focused && box.caretVisible) {
        const int caretX = box.x + margin + textWidth(box.font, std::string_view(box.text).substr(0, box.caret));
        drawSolid({255, 255, 255},
                  transform_.toTarget(static_cast<float>(caretX), static_cast<float>(box.y), 1.0F,
                                      static_cast<float>(textHeight(box.font))),
                  0, 0);
    }
}

void Display::setSceneLayer(SceneLayer layer)
{
    sceneLayer_ = std::move(layer);
}

void Display::setRenderResolution(int width, int height)
{
    renderWidth_ = std::max(width, 0);
    renderHeight_ = std::max(height, 0);
}

bool Display::bindOffscreen(int width, int height)
{
    if (offscreenFramebuffer_ == 0 || width != offscreenWidth_ || height != offscreenHeight_) {
        destroyOffscreen();
        glGenTextures(1, &offscreenColor_);
        glBindTexture(GL_TEXTURE_2D, offscreenColor_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        // The race draws with depth and stencil, like the window's buffer.
        gl.genRenderbuffers(1, &offscreenDepth_);
        gl.bindRenderbuffer(GL_RENDERBUFFER, offscreenDepth_);
        gl.renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        gl.bindRenderbuffer(GL_RENDERBUFFER, 0);
        gl.genFramebuffers(1, &offscreenFramebuffer_);
        gl.bindFramebuffer(GL_FRAMEBUFFER, offscreenFramebuffer_);
        gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, offscreenColor_, 0);
        gl.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, offscreenDepth_);
        offscreenWidth_ = width;
        offscreenHeight_ = height;
        if (gl.checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create the %d x %d render buffer.", width, height);
            gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
            destroyOffscreen();
            // Not tried again: the game draws at the window's resolution.
            renderWidth_ = renderHeight_ = 0;
            return false;
        }
        return true;
    }
    gl.bindFramebuffer(GL_FRAMEBUFFER, offscreenFramebuffer_);
    return true;
}

void Display::destroyOffscreen()
{
    if (offscreenFramebuffer_ != 0) gl.deleteFramebuffers(1, &offscreenFramebuffer_);
    if (offscreenDepth_ != 0) gl.deleteRenderbuffers(1, &offscreenDepth_);
    deleteTexture(offscreenColor_);
    offscreenFramebuffer_ = offscreenDepth_ = 0;
    offscreenWidth_ = offscreenHeight_ = 0;
}

void Display::drawFpsCounter()
{
    const std::uint64_t now = SDL_GetTicks();
    ++fpsFrames_;
    if (fpsStart_ == 0) {
        fpsStart_ = now;
    } else if (now - fpsStart_ >= 500) {
        const double fps = fpsFrames_ * 1000.0 / static_cast<double>(now - fpsStart_);
        fpsText_ = std::to_string(static_cast<int>(std::lround(fps))) + " FPS";
        fpsStart_ = now;
        fpsFrames_ = 0;
    }
    if (fpsText_.empty()) {
        return;
    }
    constexpr int font = 0;
    const auto box = transform_.toTarget(2.0F, 2.0F, static_cast<float>(textWidth(font, fpsText_) + 6),
                                         static_cast<float>(textHeight(font) + 2));
    drawSolid({0, 0, 0}, box, 0, 0);
    DrawOp op;
    op.kind = DrawOp::Kind::text;
    op.x = 5;
    op.y = 3;
    op.font = font;
    op.color = {255, 255, 0};
    op.text = fpsText_;
    drawText(op);
}

void Display::render(int pixelWidth, int pixelHeight)
{
    windowWidth_ = std::max(pixelWidth, 1);
    windowHeight_ = std::max(pixelHeight, 1);
    pixelWidth_ = windowWidth_;
    pixelHeight_ = windowHeight_;
    bool offscreen = false;
    if (renderWidth_ > 0 && renderHeight_ > 0) {
        // Scale the window so the 640x480 area is no larger than asked.
        const auto fit = ScreenTransform::fit(windowWidth_, windowHeight_);
        const double factor =
            std::min({1.0, renderWidth_ / static_cast<double>(ScreenTransform::logicalWidth * fit.scale),
                      renderHeight_ / static_cast<double>(ScreenTransform::logicalHeight * fit.scale)});
        if (factor < 1.0) {
            const int width = std::max(1, static_cast<int>(std::lround(windowWidth_ * factor)));
            const int height = std::max(1, static_cast<int>(std::lround(windowHeight_ * factor)));
            offscreen = bindOffscreen(width, height);
            if (offscreen) {
                pixelWidth_ = width;
                pixelHeight_ = height;
            }
        }
    }
    transform_ = ScreenTransform::fit(pixelWidth_, pixelHeight_);
    if (transform_.scale != textCacheScale_) {
        clearTextCache();
        textCacheScale_ = transform_.scale;
    }
    if (paletteDirty_) {
        glBindTexture(GL_TEXTURE_2D, paletteTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, palette_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        paletteDirty_ = false;
    }
    glViewport(0, 0, pixelWidth_, pixelHeight_);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    if (sceneLayer_ && !movieVisible_) {
        sceneLayer_(transform_, pixelWidth_, pixelHeight_);
        glViewport(0, 0, pixelWidth_, pixelHeight_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
    }

    const auto full = transform_.toTarget(0.0F, 0.0F, width, height);
    glEnable(GL_SCISSOR_TEST);
    setScissor(full);
    if (movieVisible_) {
        drawIndexed(movieFrame_, moviePalette_, full, false);
    } else {
        renderOps(*surfaces_[0], full);
        drawEditBox();
    }
    if (fpsShown_) {
        drawFpsCounter();
    }
    glDisable(GL_SCISSOR_TEST);
    if (offscreen) {
        // Whole multiples keep the pixels sharp; other sizes are smoothed.
        const bool whole = windowWidth_ % pixelWidth_ == 0 && windowHeight_ % pixelHeight_ == 0;
        gl.bindFramebuffer(GL_READ_FRAMEBUFFER, offscreenFramebuffer_);
        gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl.blitFramebuffer(0, 0, pixelWidth_, pixelHeight_, 0, 0, windowWidth_, windowHeight_, GL_COLOR_BUFFER_BIT,
                           whole ? GL_NEAREST : GL_LINEAR);
        gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, windowWidth_, windowHeight_);
    }
    gl.bindVertexArray(0);
    gl.useProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Display::capture(const std::filesystem::path& path) const
{
    const auto w = static_cast<std::size_t>(windowWidth_);
    const auto h = static_cast<std::size_t>(windowHeight_);
    std::vector<std::uint8_t> pixels(w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, windowWidth_, windowHeight_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (std::size_t row = 0; row < h / 2; ++row) {
        std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(row * w * 4),
                         pixels.begin() + static_cast<std::ptrdiff_t>((row + 1) * w * 4),
                         pixels.begin() + static_cast<std::ptrdiff_t>((h - 1 - row) * w * 4));
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(windowWidth_, windowHeight_, SDL_PIXELFORMAT_RGBA32,
                                                 pixels.data(), windowWidth_ * 4);
    if (surface == nullptr) {
        return false;
    }
    const bool saved = SDL_SaveBMP(surface, reinterpret_cast<const char*>(path.u8string().c_str()));
    SDL_DestroySurface(surface);
    return saved;
}

const Display::CachedText& Display::textTexture(int font, const std::string& value)
{
    const auto key = std::make_pair(font, value);
    if (const auto found = textCache_.find(key); found != textCache_.end()) {
        return found->second;
    }
    if (textCache_.size() >= textCacheLimit) {
        clearTextCache();
    }
    const auto bitmap = fonts_->rasterize(font, value, transform_.scale);
    CachedText cached;
    cached.left = bitmap.left;
    cached.top = bitmap.top;
    if (bitmap.width > 0 && bitmap.height > 0) {
        cached.texture = {uploadTexture(GL_R8, GL_RED, bitmap.width, bitmap.height, bitmap.alpha.data()),
                          bitmap.width, bitmap.height};
    }
    return textCache_.emplace(key, cached).first->second;
}

void Display::clearTextCache()
{
    for (auto& [key, cached] : textCache_) {
        deleteTexture(cached.texture.id);
    }
    textCache_.clear();
}

} // namespace opena2::game
