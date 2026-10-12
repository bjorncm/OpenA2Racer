#include "game/start_window.hpp"

#include "game/gl_functions.hpp"
#include "game/platform.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#ifdef OPENA2_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace opena2::game {
namespace {

namespace fs = std::filesystem;

// The start window's texts (Dutch, like the rest of the game).
constexpr const char* appTitle = "A2 Racer";
constexpr const char* firstRunText =
    "A2 Racer wordt voor het eerst gestart: de spelbestanden moeten eerst eenmalig van de CD-ROM worden "
    "gekopieerd.";
constexpr const char* incompleteFormat = "De spelbestanden in de map %s zijn niet compleet.";
constexpr const char* chooseFormat =
    "%s\n\nKies in het volgende venster de map met de bestanden van de A2 Racer CD-ROM: de CD-speler zelf "
    "of een kopie van de CD op de harde schijf.\n\nDe benodigde bestanden worden daarna gekopieerd naar:\n%s";
constexpr const char* pickTitle = "Kies de map met de bestanden van de A2 Racer CD-ROM";
constexpr const char* notCdFormat =
    "In deze map staan niet de bestanden van de A2 Racer CD-ROM:\n%s\n\nKies de hoofdmap van de CD "
    "(met daarin A2.EXE).";
constexpr const char* installFailedFormat = "De installatie is mislukt.\n\n%s";
constexpr const char* reinstallWhy =
    "De spelbestanden worden opnieuw ge\xC3\xAFnstalleerd. Opgeslagen spellen, opties en scores blijven "
    "bewaard.";
constexpr const char* pictureGroup = "Beeld";
constexpr const char* windowSizeLabel = "Venstergrootte:";
constexpr const char* renderLabel = "Renderresolutie:";
constexpr const char* nativeText = "Native";
constexpr const char* resolutionHint =
    "Native tekent het spel op de resolutie van het scherm; een lagere renderresolutie wordt opgeschaald.";
constexpr const char* fullscreenText = "Volledig scherm (Alt+Enter wisselt tijdens het spel)";
constexpr const char* fpsText = "FPS-teller tonen";
constexpr const char* raceGroup = "Race";
constexpr const char* distanceLabel = "Zichtafstand:";
constexpr const char* distanceOriginalText = "Origineel";
constexpr const char* tickLabel = "Tickrate:";
constexpr const char* tickFormat = "%s per seconde";
constexpr const char* tickDefaultFormat = "%s per seconde (aanbevolen)";
constexpr const char* raceHint =
    "Een grotere zichtafstand tekent de weg, de omgeving en het verkeer verder vooruit. De tickrate is hoe vaak per seconde de "
    "race wordt doorgerekend. Auto's, verkeer en bonussen bewegen elke tick een vaste stap, maar de klok loopt "
    "in echte tijd: een hogere tickrate maakt het spel sneller en de tijdslimiet makkelijker, een lagere maakt "
    "het trager. In het originele spel was de tickrate gekoppeld aan de snelheid van de CPU waardoor deze per PC kon verschillen.";
constexpr const char* filesGroup = "Spelbestanden";
constexpr const char* reinstallText = "Opnieuw installeren";
constexpr const char* openFolderText = "Map openen";
constexpr const char* playText = "Spelen";
constexpr const char* quitText = "Afsluiten";
constexpr const char* installTitle = "A2 Racer installeren";
constexpr const char* copyingText = "De spelbestanden worden van de CD-ROM gekopieerd...";
constexpr const char* cancelText = "Annuleren";
constexpr const char* cancellingText = "Bezig met afbreken...";
constexpr const char* uninstallText = "Verwijderen...";
constexpr const char* uninstallConfirm =
    "OpenA2Racer wordt van deze computer verwijderd: de gekopieerde spelbestanden, de instellingen van dit "
    "venster en het programma zelf.\n\nDoorgaan?";
constexpr const char* uninstallButton = "Verwijderen";
constexpr const char* savesQuestion =
    "Ook de opgeslagen spellen, scores en opties verwijderen?\n\nKies Nee om ze te bewaren voor een latere "
    "installatie.";
constexpr const char* yesText = "Ja";
constexpr const char* noText = "Nee";
constexpr const char* removedText = "OpenA2Racer is verwijderd. Het programma staat in de prullenmand.";
constexpr const char* removedProgramFormat =
    "De spelbestanden en instellingen zijn verwijderd. Verwijder nu zelf het programma:\n%s";
constexpr const char* removeFailedFormat = "Niet alles kon worden verwijderd.\n\n%s";
constexpr const char* okText = "OK";
constexpr const char* retryText = "Opnieuw";

std::string fillIn(const char* pattern, std::string_view first, std::string_view second = {})
{
    std::string out;
    int index = 0;
    for (const char* p = pattern; *p != '\0'; ++p) {
        if (p[0] == '%' && p[1] == 's') {
            out += index++ == 0 ? first : second;
            ++p;
        } else {
            out.push_back(*p);
        }
    }
    return out;
}

std::string pathText(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

fs::path fromUtf8(const char* text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

// ---------------------------------------------------------------- messages

enum class Answer { ok, cancel };

Answer message(SDL_Window* window, SDL_MessageBoxFlags kind, const std::string& text, const char* okLabel,
               bool withCancel)
{
    SDL_MessageBoxButtonData buttons[2] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, okLabel},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, cancelText},
    };
    if (!withCancel) {
        buttons[0].flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    }
    const SDL_MessageBoxData data{kind, window, appTitle, text.c_str(), withCancel ? 2 : 1, buttons, nullptr};
    int pressed = 0;
    if (!SDL_ShowMessageBox(&data, &pressed)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str());
        return withCancel ? Answer::cancel : Answer::ok;
    }
    return pressed == 1 ? Answer::ok : Answer::cancel;
}

// A message box with up to three buttons; the index of the one pressed. Esc
// presses the last.
int ask(SDL_Window* window, SDL_MessageBoxFlags kind, const std::string& text,
        std::initializer_list<const char*> labels)
{
    std::vector<SDL_MessageBoxButtonData> buttons;
    int id = 0;
    for (const char* label : labels) {
        SDL_MessageBoxButtonFlags flags = 0;
        if (id == 0) {
            flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        }
        if (id == static_cast<int>(labels.size()) - 1) {
            flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        }
        buttons.push_back({flags, id++, label});
    }
    const SDL_MessageBoxData data{kind, window, appTitle, text.c_str(), static_cast<int>(buttons.size()),
                                  buttons.data(), nullptr};
    int pressed = static_cast<int>(buttons.size()) - 1;
    if (!SDL_ShowMessageBox(&data, &pressed)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str());
        return static_cast<int>(buttons.size()) - 1;
    }
    return pressed;
}

// ---------------------------------------------------------------- drawing

struct Color {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

constexpr Color rgb(std::uint32_t value)
{
    return {static_cast<float>((value >> 16) & 0xFF) / 255.0F, static_cast<float>((value >> 8) & 0xFF) / 255.0F,
            static_cast<float>(value & 0xFF) / 255.0F};
}

// Colours of a Windows dialog, as the original start window looks.
constexpr Color windowColor = rgb(0xF0F0F0);
constexpr Color textColor = rgb(0x000000);
constexpr Color greyText = rgb(0x6D6D6D);
constexpr Color groupLine = rgb(0xDCDCDC);
constexpr Color buttonFace = rgb(0xE1E1E1);
constexpr Color buttonHover = rgb(0xE5F1FB);
constexpr Color buttonPressed = rgb(0xCCE4F7);
constexpr Color buttonBorder = rgb(0xADADAD);
constexpr Color accent = rgb(0x0078D7);
constexpr Color fieldColor = rgb(0xFFFFFF);
constexpr Color listHover = rgb(0x0078D7);
constexpr Color white = rgb(0xFFFFFF);
constexpr Color progressBack = rgb(0xE6E6E6);
constexpr Color progressFill = rgb(0x06B025);

constexpr float textSize = 13.0F;
constexpr float lineHeight = 18.0F;

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

constexpr const char* solidSource = R"glsl(#version 150 core
uniform vec3 fillColor;
out vec4 color;
void main()
{
    color = vec4(fillColor, 1.0);
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

std::vector<std::uint32_t> codepoints(std::string_view text)
{
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        int extra = byte >= 0xF0 ? 3 : byte >= 0xE0 ? 2 : byte >= 0xC0 ? 1 : 0;
        std::uint32_t code = extra == 3 ? byte & 0x07U : extra == 2 ? byte & 0x0FU : extra == 1 ? byte & 0x1FU : byte;
        ++i;
        for (; extra > 0 && i < text.size(); --extra, ++i) {
            code = (code << 6) | (static_cast<unsigned char>(text[i]) & 0x3FU);
        }
        out.push_back(code);
    }
    return out;
}

// Window coordinates per dialog unit. macOS window coordinates already are
// points; on Windows they are pixels, so the dialogs follow the display's
// scale setting (1.5 at 150%).
float uiScale(SDL_Window* window)
{
    const float density = SDL_GetWindowPixelDensity(window);
    const float scale = SDL_GetWindowDisplayScale(window);
    if (density <= 0.0F || scale <= 0.0F) {
        return 1.0F;
    }
    return std::max(scale / density, 1.0F);
}

// Solid rectangles and FreeType text in dialog units, drawn at the
// window's pixel resolution.
class Painter {
public:
    Painter() = default;
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    ~Painter()
    {
        clearCache();
        if (vertexBuffer_ != 0) {
            gl.deleteBuffers(1, &vertexBuffer_);
        }
        if (vertexArray_ != 0) {
            gl.deleteVertexArrays(1, &vertexArray_);
        }
        if (solidProgram_ != 0) {
            gl.deleteProgram(solidProgram_);
        }
        if (textProgram_ != 0) {
            gl.deleteProgram(textProgram_);
        }
#ifdef OPENA2_HAS_FREETYPE
        if (face_ != nullptr) {
            FT_Done_Face(face_);
        }
        if (library_ != nullptr) {
            FT_Done_FreeType(library_);
        }
#endif
    }

    bool initialize()
    {
        std::string error;
        solidProgram_ = linkProgram(vertexSource, solidSource, {"position"}, error);
        textProgram_ = solidProgram_ == 0 ? 0 : linkProgram(vertexSource, textSource, {"position"}, error);
        if (textProgram_ == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
            return false;
        }
        solidRect_ = gl.getUniformLocation(solidProgram_, "rect");
        solidColor_ = gl.getUniformLocation(solidProgram_, "fillColor");
        textRect_ = gl.getUniformLocation(textProgram_, "rect");
        textColor_ = gl.getUniformLocation(textProgram_, "textColor");
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

#ifdef OPENA2_HAS_FREETYPE
        if (FT_Init_FreeType(&library_) == 0) {
            static constexpr const char* candidates[] = {
                "C:/Windows/Fonts/segoeui.ttf",
                "/System/Library/Fonts/Helvetica.ttc",
                "/System/Library/Fonts/Supplemental/Arial.ttf",
                "/Library/Fonts/Arial.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/TTF/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            };
            for (const char* path : candidates) {
                if (FT_New_Face(library_, path, 0, &face_) == 0) {
                    break;
                }
                face_ = nullptr;
            }
        }
        if (face_ == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No font for the start window was found.");
        }
#endif
        return true;
    }

    void begin(SDL_Window* window)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        SDL_GetWindowSizeInPixels(window, &pixelWidth_, &pixelHeight_);
        const float scale = uiScale(window);
        width_ = static_cast<float>(std::max(width, 1)) / scale;
        height_ = static_cast<float>(std::max(height, 1)) / scale;
        const float density = static_cast<float>(std::max(pixelWidth_, 1)) / width_;
        if (density != density_) {
            clearCache();
            density_ = density;
        }
        glViewport(0, 0, pixelWidth_, pixelHeight_);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl.bindVertexArray(vertexArray_);
        fill(0, 0, width_, height_, windowColor);
    }

    void end()
    {
        gl.bindVertexArray(0);
        gl.useProgram(0);
        glDisable(GL_BLEND);
    }

    void fill(float x, float y, float w, float h, Color color)
    {
        gl.useProgram(solidProgram_);
        setRect(solidRect_, x, y, w, h);
        gl.uniform3f(solidColor_, color.r, color.g, color.b);
        gl.drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // A one-pixel outline.
    void outline(float x, float y, float w, float h, Color color)
    {
        const float pixel = 1.0F / density_;
        fill(x, y, w, pixel, color);
        fill(x, y + h - pixel, w, pixel, color);
        fill(x, y, pixel, h, color);
        fill(x + w - pixel, y, pixel, h, color);
    }

    float textWidth(std::string_view text) { return static_cast<float>(cached(text).width) / density_; }

    // (x, y) is the top-left of the line.
    void text(float x, float y, std::string_view text, Color color)
    {
        const Cached& entry = cached(text);
        if (entry.texture == 0) {
            return;
        }
        gl.useProgram(textProgram_);
        const float px = std::round(x * density_) / density_;
        const float py = std::round(y * density_) / density_;
        setRect(textRect_, px, py, static_cast<float>(entry.width) / density_,
                static_cast<float>(entry.height) / density_);
        gl.uniform3f(textColor_, color.r, color.g, color.b);
        gl.activeTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, entry.texture);
        gl.drawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Breaks `text` into lines no wider than `width`: at its newlines, then
    // between words, and inside a word (a long path) when it alone is wider.
    std::vector<std::string> wrap(std::string_view text, float width)
    {
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto newline = text.find('\n', start);
            const auto paragraph =
                text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
            wrapParagraph(paragraph, width, lines);
            if (newline == std::string_view::npos) {
                break;
            }
            start = newline + 1;
        }
        return lines;
    }

private:
    struct Cached {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
    };

    void wrapParagraph(std::string_view paragraph, float width, std::vector<std::string>& lines)
    {
        std::string line;
        std::size_t i = 0;
        while (i < paragraph.size()) {
            const auto space = paragraph.find(' ', i);
            const std::size_t end = space == std::string_view::npos ? paragraph.size() : space;
            const std::string word(paragraph.substr(i, end - i));
            const std::string candidate = line.empty() ? word : line + ' ' + word;
            if (textWidth(candidate) <= width) {
                line = candidate;
            } else if (!line.empty() && textWidth(word) <= width) {
                lines.push_back(line);
                line = word;
            } else {
                // Break the word by characters (UTF-8 aware).
                std::string piece = line.empty() ? std::string() : line + ' ';
                for (std::size_t c = 0; c < word.size();) {
                    std::size_t length = 1;
                    while (c + length < word.size() && (static_cast<unsigned char>(word[c + length]) & 0xC0) == 0x80) {
                        ++length;
                    }
                    const std::string next = piece + word.substr(c, length);
                    if (!piece.empty() && textWidth(next) > width) {
                        lines.push_back(piece);
                        piece = word.substr(c, length);
                    } else {
                        piece = next;
                    }
                    c += length;
                }
                line = piece;
            }
            i = end + 1;
        }
        lines.push_back(line);
    }

    void setRect(GLint uniform, float x, float y, float w, float h) const
    {
        const float x0 = x / width_ * 2.0F - 1.0F;
        const float x1 = (x + w) / width_ * 2.0F - 1.0F;
        const float y0 = 1.0F - y / height_ * 2.0F;
        const float y1 = 1.0F - (y + h) / height_ * 2.0F;
        gl.uniform4f(uniform, x0, y0, x1, y1);
    }

    const Cached& cached(std::string_view text)
    {
        const std::string key(text);
        if (const auto found = cache_.find(key); found != cache_.end()) {
            return found->second;
        }
        if (cache_.size() > 512) {
            clearCache();
        }
        Cached entry;
#ifdef OPENA2_HAS_FREETYPE
        if (face_ != nullptr) {
            const int pixels = std::max(1, static_cast<int>(std::lround(textSize * density_)));
            FT_Set_Pixel_Sizes(face_, 0, static_cast<FT_UInt>(pixels));
            const int ascender = static_cast<int>(face_->size->metrics.ascender >> 6);
            const int descender = static_cast<int>(-(face_->size->metrics.descender >> 6));
            const int height = static_cast<int>(std::lround(lineHeight * density_));
            const int baseline = (height - ascender - descender) / 2 + ascender;
            const auto codes = codepoints(text);
            int width = 0;
            for (const auto code : codes) {
                if (FT_Load_Char(face_, code, FT_LOAD_DEFAULT) == 0) {
                    width += static_cast<int>(face_->glyph->advance.x >> 6);
                }
            }
            entry.width = std::max(width, 1);
            entry.height = height;
            std::vector<std::uint8_t> alpha(static_cast<std::size_t>(entry.width) * static_cast<std::size_t>(height));
            int pen = 0;
            for (const auto code : codes) {
                if (FT_Load_Char(face_, code, FT_LOAD_RENDER) != 0) {
                    continue;
                }
                const FT_GlyphSlot glyph = face_->glyph;
                const FT_Bitmap& bitmap = glyph->bitmap;
                for (unsigned int row = 0; row < bitmap.rows; ++row) {
                    const int y = baseline - glyph->bitmap_top + static_cast<int>(row);
                    if (y < 0 || y >= height) {
                        continue;
                    }
                    for (unsigned int column = 0; column < bitmap.width; ++column) {
                        const int x = pen + glyph->bitmap_left + static_cast<int>(column);
                        if (x < 0 || x >= entry.width) {
                            continue;
                        }
                        auto& target = alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(entry.width) +
                                             static_cast<std::size_t>(x)];
                        target = std::max(target, bitmap.buffer[row * static_cast<unsigned int>(bitmap.pitch) + column]);
                    }
                }
                pen += static_cast<int>(glyph->advance.x >> 6);
            }
            glGenTextures(1, &entry.texture);
            glBindTexture(GL_TEXTURE_2D, entry.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            // uv (0, 0) is the top-left of the quad, so row 0 is the top.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, entry.width, height, 0, GL_RED, GL_UNSIGNED_BYTE, alpha.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
#else
        (void)text;
#endif
        return cache_.emplace(key, entry).first->second;
    }

    void clearCache()
    {
        for (auto& [key, entry] : cache_) {
            if (entry.texture != 0) {
                glDeleteTextures(1, &entry.texture);
            }
        }
        cache_.clear();
    }

    GLuint solidProgram_ = 0;
    GLuint textProgram_ = 0;
    GLint solidRect_ = -1;
    GLint solidColor_ = -1;
    GLint textRect_ = -1;
    GLint textColor_ = -1;
    GLuint vertexArray_ = 0;
    GLuint vertexBuffer_ = 0;
    float width_ = 1.0F;
    float height_ = 1.0F;
    int pixelWidth_ = 1;
    int pixelHeight_ = 1;
    float density_ = 1.0F;
    std::map<std::string, Cached> cache_;
#ifdef OPENA2_HAS_FREETYPE
    FT_Library library_ = nullptr;
    FT_Face face_ = nullptr;
#endif
};

// ---------------------------------------------------------------- widgets

struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;

    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Mouse state of one frame, in window points.
struct Mouse {
    float x = -1.0F;
    float y = -1.0F;
    bool down = false;
    // The widget the button went down on; it acts when released over it.
    int pressed = 0;
};

void drawButton(Painter& painter, const Rect& r, std::string_view label, const Mouse& mouse, int id,
                bool isDefault, bool enabled = true)
{
    const bool hover = enabled && r.contains(mouse.x, mouse.y);
    const bool pressed = hover && mouse.down && mouse.pressed == id;
    painter.fill(r.x, r.y, r.w, r.h, pressed ? buttonPressed : hover ? buttonHover : buttonFace);
    painter.outline(r.x, r.y, r.w, r.h, hover || isDefault ? accent : buttonBorder);
    if (isDefault) {
        painter.outline(r.x + 1.0F, r.y + 1.0F, r.w - 2.0F, r.h - 2.0F, accent);
    }
    const float width = painter.textWidth(label);
    painter.text(r.x + (r.w - width) / 2.0F, r.y + (r.h - lineHeight) / 2.0F, label,
                 enabled ? textColor : greyText);
}

void drawGroup(Painter& painter, const Rect& r, std::string_view title)
{
    const float top = r.y + lineHeight / 2.0F;
    painter.outline(r.x, top, r.w, r.h - lineHeight / 2.0F, groupLine);
    const float width = painter.textWidth(title);
    painter.fill(r.x + 6.0F, r.y, width + 6.0F, lineHeight, windowColor);
    painter.text(r.x + 9.0F, r.y, title, textColor);
}

// Runs frames until `step` returns false: polls the events (passing them
// on with mouse positions in dialog units), draws, swaps. Returns false when
// the window was closed.
bool frames(SDL_Window* window, Painter& painter, const std::function<bool(const SDL_Event*)>& step)
{
    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                return false;
            }
            const float scale = uiScale(window);
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                event.motion.x /= scale;
                event.motion.y /= scale;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                event.button.x /= scale;
                event.button.y /= scale;
            }
            if (!step(&event)) {
                return true;
            }
        }
        painter.begin(window);
        const bool more = step(nullptr);
        painter.end();
        SDL_GL_SwapWindow(window);
        if (!more) {
            return true;
        }
        SDL_Delay(10);
    }
}

// Sets the window size in window coordinates.
void setWindowSize(SDL_Window* window, int width, int height)
{
    SDL_SetWindowFullscreen(window, false);
    SDL_SetWindowResizable(window, false);
    SDL_SetWindowSize(window, width, height);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SyncWindow(window);
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
}

// Sets the window size in dialog units.
void setDialogSize(SDL_Window* window, int width, int height)
{
    const float scale = uiScale(window);
    setWindowSize(window, static_cast<int>(std::lround(width * scale)), static_cast<int>(std::lround(height * scale)));
}

// ---------------------------------------------------------------- folder picker

struct FolderSelection {
    std::mutex mutex;
    bool completed = false;
    std::optional<fs::path> path;
};

void SDLCALL onFolderSelected(void* userdata, const char* const* filelist, int)
{
    auto& selection = *static_cast<FolderSelection*>(userdata);
    std::lock_guard lock(selection.mutex);
    selection.completed = true;
    if (filelist == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The folder picker failed: %s", SDL_GetError());
    } else if (filelist[0] != nullptr) {
        selection.path = fromUtf8(filelist[0]);
    }
}

// The folder picker, starting in `start`; keeps `redraw` on screen meanwhile.
std::optional<fs::path> pickFolder(SDL_Window* window, Painter& painter, const fs::path& start,
                                   const std::function<void()>& redraw)
{
    FolderSelection selection;
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
    SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, pickTitle);
    const std::string location = pathText(start);
    if (!start.empty()) {
        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
    }
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, onFolderSelected, &selection, properties);
    frames(window, painter, [&](const SDL_Event* event) {
        if (event != nullptr) {
            return true;
        }
        redraw();
        std::lock_guard lock(selection.mutex);
        return !selection.completed;
    });
    // The callback may still be pending if the window was closed.
    while (true) {
        {
            std::lock_guard lock(selection.mutex);
            if (selection.completed) {
                break;
            }
        }
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(properties);
    return selection.path;
}

// Asks for the CD folder until a valid one is chosen; `out` starts as the
// last one used.
bool chooseCdFolder(SDL_Window* window, Painter& painter, const std::string& why, const fs::path& gameData,
                    fs::path& out, const std::function<void()>& redraw)
{
    if (message(window, SDL_MESSAGEBOX_INFORMATION, fillIn(chooseFormat, why, pathText(gameData)), okText,
                true) != Answer::ok) {
        return false;
    }
    std::error_code error;
    fs::path start = !out.empty() && fs::is_directory(out, error) ? out : findGameCd();
    while (true) {
        const auto picked = pickFolder(window, painter, start, redraw);
        if (!picked) {
            return false;
        }
        if (isGameSourceFolder(*picked)) {
            out = *picked;
            return true;
        }
        if (message(window, SDL_MESSAGEBOX_WARNING, fillIn(notCdFormat, pathText(*picked)), retryText, true) !=
            Answer::ok) {
            return false;
        }
        start = *picked;
    }
}

// ---------------------------------------------------------------- copying

// The progress window: what is being copied, a
// bar and Annuleren. Runs the copy on a worker thread.
bool installGame(SDL_Window* window, Painter& painter, const fs::path& source, const fs::path& gameData)
{
    constexpr int width = 420;
    constexpr int height = 132;
    int oldWidth = 0;
    int oldHeight = 0;
    SDL_GetWindowSize(window, &oldWidth, &oldHeight);
    const char* oldTitle = SDL_GetWindowTitle(window);
    const std::string savedTitle = oldTitle != nullptr ? oldTitle : "";
    setDialogSize(window, width, height);
    SDL_SetWindowTitle(window, installTitle);

    Installer installer(source, gameData);
    bool succeeded = false;
    bool finished = false;
    std::mutex doneMutex;
    std::thread worker([&] {
        const bool ok = installer.run();
        std::lock_guard lock(doneMutex);
        succeeded = ok;
        finished = true;
    });

    Mouse mouse;
    constexpr int cancelId = 1;
    const Rect cancelButton{width - 12.0F - 88.0F, height - 12.0F - 26.0F, 88.0F, 26.0F};
    const bool windowOpen = frames(window, painter, [&](const SDL_Event* event) {
        if (event != nullptr) {
            if (event->type == SDL_EVENT_MOUSE_MOTION) {
                mouse.x = event->motion.x;
                mouse.y = event->motion.y;
            } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
                mouse.down = true;
                mouse.pressed = cancelButton.contains(event->button.x, event->button.y) ? cancelId : 0;
            } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
                if (mouse.pressed == cancelId && cancelButton.contains(event->button.x, event->button.y)) {
                    installer.cancel();
                }
                mouse.down = false;
                mouse.pressed = 0;
            } else if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
                installer.cancel();
            }
            return true;
        }
        painter.text(12.0F, 12.0F, copyingText, textColor);
        const std::string file = installer.cancelled() ? cancellingText : installer.currentFile();
        painter.text(12.0F, 12.0F + lineHeight, file, textColor);
        const Rect bar{12.0F, 12.0F + 2.0F * lineHeight + 6.0F, width - 24.0F, 16.0F};
        painter.fill(bar.x, bar.y, bar.w, bar.h, progressBack);
        const auto total = installer.totalBytes();
        const float done = total == 0 ? 0.0F
                                      : static_cast<float>(static_cast<double>(installer.copiedBytes()) /
                                                           static_cast<double>(total));
        painter.fill(bar.x, bar.y, bar.w * std::clamp(done, 0.0F, 1.0F), bar.h, progressFill);
        painter.outline(bar.x, bar.y, bar.w, bar.h, buttonBorder);
        drawButton(painter, cancelButton, cancelText, mouse, cancelId, false, !installer.cancelled());
        std::lock_guard lock(doneMutex);
        return !finished;
    });
    if (!windowOpen) {
        installer.cancel();
    }
    worker.join();

    SDL_SetWindowTitle(window, savedTitle.c_str());
    if (oldWidth > 0 && oldHeight > 0) {
        setWindowSize(window, oldWidth, oldHeight);
    }
    if (!succeeded && !installer.cancelled() && !installer.error().empty()) {
        message(window, SDL_MESSAGEBOX_ERROR, fillIn(installFailedFormat, installer.error()), okText, false);
    }
    return succeeded && windowOpen;
}

// ---------------------------------------------------------------- start window

constexpr float dialogWidth = 400.0F;
constexpr float margin = 12.0F;
constexpr float groupPadding = 12.0F;
constexpr float controlHeight = 26.0F;

// A file:// URL for SDL_OpenURL, with the path percent-encoded.
std::string fileUrl(const fs::path& path)
{
    const std::u8string generic = path.generic_u8string();
    const std::string text(reinterpret_cast<const char*>(generic.data()), generic.size());
    std::string url = text.starts_with('/') ? "file://" : "file:///";
    constexpr const char* hex = "0123456789ABCDEF";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
            url.push_back(c);
        } else {
            url.push_back('%');
            url.push_back(hex[byte >> 4]);
            url.push_back(hex[byte & 0x0F]);
        }
    }
    return url;
}

class StartWindow {
public:
    StartWindow(SDL_Window* window, Painter& painter, LauncherSettings& settings, const StartPaths& paths)
        : window_(window)
        , painter_(painter)
        , settings_(settings)
        , paths_(paths)
    {
        chooseSizes();
    }

    bool run()
    {
        // Lay out once to know the height, then size the window.
        painter_.begin(window_);
        layout();
        painter_.end();
        setDialogSize(window_, static_cast<int>(dialogWidth), static_cast<int>(std::ceil(height_)));
        SDL_SetWindowTitle(window_, appTitle);
        bool play = false;
        done_ = false;
        const Uint64 startTicks = SDL_GetTicks();
        const bool open = frames(window_, painter_, [this, &play, startTicks](const SDL_Event* event) {
            if (event != nullptr) {
                handle(*event, play);
                return !done_;
            }
            layout();
            draw();
            if (!paths_.capture.empty() &&
                static_cast<float>(SDL_GetTicks() - startTicks) / 1000.0F > paths_.captureSeconds) {
                capture(paths_.capture);
                done_ = true;
            }
            return !done_;
        });
        return open && play;
    }

private:
    enum Id { none, combo, comboItem, check, fps, reinstall, openFolder, uninstall, play, quit };

    // A drop-down list; values are indices into windowResolutions() or
    // LauncherSettings::nativeRender.
    struct Combo {
        const char* label = "";
        int* value = nullptr;
        std::vector<int> values;
        // The text shown for each of `values`.
        std::vector<std::string> names;
        Rect box;
        Rect list;

        const std::string& name(int v) const
        {
            const auto found = std::find(values.begin(), values.end(), v);
            return names[found == values.end() ? 0 : static_cast<std::size_t>(found - values.begin())];
        }

        int selected() const
        {
            const auto found = std::find(values.begin(), values.end(), *value);
            return found == values.end() ? 0 : static_cast<int>(found - values.begin());
        }
    };

    // Only the sizes that fit the screen: window sizes in points within the
    // display, render resolutions within its pixels. 640 x 480 always stays.
    void chooseSizes()
    {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
        SDL_Rect bounds{};
        const bool haveBounds = SDL_GetDisplayBounds(display, &bounds) && bounds.w > 0 && bounds.h > 0;
        int pixelWidth = 0;
        int pixelHeight = 0;
        if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display); mode != nullptr) {
            const float density = mode->pixel_density > 0.0F ? mode->pixel_density : 1.0F;
            pixelWidth = static_cast<int>(std::lround(static_cast<float>(mode->w) * density));
            pixelHeight = static_cast<int>(std::lround(static_cast<float>(mode->h) * density));
        }
        const auto sizes = windowResolutions();
        combos_[0] = {windowSizeLabel, &settings_.resolution, {}, {}, {}, {}};
        combos_[1] = {renderLabel, &settings_.renderResolution, {LauncherSettings::nativeRender}, {nativeText}, {},
                      {}};
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            const int index = static_cast<int>(i);
            if (i == 0 || !haveBounds || (sizes[i].width <= bounds.w && sizes[i].height <= bounds.h)) {
                combos_[0].values.push_back(index);
                combos_[0].names.emplace_back(sizes[i].label);
            }
            if (i == 0 || pixelWidth <= 0 || (sizes[i].width <= pixelWidth && sizes[i].height <= pixelHeight)) {
                combos_[1].values.push_back(index);
                combos_[1].names.emplace_back(sizes[i].label);
            }
        }
        combos_[2] = {distanceLabel, &settings_.viewDistance, {}, {}, {}, {}};
        for (const int percent : viewDistances()) {
            combos_[2].values.push_back(percent);
            // 150 -> "1,5x" (Dutch decimal comma).
            std::string name = std::to_string(percent / 100);
            if (percent % 100 != 0) {
                name += "," + std::to_string(percent % 100 / 10);
            }
            combos_[2].names.push_back(percent == 100 ? std::string(distanceOriginalText) : name + "x");
        }
        combos_[3] = {tickLabel, &settings_.tickRate, {}, {}, {}, {}};
        for (const int rate : tickRates()) {
            combos_[3].values.push_back(rate);
            combos_[3].names.push_back(fillIn(rate == LauncherSettings{}.tickRate ? tickDefaultFormat : tickFormat,
                                              std::to_string(rate)));
        }
        // A saved size that no longer fits: the largest window that does,
        // and native rendering.
        const auto& windows = combos_[0].values;
        if (std::find(windows.begin(), windows.end(), settings_.resolution) == windows.end()) {
            settings_.resolution = windows.back();
        }
        const auto& renders = combos_[1].values;
        if (std::find(renders.begin(), renders.end(), settings_.renderResolution) == renders.end()) {
            settings_.renderResolution = LauncherSettings::nativeRender;
        }
    }

    // The window size only matters for a window, so it is hidden with full
    // screen.
    bool shown(std::size_t combo) const { return combo != 0 || !settings_.fullscreen; }

    // Places combo `i` at `y` when it is shown; returns the y below it.
    float placeCombo(std::size_t i, float y, float labelWidth)
    {
        const float inner = dialogWidth - 2.0F * margin - 2.0F * groupPadding;
        Combo& c = combos_[i];
        if (!shown(i)) {
            c.box = c.list = {};
            return y;
        }
        c.box = {margin + groupPadding + labelWidth, y, inner - labelWidth, controlHeight};
        const auto count = static_cast<float>(c.values.size());
        c.list = {c.box.x, c.box.y + c.box.h, c.box.w, count * (lineHeight + 4.0F) + 2.0F};
        return y + controlHeight + 6.0F;
    }

    void layout()
    {
        const float inner = dialogWidth - 2.0F * margin - 2.0F * groupPadding;
        float y = margin;
        pictureGroup_.x = margin;
        pictureGroup_.y = y;
        pictureGroup_.w = dialogWidth - 2.0F * margin;
        y += lineHeight + 6.0F;
        float labelWidth = 0.0F;
        for (const auto& c : combos_) {
            labelWidth = std::max(labelWidth, painter_.textWidth(c.label));
        }
        labelWidth += 12.0F;
        y = placeCombo(0, y, labelWidth);
        y = placeCombo(1, y, labelWidth);
        hint_ = painter_.wrap(resolutionHint, inner);
        hintY_ = y;
        y += static_cast<float>(hint_.size()) * lineHeight + 8.0F;
        checkLines_ = painter_.wrap(fullscreenText, inner - 22.0F);
        check_ = {margin + groupPadding, y, inner, static_cast<float>(checkLines_.size()) * lineHeight};
        y += check_.h + 6.0F;
        fps_ = {margin + groupPadding, y, inner, lineHeight};
        y += fps_.h + groupPadding;
        pictureGroup_.h = y - pictureGroup_.y;

        y += 8.0F;
        raceGroup_ = {margin, y, dialogWidth - 2.0F * margin, 0.0F};
        y += lineHeight + 6.0F;
        y = placeCombo(2, y, labelWidth);
        y = placeCombo(3, y, labelWidth);
        raceHint_ = painter_.wrap(raceHint, inner);
        raceHintY_ = y;
        y += static_cast<float>(raceHint_.size()) * lineHeight + groupPadding;
        raceGroup_.h = y - raceGroup_.y;

        y += 8.0F;
        filesGroup_ = {margin, y, dialogWidth - 2.0F * margin, 0.0F};
        y += lineHeight + 6.0F;
        reinstall_ = {margin + groupPadding, y, painter_.textWidth(reinstallText) + 28.0F, controlHeight};
        openFolder_ = {reinstall_.x + reinstall_.w + 10.0F, y, painter_.textWidth(openFolderText) + 28.0F,
                       controlHeight};
        y += controlHeight + groupPadding;
        filesGroup_.h = y - filesGroup_.y;

        y += 10.0F;
        lineY_ = y;
        y += 10.0F;
        constexpr float buttonWidth = 96.0F;
        quit_ = {dialogWidth - margin - buttonWidth, y, buttonWidth, controlHeight};
        uninstall_ = {margin, y, painter_.textWidth(uninstallText) + 28.0F, controlHeight};
        play_ = {quit_.x - 10.0F - buttonWidth, y, buttonWidth, controlHeight};
        y += controlHeight + margin;
        height_ = y;

        // A list that would run off the window opens upwards.
        for (auto& c : combos_) {
            if (c.list.y + c.list.h > height_) {
                c.list.y = c.box.y - c.list.h;
            }
        }
    }

    Rect itemRect(const Combo& c, int index) const
    {
        return {c.list.x + 1.0F, c.list.y + 1.0F + static_cast<float>(index) * (lineHeight + 4.0F), c.list.w - 2.0F,
                lineHeight + 4.0F};
    }

    // The widget under (x, y); `which` gets the combo for combo.
    int hit(float x, float y, int& which) const
    {
        if (open_ >= 0) {
            const Combo& c = combos_[static_cast<std::size_t>(open_)];
            which = open_;
            return c.list.contains(x, y) ? comboItem : c.box.contains(x, y) ? combo : none;
        }
        for (std::size_t i = 0; i < combos_.size(); ++i) {
            if (shown(i) && combos_[i].box.contains(x, y)) {
                which = static_cast<int>(i);
                return combo;
            }
        }
        if (check_.contains(x, y)) return check;
        if (fps_.contains(x, y)) return fps;
        if (reinstall_.contains(x, y)) return reinstall;
        if (openFolder_.contains(x, y)) return openFolder;
        if (uninstall_.contains(x, y)) return uninstall;
        if (play_.contains(x, y)) return play;
        if (quit_.contains(x, y)) return quit;
        return none;
    }

    void handle(const SDL_Event& event, bool& playChosen)
    {
        switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            mouse_.x = event.motion.x;
            mouse_.y = event.motion.y;
            if (open_ >= 0) {
                const Combo& c = combos_[static_cast<std::size_t>(open_)];
                if (c.list.contains(mouse_.x, mouse_.y)) {
                    highlight_ = std::clamp(static_cast<int>((mouse_.y - c.list.y - 1.0F) / (lineHeight + 4.0F)), 0,
                                            static_cast<int>(c.values.size()) - 1);
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button != SDL_BUTTON_LEFT) {
                break;
            }
            int which = -1;
            mouse_.down = true;
            mouse_.pressed = hit(event.button.x, event.button.y, which);
            if (mouse_.pressed == combo) {
                open_ = open_ == which ? -1 : which;
                if (open_ >= 0) {
                    highlight_ = combos_[static_cast<std::size_t>(open_)].selected();
                }
                mouse_.pressed = none;
            } else if (open_ >= 0 && mouse_.pressed == none) {
                open_ = -1;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event.button.button != SDL_BUTTON_LEFT) {
                break;
            }
            int which = -1;
            const int released = hit(event.button.x, event.button.y, which);
            const int pressed = mouse_.pressed;
            mouse_.down = false;
            mouse_.pressed = none;
            if (released != pressed) {
                break;
            }
            act(pressed, playChosen);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            if (open_ >= 0) {
                const int count = static_cast<int>(combos_[static_cast<std::size_t>(open_)].values.size());
                if (event.key.key == SDLK_UP) {
                    highlight_ = std::max(highlight_ - 1, 0);
                } else if (event.key.key == SDLK_DOWN) {
                    highlight_ = std::min(highlight_ + 1, count - 1);
                } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                    act(comboItem, playChosen);
                } else if (event.key.key == SDLK_ESCAPE) {
                    open_ = -1;
                }
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                act(play, playChosen);
            } else if (event.key.key == SDLK_ESCAPE) {
                act(quit, playChosen);
            }
            break;
        default:
            break;
        }
    }

    void act(int id, bool& playChosen)
    {
        switch (id) {
        case comboItem:
            if (open_ >= 0) {
                Combo& c = combos_[static_cast<std::size_t>(open_)];
                *c.value = c.values[static_cast<std::size_t>(highlight_)];
            }
            open_ = -1;
            break;
        case check: {
            settings_.fullscreen = !settings_.fullscreen;
            // The window size row comes and goes: fit the window to it,
            // keeping its top-left where it is.
            layout();
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(window_, &width, &height);
            SDL_SetWindowSize(window_, width, static_cast<int>(std::ceil(height_ * uiScale(window_))));
            break;
        }
        case fps:
            settings_.showFps = !settings_.showFps;
            break;
        case reinstall: {
            fs::path cd = settings_.sourceFolder;
            const auto redraw = [this] {
                layout();
                draw();
            };
            if (chooseCdFolder(window_, painter_, reinstallWhy, paths_.gameData, cd, redraw) &&
                installGame(window_, painter_, cd, paths_.gameData)) {
                settings_.sourceFolder = cd;
                saveLauncherSettings(paths_.settings, settings_);
            }
            mouse_ = {};
            break;
        }
        case openFolder:
            if (!SDL_OpenURL(fileUrl(paths_.gameData).c_str())) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not open %s: %s", pathText(paths_.gameData).c_str(),
                             SDL_GetError());
            }
            mouse_ = {};
            break;
        case uninstall:
            mouse_ = {};
            if (removeAll()) {
                playChosen = false;
                done_ = true;
            }
            break;
        case play:
            playChosen = true;
            done_ = true;
            break;
        case quit:
            playChosen = false;
            done_ = true;
            break;
        default:
            break;
        }
    }

    // Uninstalling: macOS does not tell an app that it is moved to the
    // Trash, so the start window offers it. Deletes the preference folder's
    // game files and settings (the saves only when asked), then moves the
    // app to the Trash. True when it ran, so the program quits.
    bool removeAll()
    {
        if (message(window_, SDL_MESSAGEBOX_WARNING, uninstallConfirm, uninstallButton, true) != Answer::ok) {
            return false;
        }
        const int saves = ask(window_, SDL_MESSAGEBOX_INFORMATION, savesQuestion, {yesText, noText, cancelText});
        if (saves == 2) {
            return false;
        }
        std::string error;
        removeUserData(paths_.userFolder, saves == 0, error);
        const fs::path app = appBundle();
        const bool trashed = !app.empty() && moveToTrash(app);
        if (!error.empty()) {
            message(window_, SDL_MESSAGEBOX_ERROR, fillIn(removeFailedFormat, error), okText, false);
        } else if (trashed) {
            message(window_, SDL_MESSAGEBOX_INFORMATION, removedText, okText, false);
        } else {
            const char* base = SDL_GetBasePath();
            const fs::path program = !app.empty() ? app : base != nullptr ? fromUtf8(base) : fs::path();
            message(window_, SDL_MESSAGEBOX_INFORMATION, fillIn(removedProgramFormat, pathText(program)), okText,
                    false);
        }
        return true;
    }

    // Saves the frame being drawn (the back buffer) as a BMP.
    void capture(const fs::path& path) const
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        const auto w = static_cast<std::size_t>(width);
        const auto h = static_cast<std::size_t>(height);
        std::vector<std::uint8_t> pixels(w * h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (std::size_t row = 0; row < h / 2; ++row) {
            std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(row * w * 4),
                             pixels.begin() + static_cast<std::ptrdiff_t>((row + 1) * w * 4),
                             pixels.begin() + static_cast<std::ptrdiff_t>((h - 1 - row) * w * 4));
        }
        if (SDL_Surface* surface =
                SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4)) {
            SDL_SaveBMP(surface, pathText(path).c_str());
            SDL_DestroySurface(surface);
        }
    }

    void drawCheckBox(const Rect& r, bool checked, const std::vector<std::string>& lines)
    {
        const float box = 14.0F;
        const float bx = r.x;
        const float by = r.y + (lineHeight - box) / 2.0F;
        const bool hover = r.contains(mouse_.x, mouse_.y) && open_ < 0;
        painter_.fill(bx, by, box, box, checked ? accent : fieldColor);
        painter_.outline(bx, by, box, box, checked || hover ? accent : rgb(0x333333));
        if (checked) {
            // A tick, as two thick strokes.
            for (int i = 0; i < 3; ++i) {
                const auto f = static_cast<float>(i);
                painter_.fill(bx + 3.0F + f, by + 6.0F + f, 1.6F, 1.6F, white);
            }
            for (int i = 0; i < 6; ++i) {
                const auto f = static_cast<float>(i);
                painter_.fill(bx + 5.0F + f, by + 8.0F - f, 1.6F, 1.6F, white);
            }
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            painter_.text(r.x + 22.0F, r.y + static_cast<float>(i) * lineHeight, lines[i], textColor);
        }
    }

    void draw()
    {
        drawGroup(painter_, pictureGroup_, pictureGroup);
        for (std::size_t i = 0; i < combos_.size(); ++i) {
            if (!shown(i)) {
                continue;
            }
            const Combo& c = combos_[i];
            painter_.text(margin + groupPadding, c.box.y + (controlHeight - lineHeight) / 2.0F, c.label, textColor);
            const bool isOpen = open_ == static_cast<int>(i);
            const bool hover = c.box.contains(mouse_.x, mouse_.y) && (open_ < 0 || isOpen);
            painter_.fill(c.box.x, c.box.y, c.box.w, c.box.h, fieldColor);
            painter_.outline(c.box.x, c.box.y, c.box.w, c.box.h, hover || isOpen ? accent : buttonBorder);
            painter_.text(c.box.x + 8.0F, c.box.y + (controlHeight - lineHeight) / 2.0F, c.name(*c.value),
                          textColor);
            // The drop-down arrow.
            const float ax = c.box.x + c.box.w - 18.0F;
            const float ay = c.box.y + c.box.h / 2.0F - 2.0F;
            for (int row = 0; row < 4; ++row) {
                const auto r = static_cast<float>(row);
                painter_.fill(ax + r, ay + r, 8.0F - 2.0F * r, 1.0F, textColor);
            }
        }
        for (std::size_t i = 0; i < hint_.size(); ++i) {
            painter_.text(margin + groupPadding, hintY_ + static_cast<float>(i) * lineHeight, hint_[i], greyText);
        }
        drawCheckBox(check_, settings_.fullscreen, checkLines_);
        drawCheckBox(fps_, settings_.showFps, {fpsText});

        drawGroup(painter_, raceGroup_, raceGroup);
        for (std::size_t i = 0; i < raceHint_.size(); ++i) {
            painter_.text(margin + groupPadding, raceHintY_ + static_cast<float>(i) * lineHeight, raceHint_[i],
                          greyText);
        }

        drawGroup(painter_, filesGroup_, filesGroup);
        const Mouse buttonsMouse = open_ >= 0 ? Mouse{} : mouse_;
        drawButton(painter_, reinstall_, reinstallText, buttonsMouse, reinstall, false);
        drawButton(painter_, openFolder_, openFolderText, buttonsMouse, openFolder, false);
        drawButton(painter_, uninstall_, uninstallText, buttonsMouse, uninstall, false);

        painter_.fill(margin, lineY_, dialogWidth - 2.0F * margin, 1.0F, groupLine);
        drawButton(painter_, play_, playText, buttonsMouse, play, true);
        drawButton(painter_, quit_, quitText, buttonsMouse, quit, false);

        if (open_ >= 0) {
            const Combo& c = combos_[static_cast<std::size_t>(open_)];
            painter_.fill(c.list.x, c.list.y, c.list.w, c.list.h, fieldColor);
            painter_.outline(c.list.x, c.list.y, c.list.w, c.list.h, buttonBorder);
            for (std::size_t i = 0; i < c.values.size(); ++i) {
                const Rect item = itemRect(c, static_cast<int>(i));
                const bool selected = static_cast<int>(i) == highlight_;
                if (selected) {
                    painter_.fill(item.x, item.y, item.w, item.h, listHover);
                }
                painter_.text(item.x + 7.0F, item.y + 2.0F, c.names[i], selected ? white : textColor);
            }
        }
    }

    SDL_Window* window_;
    Painter& painter_;
    LauncherSettings& settings_;
    const StartPaths& paths_;
    Mouse mouse_;
    // The open drop-down list, or -1.
    int open_ = -1;
    bool done_ = false;
    int highlight_ = 0;
    float height_ = 0.0F;
    std::array<Combo, 4> combos_;
    Rect pictureGroup_;
    Rect check_;
    Rect fps_;
    Rect raceGroup_;
    float raceHintY_ = 0.0F;
    std::vector<std::string> raceHint_;
    Rect filesGroup_;
    Rect reinstall_;
    Rect openFolder_;
    Rect uninstall_;
    Rect play_;
    Rect quit_;
    float hintY_ = 0.0F;
    float lineY_ = 0.0F;
    std::vector<std::string> hint_;
    std::vector<std::string> checkLines_;
};

} // namespace

bool ensureInstalled(SDL_Window* window, LauncherSettings& settings, const StartPaths& paths)
{
    if (isInstalled(paths.gameData)) {
        return true;
    }
    Painter painter;
    if (!painter.initialize()) {
        return false;
    }
    setDialogSize(window, 420, 132);
    SDL_SetWindowTitle(window, appTitle);
    std::error_code error;
    const std::string why = fs::exists(paths.gameData, error) ? fillIn(incompleteFormat, pathText(paths.gameData))
                                                              : std::string(firstRunText);
    const auto blank = [] {};
    fs::path cd = settings.sourceFolder;
    if (!chooseCdFolder(window, painter, why, paths.gameData, cd, blank) ||
        !installGame(window, painter, cd, paths.gameData)) {
        return false;
    }
    settings.sourceFolder = cd;
    saveLauncherSettings(paths.settings, settings);
    return true;
}

bool runStartWindow(SDL_Window* window, LauncherSettings& settings, const StartPaths& paths)
{
    Painter painter;
    if (!painter.initialize()) {
        return true;
    }
    StartWindow start(window, painter, settings, paths);
    return start.run();
}

// Shows a black frame, so the start window's last frame is not stretched
// over the screen while the window changes size or until the game draws.
static void presentBlack(SDL_Window* window)
{
    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    glViewport(0, 0, pixelWidth, pixelHeight);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(window);
}

void applyWindowSettings(SDL_Window* window, const LauncherSettings& settings)
{
    presentBlack(window);

    const auto choices = windowResolutions();
    const auto& choice =
        choices[static_cast<std::size_t>(std::clamp(settings.resolution, 0, static_cast<int>(choices.size()) - 1))];
    int width = choice.width;
    int height = choice.height;
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable) && usable.w > 0 && usable.h > 0) {
        // Leave room for the title bar; keep 4:3.
        const double scale = std::min({1.0, static_cast<double>(usable.w) / width,
                                       static_cast<double>(usable.h - 40) / height});
        width = static_cast<int>(width * scale);
        height = static_cast<int>(height * scale);
    }
    SDL_SetWindowTitle(window, "OpenA2Racer");
    SDL_SetWindowResizable(window, true);
    SDL_SetWindowSize(window, width, height);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowFullscreen(window, settings.fullscreen);
    SDL_ShowWindow(window);
    SDL_SyncWindow(window);
    presentBlack(window);
}

} // namespace opena2::game
