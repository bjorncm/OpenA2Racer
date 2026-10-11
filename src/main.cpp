#include <SDL3/SDL.h>
// Turns main into the Windows entry point (WinMain).
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_opengl.h>

#include "opena2/indexed_archive.hpp"
#include "opena2/indexed_image.hpp"
#include "opena2/game_data_directory.hpp"
#include "opena2/d3d_mesh.hpp"
#include "opena2/pcm_wave.hpp"
#include "opena2/vehicle.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using DrawArraysProc = void (APIENTRYP)(GLenum mode, GLint first, GLsizei count);
using DrawElementsProc = void (APIENTRYP)(
    GLenum mode, GLsizei count, GLenum type, const void* indices);

struct OpenGLFunctions {
    PFNGLCREATESHADERPROC createShader = nullptr;
    PFNGLSHADERSOURCEPROC shaderSource = nullptr;
    PFNGLCOMPILESHADERPROC compileShader = nullptr;
    PFNGLGETSHADERIVPROC getShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC getShaderInfoLog = nullptr;
    PFNGLDELETESHADERPROC deleteShader = nullptr;
    PFNGLCREATEPROGRAMPROC createProgram = nullptr;
    PFNGLATTACHSHADERPROC attachShader = nullptr;
    PFNGLBINDATTRIBLOCATIONPROC bindAttribLocation = nullptr;
    PFNGLLINKPROGRAMPROC linkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC getProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog = nullptr;
    PFNGLDELETEPROGRAMPROC deleteProgram = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC getUniformLocation = nullptr;
    PFNGLGENVERTEXARRAYSPROC genVertexArrays = nullptr;
    PFNGLBINDVERTEXARRAYPROC bindVertexArray = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC deleteVertexArrays = nullptr;
    PFNGLGENBUFFERSPROC genBuffers = nullptr;
    PFNGLBINDBUFFERPROC bindBuffer = nullptr;
    PFNGLDELETEBUFFERSPROC deleteBuffers = nullptr;
    PFNGLBUFFERDATAPROC bufferData = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enableVertexAttribArray = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC vertexAttribPointer = nullptr;
    PFNGLUSEPROGRAMPROC useProgram = nullptr;
    PFNGLUNIFORM2FPROC uniform2f = nullptr;
    PFNGLUNIFORM1IPROC uniform1i = nullptr;
    PFNGLUNIFORMMATRIX4FVPROC uniformMatrix4fv = nullptr;
    PFNGLACTIVETEXTUREPROC activeTexture = nullptr;
    DrawArraysProc drawArrays = nullptr;
    DrawElementsProc drawElements = nullptr;

    bool load(std::string& error)
    {
#define LOAD_GL_PROC(member, symbol) \
        member = reinterpret_cast<decltype(member)>(SDL_GL_GetProcAddress(symbol)); \
        if (member == nullptr) { \
            error = "Could not load OpenGL function " symbol "."; \
            return false; \
        }
        LOAD_GL_PROC(createShader, "glCreateShader")
        LOAD_GL_PROC(shaderSource, "glShaderSource")
        LOAD_GL_PROC(compileShader, "glCompileShader")
        LOAD_GL_PROC(getShaderiv, "glGetShaderiv")
        LOAD_GL_PROC(getShaderInfoLog, "glGetShaderInfoLog")
        LOAD_GL_PROC(deleteShader, "glDeleteShader")
        LOAD_GL_PROC(createProgram, "glCreateProgram")
        LOAD_GL_PROC(attachShader, "glAttachShader")
        LOAD_GL_PROC(bindAttribLocation, "glBindAttribLocation")
        LOAD_GL_PROC(linkProgram, "glLinkProgram")
        LOAD_GL_PROC(getProgramiv, "glGetProgramiv")
        LOAD_GL_PROC(getProgramInfoLog, "glGetProgramInfoLog")
        LOAD_GL_PROC(deleteProgram, "glDeleteProgram")
        LOAD_GL_PROC(getUniformLocation, "glGetUniformLocation")
        LOAD_GL_PROC(genVertexArrays, "glGenVertexArrays")
        LOAD_GL_PROC(bindVertexArray, "glBindVertexArray")
        LOAD_GL_PROC(deleteVertexArrays, "glDeleteVertexArrays")
        LOAD_GL_PROC(genBuffers, "glGenBuffers")
        LOAD_GL_PROC(bindBuffer, "glBindBuffer")
        LOAD_GL_PROC(deleteBuffers, "glDeleteBuffers")
        LOAD_GL_PROC(bufferData, "glBufferData")
        LOAD_GL_PROC(enableVertexAttribArray, "glEnableVertexAttribArray")
        LOAD_GL_PROC(vertexAttribPointer, "glVertexAttribPointer")
        LOAD_GL_PROC(useProgram, "glUseProgram")
        LOAD_GL_PROC(uniform2f, "glUniform2f")
        LOAD_GL_PROC(uniform1i, "glUniform1i")
        LOAD_GL_PROC(uniformMatrix4fv, "glUniformMatrix4fv")
        LOAD_GL_PROC(activeTexture, "glActiveTexture")
        LOAD_GL_PROC(drawArrays, "glDrawArrays")
        LOAD_GL_PROC(drawElements, "glDrawElements")
#undef LOAD_GL_PROC
        return true;
    }
};

OpenGLFunctions glFunctions;

enum class AssetCategory : std::size_t {
    gameSprites,
    mrcImages,
    a2mrcsPcx,
    rcsPcx,
    audio,
    models,
    count
};

constexpr std::size_t categoryCount =
    static_cast<std::size_t>(AssetCategory::count);

#define glCreateShader glFunctions.createShader
#define glShaderSource glFunctions.shaderSource
#define glCompileShader glFunctions.compileShader
#define glGetShaderiv glFunctions.getShaderiv
#define glGetShaderInfoLog glFunctions.getShaderInfoLog
#define glDeleteShader glFunctions.deleteShader
#define glCreateProgram glFunctions.createProgram
#define glAttachShader glFunctions.attachShader
#define glBindAttribLocation glFunctions.bindAttribLocation
#define glLinkProgram glFunctions.linkProgram
#define glGetProgramiv glFunctions.getProgramiv
#define glGetProgramInfoLog glFunctions.getProgramInfoLog
#define glDeleteProgram glFunctions.deleteProgram
#define glGetUniformLocation glFunctions.getUniformLocation
#define glGenVertexArrays glFunctions.genVertexArrays
#define glBindVertexArray glFunctions.bindVertexArray
#define glDeleteVertexArrays glFunctions.deleteVertexArrays
#define glGenBuffers glFunctions.genBuffers
#define glBindBuffer glFunctions.bindBuffer
#define glDeleteBuffers glFunctions.deleteBuffers
#define glBufferData glFunctions.bufferData
#define glEnableVertexAttribArray glFunctions.enableVertexAttribArray
#define glVertexAttribPointer glFunctions.vertexAttribPointer
#define glUseProgram glFunctions.useProgram
#define glUniform2f glFunctions.uniform2f
#define glUniform1i glFunctions.uniform1i
#define glUniformMatrix4fv glFunctions.uniformMatrix4fv
#define glActiveTexture glFunctions.activeTexture
#define glDrawArrays glFunctions.drawArrays
#define glDrawElements glFunctions.drawElements

struct SpriteRenderer {
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint texture = 0;
    GLuint paletteTexture = 0;
    GLint scaleUniform = -1;
    GLint imageUniform = -1;
    GLint paletteUniform = -1;
    GLint hasPaletteUniform = -1;

    bool updateImage(const opena2::IndexedImage& image, std::string& error)
    {
        GLuint replacement = 0;
        GLuint replacementPalette = 0;
        glGenTextures(1, &replacement);
        if (replacement == 0) {
            error = "Could not create OpenGL texture.";
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, replacement);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_R8,
            static_cast<GLsizei>(image.width),
            static_cast<GLsizei>(image.height),
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            image.pixels.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (!image.paletteRgb.empty()) {
            glGenTextures(1, &replacementPalette);
            if (replacementPalette == 0) {
                glDeleteTextures(1, &replacement);
                error = "Could not create OpenGL palette texture.";
                return false;
            }
            glBindTexture(GL_TEXTURE_2D, replacementPalette);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGB8,
                256,
                1,
                0,
                GL_RGB,
                GL_UNSIGNED_BYTE,
                image.paletteRgb.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &replacement);
            if (replacementPalette != 0) {
                glDeleteTextures(1, &replacementPalette);
            }
            error = "Could not upload indexed image to OpenGL.";
            return false;
        }

        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
        if (paletteTexture != 0) {
            glDeleteTextures(1, &paletteTexture);
        }
        texture = replacement;
        paletteTexture = replacementPalette;
        return true;
    }

    void destroy()
    {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        if (paletteTexture != 0) {
            glDeleteTextures(1, &paletteTexture);
            paletteTexture = 0;
        }
        if (vertexBuffer != 0) {
            glDeleteBuffers(1, &vertexBuffer);
            vertexBuffer = 0;
        }
        if (vertexArray != 0) {
            glDeleteVertexArrays(1, &vertexArray);
            vertexArray = 0;
        }
        if (program != 0) {
            glDeleteProgram(program);
            program = 0;
        }
    }

    bool initialize(const opena2::IndexedImage& image, std::string& error)
    {
        constexpr const char* vertexSource = R"glsl(#version 150 core
in vec2 position;
in vec2 texCoord;
uniform vec2 imageScale;
out vec2 uv;
void main()
{
    gl_Position = vec4(position * imageScale, 0.0, 1.0);
    uv = texCoord;
}
)glsl";
        constexpr const char* fragmentSource = R"glsl(#version 150 core
uniform sampler2D indexedPixels;
uniform sampler2D colorPalette;
uniform bool hasPalette;
in vec2 uv;
out vec4 color;
void main()
{
    float value = texture(indexedPixels, uv).r;
    if (hasPalette) {
        float paletteIndex = floor(value * 255.0 + 0.5);
        color = vec4(texture(colorPalette, vec2((paletteIndex + 0.5) / 256.0, 0.5)).rgb, 1.0);
    } else {
        color = vec4(value, value, value, 1.0);
    }
}
)glsl";

        const auto compileShader = [&error](GLenum type, const char* source) -> GLuint {
            const GLuint shader = glCreateShader(type);
            if (shader == 0) {
                error = "Could not create an OpenGL shader.";
                return 0;
            }

            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_FALSE) {
                GLint logLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
                std::string log(static_cast<std::size_t>(logLength > 1 ? logLength : 1), '\0');
                glGetShaderInfoLog(shader, logLength, nullptr, log.data());
                error = "Could not compile OpenGL shader: " + log;
                glDeleteShader(shader);
                return 0;
            }

            return shader;
        };

        const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
        if (vertexShader == 0) {
            return false;
        }
        const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            return false;
        }

        program = glCreateProgram();
        if (program == 0) {
            error = "Could not create an OpenGL program.";
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return false;
        }
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glBindAttribLocation(program, 0, "position");
        glBindAttribLocation(program, 1, "texCoord");
        glLinkProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE) {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<std::size_t>(logLength > 1 ? logLength : 1), '\0');
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            error = "Could not link OpenGL program: " + log;
            destroy();
            return false;
        }

        scaleUniform = glGetUniformLocation(program, "imageScale");
        imageUniform = glGetUniformLocation(program, "indexedPixels");
        paletteUniform = glGetUniformLocation(program, "colorPalette");
        hasPaletteUniform = glGetUniformLocation(program, "hasPalette");
        if (scaleUniform < 0 || imageUniform < 0 ||
            paletteUniform < 0 || hasPaletteUniform < 0) {
            error = "Could not find required OpenGL shader uniforms.";
            destroy();
            return false;
        }

        constexpr GLfloat vertices[] = {
            -1.0F, -1.0F, 0.0F, 1.0F,
             1.0F, -1.0F, 1.0F, 1.0F,
            -1.0F,  1.0F, 0.0F, 0.0F,
             1.0F,  1.0F, 1.0F, 0.0F,
        };
        glGenVertexArrays(1, &vertexArray);
        glBindVertexArray(vertexArray);
        glGenBuffers(1, &vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
            reinterpret_cast<const void*>(2 * sizeof(GLfloat)));

        glBindVertexArray(0);

        if (!updateImage(image, error)) {
            destroy();
            return false;
        }
        return true;
    }

    void draw(SDL_Window* window, const opena2::IndexedImage& image) const
    {
        int logicalWidth = 0;
        int logicalHeight = 0;
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &logicalWidth, &logicalHeight) ||
            !SDL_GetWindowSizeInPixels(window, &width, &height) ||
            logicalWidth <= 0 || logicalHeight <= 0 || width <= 0 || height <= 0) {
            return;
        }

        constexpr int left = 326;
        constexpr int top = 98;
        constexpr int margin = 18;
        const int viewWidth = logicalWidth - left - margin;
        const int viewHeight = logicalHeight - top - margin;
        if (viewWidth <= 0 || viewHeight <= 0) {
            return;
        }
        const float pixelScaleX = static_cast<float>(width) /
            static_cast<float>(logicalWidth);
        const float pixelScaleY = static_cast<float>(height) /
            static_cast<float>(logicalHeight);
        const int viewportX = static_cast<int>(static_cast<float>(left) * pixelScaleX);
        const int viewportY = height - static_cast<int>(
            static_cast<float>(top + viewHeight) * pixelScaleY);
        const int viewportWidth = static_cast<int>(static_cast<float>(viewWidth) * pixelScaleX);
        const int viewportHeight = static_cast<int>(static_cast<float>(viewHeight) * pixelScaleY);
        const float windowAspect = static_cast<float>(viewportWidth) /
            static_cast<float>(viewportHeight);
        const float imageAspect = static_cast<float>(image.width) / static_cast<float>(image.height);
        const float scaleX = windowAspect > imageAspect ? imageAspect / windowAspect : 1.0F;
        const float scaleY = windowAspect > imageAspect ? 1.0F : windowAspect / imageAspect;

        glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
        glUseProgram(program);
        glUniform2f(scaleUniform, scaleX, scaleY);
        glUniform1i(imageUniform, 0);
        glUniform1i(paletteUniform, 1);
        glUniform1i(hasPaletteUniform, paletteTexture != 0 ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, paletteTexture);
        glBindVertexArray(vertexArray);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }

};

struct ModelPreviewVertex {
    GLfloat position[3];
    GLfloat normal[3];
    GLfloat color[3];
};

struct D3DModelRenderer {
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    GLuint trackVertexArray = 0;
    GLuint trackVertexBuffer = 0;
    GLint mvpUniform = -1;
    GLint modelUniform = -1;
    std::size_t indexCount = 0;
    std::array<float, 3> center{};
    float groundOffset = 0.0F;
    float scale = 1.0F;
    float cameraDistance = 4.8F;

    bool initialize(std::string& error)
    {
        constexpr const char* vertexSource = R"glsl(#version 150 core
in vec3 position;
in vec3 normal;
in vec3 vertexColor;
uniform mat4 modelTransform;
uniform mat4 mvp;
out vec3 surfaceNormal;
out vec3 surfaceColor;
void main()
{
    gl_Position = mvp * vec4(position, 1.0);
    surfaceNormal = mat3(modelTransform) * normal;
    surfaceColor = vertexColor;
}
)glsl";
        constexpr const char* fragmentSource = R"glsl(#version 150 core
in vec3 surfaceNormal;
in vec3 surfaceColor;
out vec4 color;
void main()
{
    vec3 normal = length(surfaceNormal) > 0.001
        ? normalize(surfaceNormal)
        : vec3(0.0, 0.0, 1.0);
    float lighting = 0.28 + 0.72 * max(dot(normal, normalize(vec3(0.4, 0.7, 0.6))), 0.0);
    color = vec4(surfaceColor * lighting, 1.0);
}
)glsl";
        const auto compile = [&error](GLenum type, const char* source) -> GLuint {
            const GLuint shader = glCreateShader(type);
            if (shader == 0) {
                error = "Could not create a D3D preview shader.";
                return 0;
            }
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_FALSE) {
                GLint length = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
                glGetShaderInfoLog(shader, length, nullptr, log.data());
                error = "Could not compile D3D preview shader: " + log;
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        };
        const GLuint vertexShader = compile(GL_VERTEX_SHADER, vertexSource);
        if (vertexShader == 0) {
            return false;
        }
        const GLuint fragmentShader = compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            return false;
        }
        program = glCreateProgram();
        if (program == 0) {
            error = "Could not create a D3D preview program.";
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return false;
        }
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glBindAttribLocation(program, 0, "position");
        glBindAttribLocation(program, 1, "normal");
        glBindAttribLocation(program, 2, "vertexColor");
        glLinkProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            error = "Could not link D3D preview program: " + log;
            return false;
        }
        mvpUniform = glGetUniformLocation(program, "mvp");
        modelUniform = glGetUniformLocation(program, "modelTransform");
        if (mvpUniform < 0 || modelUniform < 0) {
            error = "Could not find required D3D preview shader uniforms.";
            return false;
        }
        glGenVertexArrays(1, &vertexArray);
        glGenBuffers(1, &vertexBuffer);
        glGenBuffers(1, &indexBuffer);
        if (vertexArray == 0 || vertexBuffer == 0 || indexBuffer == 0) {
            error = "Could not create D3D preview geometry buffers.";
            return false;
        }
        glBindVertexArray(vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelPreviewVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelPreviewVertex),
            reinterpret_cast<const void*>(3 * sizeof(GLfloat)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 3, GL_FLOAT, GL_FALSE, sizeof(ModelPreviewVertex),
            reinterpret_cast<const void*>(6 * sizeof(GLfloat)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glBindVertexArray(0);

        glGenVertexArrays(1, &trackVertexArray);
        glGenBuffers(1, &trackVertexBuffer);
        if (trackVertexArray == 0 || trackVertexBuffer == 0) {
            error = "Could not create sandbox track buffers.";
            return false;
        }
        glBindVertexArray(trackVertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, trackVertexBuffer);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelPreviewVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelPreviewVertex),
            reinterpret_cast<const void*>(3 * sizeof(GLfloat)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 3, GL_FLOAT, GL_FALSE, sizeof(ModelPreviewVertex),
            reinterpret_cast<const void*>(6 * sizeof(GLfloat)));
        glBindVertexArray(0);
        return true;
    }

    bool updateMesh(const opena2::D3DMesh& mesh, std::string& error)
    {
        std::vector<ModelPreviewVertex> vertices;
        vertices.reserve(mesh.vertices.size());
        std::array<float, 3> minimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        std::array<float, 3> maximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        for (const auto& vertex : mesh.vertices) {
            ModelPreviewVertex preview{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                preview.position[axis] = vertex.position[axis];
                preview.normal[axis] = vertex.normal[axis];
                minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
                maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
            }
            preview.color[0] = 0.22F;
            preview.color[1] = 0.62F;
            preview.color[2] = 0.86F;
            vertices.push_back(preview);
        }
        std::vector<std::uint16_t> indices;
        indices.reserve(mesh.faces.size() * 3);
        for (const auto& face : mesh.faces) {
            indices.insert(indices.end(), face.indices.begin(), face.indices.end());
        }
        if (vertices.empty() || indices.empty() ||
            vertices.size() > static_cast<std::size_t>(
                std::numeric_limits<GLsizeiptr>::max()) / sizeof(ModelPreviewVertex) ||
            indices.size() > static_cast<std::size_t>(
                std::numeric_limits<GLsizeiptr>::max()) / sizeof(std::uint16_t) ||
            indices.size() > static_cast<std::size_t>(
                std::numeric_limits<GLsizei>::max())) {
            error = "D3D mesh is too large or empty for OpenGL preview.";
            return false;
        }
        const float extentX = maximum[0] - minimum[0];
        const float extentY = maximum[1] - minimum[1];
        const float extentZ = maximum[2] - minimum[2];
        const float extent = std::max({extentX, extentY, extentZ});
        if (!std::isfinite(extent) || extent <= 0.0F) {
            error = "D3D mesh has no usable spatial extent.";
            return false;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            center[axis] = minimum[axis] + (maximum[axis] - minimum[axis]) * 0.5F;
        }
        groundOffset = minimum[1];
        scale = 2.0F / extent;
        if (!std::isfinite(scale)) {
            error = "D3D mesh cannot be scaled for OpenGL preview.";
            return false;
        }
        const float halfDiagonal = 0.5F * std::sqrt(
            extentX * extentX + extentY * extentY + extentZ * extentZ);
        cameraDistance = std::max(
            4.8F, halfDiagonal * scale / std::sin(0.392699082F) * 1.15F);

        glBindVertexArray(vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(ModelPreviewVertex)),
                     vertices.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint16_t)),
                     indices.data(),
                     GL_STATIC_DRAW);
        glBindVertexArray(0);
        if (glGetError() != GL_NO_ERROR) {
            error = "Could not upload D3D mesh to OpenGL.";
            return false;
        }
        indexCount = indices.size();
        return true;
    }

    static std::array<float, 16> multiply(
        const std::array<float, 16>& left,
        const std::array<float, 16>& right)
    {
        std::array<float, 16> result{};
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                for (int inner = 0; inner < 4; ++inner) {
                    result[static_cast<std::size_t>(column * 4 + row)] +=
                        left[static_cast<std::size_t>(inner * 4 + row)] *
                        right[static_cast<std::size_t>(column * 4 + inner)];
                }
            }
        }
        return result;
    }

    void draw(SDL_Window* window, float elapsedSeconds) const
    {
        if (indexCount == 0) {
            return;
        }
        int logicalWidth = 0;
        int logicalHeight = 0;
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &logicalWidth, &logicalHeight) ||
            !SDL_GetWindowSizeInPixels(window, &width, &height) ||
            logicalWidth <= 0 || logicalHeight <= 0 || width <= 0 || height <= 0) {
            return;
        }
        constexpr int left = 326;
        constexpr int top = 98;
        constexpr int margin = 18;
        const int viewWidth = logicalWidth - left - margin;
        const int viewHeight = logicalHeight - top - margin;
        if (viewWidth <= 0 || viewHeight <= 0) {
            return;
        }
        const float pixelScaleX = static_cast<float>(width) /
            static_cast<float>(logicalWidth);
        const float pixelScaleY = static_cast<float>(height) /
            static_cast<float>(logicalHeight);
        const int viewportX = static_cast<int>(static_cast<float>(left) * pixelScaleX);
        const int viewportY = height - static_cast<int>(
            static_cast<float>(top + viewHeight) * pixelScaleY);
        const int viewportWidth = static_cast<int>(static_cast<float>(viewWidth) * pixelScaleX);
        const int viewportHeight = static_cast<int>(static_cast<float>(viewHeight) * pixelScaleY);
        if (viewportWidth <= 0 || viewportHeight <= 0) {
            return;
        }

        const float angle = elapsedSeconds * 0.45F;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        constexpr float tilt = -0.28F;
        const float tiltCosine = std::cos(tilt);
        const float tiltSine = std::sin(tilt);
        const std::array<float, 16> rotationY{
            cosine, 0.0F, -sine, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            sine, 0.0F, cosine, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F};
        const std::array<float, 16> rotationX{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, tiltCosine, tiltSine, 0.0F,
            0.0F, -tiltSine, tiltCosine, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F};
        const std::array<float, 16> translation{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            -center[0] * scale, -center[1] * scale, -center[2] * scale, 1.0F};
        auto modelTransform = multiply(rotationY, rotationX);
        modelTransform = multiply(modelTransform, translation);
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 3; ++row) {
                modelTransform[static_cast<std::size_t>(column * 4 + row)] *= scale;
            }
        }

        constexpr float nearPlane = 0.1F;
        constexpr float farPlane = 100.0F;
        constexpr float focalLength = 2.41421356F;
        const float aspect = static_cast<float>(viewportWidth) /
            static_cast<float>(viewportHeight);
        const std::array<float, 16> projection{
            focalLength / aspect, 0.0F, 0.0F, 0.0F,
            0.0F, focalLength, 0.0F, 0.0F,
            0.0F, 0.0F, (farPlane + nearPlane) / (nearPlane - farPlane), -1.0F,
            0.0F, 0.0F,
            (2.0F * farPlane * nearPlane) / (nearPlane - farPlane), 0.0F};
        const std::array<float, 16> view{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, -cameraDistance, 1.0F};
        const auto mvp = multiply(projection, multiply(view, modelTransform));

        glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(program);
        glUniformMatrix4fv(modelUniform, 1, GL_FALSE, modelTransform.data());
        glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, mvp.data());
        glBindVertexArray(vertexArray);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount),
                       GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
    }

    void drawSandbox(
        SDL_Window* window,
        const opena2::VehicleState& vehicle) const
    {
        int logicalWidth = 0;
        int logicalHeight = 0;
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &logicalWidth, &logicalHeight) ||
            !SDL_GetWindowSizeInPixels(window, &width, &height) ||
            logicalWidth <= 0 || logicalHeight <= 0 || width <= 0 || height <= 0) {
            return;
        }
        const float pixelScaleX = static_cast<float>(width) /
            static_cast<float>(logicalWidth);
        const float pixelScaleY = static_cast<float>(height) /
            static_cast<float>(logicalHeight);
        constexpr int left = 326;
        constexpr int top = 98;
        constexpr int margin = 18;
        const int viewWidth = logicalWidth - left - margin;
        const int viewHeight = logicalHeight - top - margin;
        const int viewportX = static_cast<int>(static_cast<float>(left) * pixelScaleX);
        const int viewportY = height - static_cast<int>(
            static_cast<float>(top + viewHeight) * pixelScaleY);
        const int viewportWidth = static_cast<int>(static_cast<float>(viewWidth) * pixelScaleX);
        const int viewportHeight = static_cast<int>(static_cast<float>(viewHeight) * pixelScaleY);
        if (viewportWidth <= 0 || viewportHeight <= 0) {
            return;
        }

        std::vector<ModelPreviewVertex> track;
        track.reserve(204);
        const auto addQuad = [&track](
            float x0, float x1, float z0, float z1, float y,
            std::array<float, 3> color) {
            const ModelPreviewVertex vertices[] = {
                {{x0, y, z0}, {0.0F, 1.0F, 0.0F}, {color[0], color[1], color[2]}},
                {{x1, y, z0}, {0.0F, 1.0F, 0.0F}, {color[0], color[1], color[2]}},
                {{x1, y, z1}, {0.0F, 1.0F, 0.0F}, {color[0], color[1], color[2]}},
                {{x0, y, z0}, {0.0F, 1.0F, 0.0F}, {color[0], color[1], color[2]}},
                {{x1, y, z1}, {0.0F, 1.0F, 0.0F}, {color[0], color[1], color[2]}},
                {{x0, y, z1}, {0.0F, 1.0F, 0.0F}, {color[0], color[1], color[2]}},
            };
            track.insert(track.end(), std::begin(vertices), std::end(vertices));
        };
        const float nearZ = vehicle.z - 180.0F;
        const float farZ = vehicle.z + 180.0F;
        addQuad(-140.0F, 140.0F, nearZ, farZ, -0.4F, {0.12F, 0.29F, 0.12F});
        addQuad(-17.0F, 17.0F, nearZ, farZ, -0.2F, {0.55F, 0.57F, 0.56F});
        addQuad(-16.0F, -15.2F, nearZ, farZ, -0.18F, {0.92F, 0.82F, 0.22F});
        addQuad(15.2F, 16.0F, nearZ, farZ, -0.18F, {0.92F, 0.82F, 0.22F});
        const float firstMarker = std::floor(nearZ / 18.0F) * 18.0F;
        for (float z = firstMarker; z < farZ; z += 18.0F) {
            addQuad(-0.18F, 0.18F, z, z + 8.0F, -0.16F, {0.87F, 0.88F, 0.82F});
        }
        if (track.size() > static_cast<std::size_t>(
                std::numeric_limits<GLsizeiptr>::max()) / sizeof(ModelPreviewVertex) ||
            track.size() > static_cast<std::size_t>(
                std::numeric_limits<GLsizei>::max())) {
            return;
        }

        const float forwardX = std::sin(vehicle.heading);
        const float forwardZ = -std::cos(vehicle.heading);
        const std::array<float, 3> eye{
            vehicle.x - forwardX * 42.0F,
            26.0F,
            vehicle.z - forwardZ * 42.0F};
        const std::array<float, 3> target{
            vehicle.x + forwardX * 12.0F,
            1.5F,
            vehicle.z + forwardZ * 12.0F};
        const auto dot = [](const auto& a, const auto& b) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        const auto normalize = [](std::array<float, 3> value) {
            const float length = std::sqrt(
                value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
            if (length > 0.0F) {
                for (auto& component : value) {
                    component /= length;
                }
            }
            return value;
        };
        const auto cross = [](const auto& a, const auto& b) {
            return std::array<float, 3>{
                a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
        };
        const auto forward = normalize(std::array<float, 3>{
            target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]});
        const auto side = normalize(cross(forward, std::array<float, 3>{0.0F, 1.0F, 0.0F}));
        const auto up = cross(side, forward);
        const std::array<float, 16> view{
            side[0], up[0], -forward[0], 0.0F,
            side[1], up[1], -forward[1], 0.0F,
            side[2], up[2], -forward[2], 0.0F,
            -dot(side, eye), -dot(up, eye), dot(forward, eye), 1.0F};
        constexpr float nearPlane = 0.5F;
        constexpr float farPlane = 500.0F;
        constexpr float focalLength = 2.41421356F;
        const float aspect = static_cast<float>(viewportWidth) /
            static_cast<float>(viewportHeight);
        const std::array<float, 16> projection{
            focalLength / aspect, 0.0F, 0.0F, 0.0F,
            0.0F, focalLength, 0.0F, 0.0F,
            0.0F, 0.0F, (farPlane + nearPlane) / (nearPlane - farPlane), -1.0F,
            0.0F, 0.0F,
            (2.0F * farPlane * nearPlane) / (nearPlane - farPlane), 0.0F};
        const auto mvp = multiply(projection, view);
        const std::array<float, 16> identity{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F};
        const float cosine = std::cos(vehicle.heading);
        const float sine = std::sin(vehicle.heading);
        const std::array<float, 16> vehicleTransform{
            cosine, 0.0F, sine, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            -sine, 0.0F, cosine, 0.0F,
            vehicle.x, 0.0F, vehicle.z, 1.0F};
        const std::array<float, 16> modelGrounding{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            -center[0], -groundOffset, -center[2], 1.0F};
        const auto vehicleModelTransform = multiply(vehicleTransform, modelGrounding);
        const auto vehicleMvp = multiply(mvp, vehicleModelTransform);

        glBindVertexArray(trackVertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, trackVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(track.size() * sizeof(ModelPreviewVertex)),
                     track.data(),
                     GL_STREAM_DRAW);
        glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(program);
        glUniformMatrix4fv(modelUniform, 1, GL_FALSE, identity.data());
        glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, mvp.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(track.size()));
        glUniformMatrix4fv(modelUniform, 1, GL_FALSE, vehicleTransform.data());
        glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, vehicleMvp.data());
        glBindVertexArray(vertexArray);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount),
                       GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
    }

    void destroy()
    {
        if (trackVertexBuffer != 0) {
            glDeleteBuffers(1, &trackVertexBuffer);
            trackVertexBuffer = 0;
        }
        if (trackVertexArray != 0) {
            glDeleteVertexArrays(1, &trackVertexArray);
            trackVertexArray = 0;
        }
        if (indexBuffer != 0) {
            glDeleteBuffers(1, &indexBuffer);
            indexBuffer = 0;
        }
        if (vertexBuffer != 0) {
            glDeleteBuffers(1, &vertexBuffer);
            vertexBuffer = 0;
        }
        if (vertexArray != 0) {
            glDeleteVertexArrays(1, &vertexArray);
            vertexArray = 0;
        }
        if (program != 0) {
            glDeleteProgram(program);
            program = 0;
        }
    }
};

struct UiVertex {
    GLfloat x;
    GLfloat y;
    GLfloat red;
    GLfloat green;
    GLfloat blue;
};

struct AssetBrowserUi {
    static constexpr int sidebarWidth = 310;
    static constexpr int rowHeight = 23;
    static constexpr int categoryTop = 61;
    static constexpr int assetTop = 239;

    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;

    static std::array<std::uint8_t, 7> glyph(char character)
    {
        switch (character) {
        case 'A': return {14, 17, 17, 31, 17, 17, 17};
        case 'B': return {30, 17, 17, 30, 17, 17, 30};
        case 'C': return {14, 17, 16, 16, 16, 17, 14};
        case 'D': return {30, 17, 17, 17, 17, 17, 30};
        case 'E': return {31, 16, 16, 30, 16, 16, 31};
        case 'F': return {31, 16, 16, 30, 16, 16, 16};
        case 'G': return {14, 17, 16, 23, 17, 17, 15};
        case 'H': return {17, 17, 17, 31, 17, 17, 17};
        case 'I': return {14, 4, 4, 4, 4, 4, 14};
        case 'J': return {7, 2, 2, 2, 2, 18, 12};
        case 'K': return {17, 18, 20, 24, 20, 18, 17};
        case 'L': return {16, 16, 16, 16, 16, 16, 31};
        case 'M': return {17, 27, 21, 21, 17, 17, 17};
        case 'N': return {17, 25, 21, 19, 17, 17, 17};
        case 'O': return {14, 17, 17, 17, 17, 17, 14};
        case 'P': return {30, 17, 17, 30, 16, 16, 16};
        case 'Q': return {14, 17, 17, 17, 21, 18, 13};
        case 'R': return {30, 17, 17, 30, 20, 18, 17};
        case 'S': return {15, 16, 16, 14, 1, 1, 30};
        case 'T': return {31, 4, 4, 4, 4, 4, 4};
        case 'U': return {17, 17, 17, 17, 17, 17, 14};
        case 'V': return {17, 17, 17, 17, 17, 10, 4};
        case 'W': return {17, 17, 17, 21, 21, 21, 10};
        case 'X': return {17, 17, 10, 4, 10, 17, 17};
        case 'Y': return {17, 17, 10, 4, 4, 4, 4};
        case 'Z': return {31, 1, 2, 4, 8, 16, 31};
        case '0': return {14, 17, 19, 21, 25, 17, 14};
        case '1': return {4, 12, 4, 4, 4, 4, 14};
        case '2': return {14, 17, 1, 2, 4, 8, 31};
        case '3': return {30, 1, 1, 14, 1, 1, 30};
        case '4': return {2, 6, 10, 18, 31, 2, 2};
        case '5': return {31, 16, 16, 30, 1, 1, 30};
        case '6': return {14, 16, 16, 30, 17, 17, 14};
        case '7': return {31, 1, 2, 4, 8, 8, 8};
        case '8': return {14, 17, 17, 14, 17, 17, 14};
        case '9': return {14, 17, 17, 15, 1, 1, 14};
        case '.': return {0, 0, 0, 0, 0, 12, 12};
        case ',': return {0, 0, 0, 0, 4, 4, 8};
        case ':': return {0, 12, 12, 0, 12, 12, 0};
        case '-': return {0, 0, 0, 31, 0, 0, 0};
        case '_': return {0, 0, 0, 0, 0, 0, 31};
        case '/': return {1, 2, 2, 4, 8, 8, 16};
        case '(': return {2, 4, 8, 8, 8, 4, 2};
        case ')': return {8, 4, 2, 2, 2, 4, 8};
        case '+': return {0, 4, 4, 31, 4, 4, 0};
        default: return {};
        }
    }

    void addRect(
        std::vector<UiVertex>& vertices,
        float x,
        float y,
        float width,
        float height,
        int screenWidth,
        int screenHeight,
        std::array<float, 3> color) const
    {
        const float left = 2.0F * x / static_cast<float>(screenWidth) - 1.0F;
        const float right = 2.0F * (x + width) / static_cast<float>(screenWidth) - 1.0F;
        const float top = 1.0F - 2.0F * y / static_cast<float>(screenHeight);
        const float bottom = 1.0F - 2.0F * (y + height) / static_cast<float>(screenHeight);
        const UiVertex corners[] = {
            {left, top, color[0], color[1], color[2]},
            {left, bottom, color[0], color[1], color[2]},
            {right, bottom, color[0], color[1], color[2]},
            {left, top, color[0], color[1], color[2]},
            {right, bottom, color[0], color[1], color[2]},
            {right, top, color[0], color[1], color[2]},
        };
        vertices.insert(vertices.end(), std::begin(corners), std::end(corners));
    }

    void addText(
        std::vector<UiVertex>& vertices,
        std::string_view text,
        int x,
        int y,
        int scale,
        int maxWidth,
        int screenWidth,
        int screenHeight,
        std::array<float, 3> color) const
    {
        int cursorX = x;
        for (char character : text) {
            if (cursorX + 5 * scale > x + maxWidth) {
                break;
            }
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - 'a' + 'A');
            }
            const auto rows = glyph(character);
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if ((rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) != 0) {
                        addRect(vertices,
                                static_cast<float>(cursorX + column * scale),
                                static_cast<float>(y + row * scale),
                                static_cast<float>(scale),
                                static_cast<float>(scale),
                                screenWidth,
                                screenHeight,
                                color);
                    }
                }
            }
            cursorX += 6 * scale;
        }
    }

    bool initialize(std::string& error)
    {
        constexpr const char* vertexSource = R"glsl(#version 150 core
in vec2 position;
in vec3 vertexColor;
out vec3 color;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    color = vertexColor;
}
)glsl";
        constexpr const char* fragmentSource = R"glsl(#version 150 core
in vec3 color;
out vec4 outputColor;
void main()
{
    outputColor = vec4(color, 1.0);
}
)glsl";
        const auto compile = [&error](GLenum type, const char* source) -> GLuint {
            const GLuint shader = glCreateShader(type);
            if (shader == 0) {
                error = "Could not create an OpenGL UI shader.";
                return 0;
            }
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_FALSE) {
                GLint length = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
                glGetShaderInfoLog(shader, length, nullptr, log.data());
                error = "Could not compile OpenGL UI shader: " + log;
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        };
        const GLuint vertexShader = compile(GL_VERTEX_SHADER, vertexSource);
        if (vertexShader == 0) {
            return false;
        }
        const GLuint fragmentShader = compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            return false;
        }
        program = glCreateProgram();
        if (program == 0) {
            error = "Could not create an OpenGL UI program.";
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return false;
        }
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glBindAttribLocation(program, 0, "position");
        glBindAttribLocation(program, 1, "vertexColor");
        glLinkProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            error = "Could not link OpenGL UI program: " + log;
            return false;
        }

        glGenVertexArrays(1, &vertexArray);
        glGenBuffers(1, &vertexBuffer);
        if (vertexArray == 0 || vertexBuffer == 0) {
            error = "Could not create OpenGL UI geometry buffers.";
            return false;
        }
        glBindVertexArray(vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
            reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        glBindVertexArray(0);
        return true;
    }

    void destroy()
    {
        if (vertexBuffer != 0) {
            glDeleteBuffers(1, &vertexBuffer);
            vertexBuffer = 0;
        }
        if (vertexArray != 0) {
            glDeleteVertexArrays(1, &vertexArray);
            vertexArray = 0;
        }
        if (program != 0) {
            glDeleteProgram(program);
            program = 0;
        }
    }

    void drawSandboxHud(SDL_Window* window, const opena2::VehicleState& vehicle) const
    {
        int width = 0;
        int height = 0;
        int pixelWidth = 0;
        int pixelHeight = 0;
        if (!SDL_GetWindowSize(window, &width, &height) ||
            !SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight) ||
            width <= 0 || height <= 0 || pixelWidth <= 0 || pixelHeight <= 0) {
            return;
        }
        std::vector<UiVertex> vertices;
        vertices.reserve(16000);
        constexpr std::array<float, 3> background{0.055F, 0.075F, 0.11F};
        constexpr std::array<float, 3> foreground{0.9F, 0.94F, 0.98F};
        constexpr std::array<float, 3> subdued{0.62F, 0.7F, 0.79F};
        addRect(vertices, 0, 0, static_cast<float>(width), 39, width, height, background);
        addText(vertices, "OPENA2 DRIVING SANDBOX", 14, 14, 2, width - 28,
                width, height, foreground);
        const int speedKph = static_cast<int>(std::lround(std::abs(vehicle.speed) * 3.6F));
        std::string speed = "SPEED ";
        if (speedKph < 100) {
            speed.push_back('0');
        }
        if (speedKph < 10) {
            speed.push_back('0');
        }
        speed += std::to_string(speedKph);
        speed += " KM/H";
        addText(vertices, speed, width - 176, 15, 1, 164, width, height, foreground);
        addRect(vertices, 0, static_cast<float>(height - 36),
                static_cast<float>(width), 36, width, height, background);
        addText(vertices, "W ACCEL   S BRAKE   A D STEER   R RESET   G BROWSER",
                14, height - 24, 1, width - 28, width, height, subdued);

        std::vector<GLfloat> packed;
        packed.reserve(vertices.size() * 5);
        for (const auto& vertex : vertices) {
            packed.push_back(vertex.x);
            packed.push_back(vertex.y);
            packed.push_back(vertex.red);
            packed.push_back(vertex.green);
            packed.push_back(vertex.blue);
        }
        glViewport(0, 0, pixelWidth, pixelHeight);
        glUseProgram(program);
        glBindVertexArray(vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(packed.size() * sizeof(GLfloat)),
                     packed.data(),
                     GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        glBindVertexArray(0);
        glUseProgram(0);
    }

    template<typename GameData>
    void draw(
        SDL_Window* window,
        const GameData& gameData) const
    {
        int width = 0;
        int height = 0;
        int pixelWidth = 0;
        int pixelHeight = 0;
        if (!SDL_GetWindowSize(window, &width, &height) ||
            !SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight) ||
            width <= 0 || height <= 0 || pixelWidth <= 0 || pixelHeight <= 0) {
            return;
        }
        std::vector<UiVertex> vertices;
        vertices.reserve(36000);
        constexpr std::array<float, 3> panel{0.055F, 0.075F, 0.11F};
        constexpr std::array<float, 3> row{0.10F, 0.13F, 0.18F};
        constexpr std::array<float, 3> selected{0.13F, 0.34F, 0.49F};
        constexpr std::array<float, 3> activeCategory{0.10F, 0.25F, 0.32F};
        constexpr std::array<float, 3> foreground{0.88F, 0.92F, 0.97F};
        constexpr std::array<float, 3> subdued{0.48F, 0.57F, 0.68F};
        addRect(vertices, 0, 0, static_cast<float>(sidebarWidth), static_cast<float>(height),
                width, height, panel);
        addRect(vertices, 0, 0, static_cast<float>(width), 39, width, height,
                {0.075F, 0.10F, 0.15F});
        addText(vertices, "OPENA2 ASSET BROWSER", 14, 14, 2, width - 28,
                width, height, foreground);
        addText(vertices, "CATEGORIES", 14, 48, 1, sidebarWidth - 28,
                width, height, subdued);

        for (std::size_t i = 0; i < categoryCount; ++i) {
            const int y = categoryTop + static_cast<int>(i) * 25;
            const auto color = i == gameData.currentCategory ? activeCategory : panel;
            addRect(vertices, 8, static_cast<float>(y), sidebarWidth - 16, 22,
                    width, height, color);
            const std::string label = std::string(gameData.categoryName(i)) + "  " +
                std::to_string(gameData.entryCount(i));
            addText(vertices, label, 15, y + 7, 1, sidebarWidth - 30,
                    width, height, i == gameData.currentCategory ? foreground : subdued);
        }
        addText(vertices, "ASSETS", 14, 220, 1, sidebarWidth - 28,
                width, height, subdued);
        addRect(vertices, 8, 233, sidebarWidth - 16, 1, width, height,
                {0.20F, 0.25F, 0.33F});

        const int availableRows = std::max(0, (height - assetTop - 30) / rowHeight);
        const auto selectedIndex = gameData.selectedIndices[gameData.currentCategory];
        const auto firstRow = selectedIndex >= static_cast<std::size_t>(availableRows)
            ? selectedIndex - static_cast<std::size_t>(availableRows) + 1
            : 0;
        const auto listCount = gameData.entryCount(gameData.currentCategory);
        for (int rowIndex = 0; rowIndex < availableRows; ++rowIndex) {
            const auto assetIndex = firstRow + static_cast<std::size_t>(rowIndex);
            if (assetIndex >= listCount) {
                break;
            }
            const int y = assetTop + rowIndex * rowHeight;
            const auto background = assetIndex == selectedIndex ? selected : row;
            addRect(vertices, 8, static_cast<float>(y), sidebarWidth - 16, 21,
                    width, height, background);
            std::string label;
            if (const auto name = gameData.entryName(gameData.currentCategory, assetIndex)) {
                if (gameData.currentCategory == static_cast<std::size_t>(AssetCategory::audio)) {
                    const auto& resource = gameData.waveResources[assetIndex];
                    label = resource.fromModelArchive ? "RCS/" : "A2MRCS/";
                }
                label.append(*name);
            } else {
                label = "ENTRY " + std::to_string(assetIndex + 1);
            }
            addText(vertices, label, 14, y + 7, 1, sidebarWidth - 28,
                    width, height, foreground);
        }

        const int detailX = sidebarWidth + 16;
        const int detailY = 52;
        const int detailWidth = std::max(0, width - detailX - 18);
        addText(vertices, gameData.categoryName(gameData.currentCategory),
                detailX, detailY, 2, detailWidth, width, height, foreground);
        const auto selectedName = gameData.entryName(gameData.currentCategory, selectedIndex);
        if (selectedName) {
            addText(vertices, *selectedName, detailX, detailY + 24, 1,
                    detailWidth, width, height, subdued);
        }

        if (gameData.currentCategory == static_cast<std::size_t>(AssetCategory::audio)) {
            const int messageY = height / 2;
            addText(vertices, "WAV AUDIO PREVIEW",
                    detailX + 18, messageY, 2, detailWidth - 36, width, height, foreground);
            addText(vertices, "PRESS SPACE TO PLAY SELECTED TRACK",
                    detailX + 18, messageY + 30, 1, detailWidth - 36,
                    width, height, subdued);
        } else if (gameData.currentCategory == static_cast<std::size_t>(AssetCategory::models)) {
            if (gameData.currentModel) {
                addText(vertices, "3D MODEL PREVIEW",
                        detailX + 18, height / 2 - 17, 2,
                        detailWidth - 36, width, height, foreground);
                const std::string geometry =
                    std::to_string(gameData.currentModel->vertices.size()) +
                    " VERTICES  " +
                    std::to_string(gameData.currentModel->faces.size()) + " FACES";
                addText(vertices, geometry, detailX + 18, height / 2 + 16, 1,
                        detailWidth - 36, width, height, subdued);
            }
        }

        addRect(vertices, sidebarWidth + 8, static_cast<float>(height - 25),
                static_cast<float>(width - sidebarWidth - 16), 1,
                width, height, {0.18F, 0.22F, 0.29F});
        addText(vertices, "TAB CATEGORY   UP DOWN SELECT   SPACE PLAY WAV   ESC QUIT",
                sidebarWidth + 14, height - 17, 1, width - sidebarWidth - 28,
                width, height, subdued);

        std::vector<GLfloat> packed;
        packed.reserve(vertices.size() * 5);
        for (const auto& vertex : vertices) {
            packed.push_back(vertex.x);
            packed.push_back(vertex.y);
            packed.push_back(vertex.red);
            packed.push_back(vertex.green);
            packed.push_back(vertex.blue);
        }
        glViewport(0, 0, pixelWidth, pixelHeight);
        glUseProgram(program);
        glBindVertexArray(vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(packed.size() * sizeof(GLfloat)),
                     packed.data(),
                     GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        glBindVertexArray(0);
        glUseProgram(0);
    }
};

struct AudioPreview {
    SDL_AudioStream* stream = nullptr;

    void stop()
    {
        if (stream != nullptr) {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
    }

    bool play(const opena2::PcmWave& wave, std::string& error)
    {
        stop();
        if (wave.samples.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            error = "WAVE resource is too large for the audio stream.";
            return false;
        }

        SDL_AudioSpec format{
            wave.format == opena2::PcmSampleFormat::unsigned8
                ? SDL_AUDIO_U8
                : SDL_AUDIO_S16LE,
            static_cast<int>(wave.channels),
            static_cast<int>(wave.sampleRate)};
        stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, nullptr, nullptr);
        if (stream == nullptr) {
            error = "Could not open audio output: " + std::string(SDL_GetError());
            return false;
        }

        if (!SDL_PutAudioStreamData(
                stream, wave.samples.data(), static_cast<int>(wave.samples.size()))) {
            error = "Could not queue WAVE samples: " + std::string(SDL_GetError());
            stop();
            return false;
        }
        if (!SDL_ResumeAudioStreamDevice(stream)) {
            error = "Could not start audio output: " + std::string(SDL_GetError());
            stop();
            return false;
        }
        return true;
    }
};

struct FolderResult {
    bool completed = false;
    bool cancelled = false;
    std::filesystem::path path;
    std::string error;
};

struct FolderSelection {
    std::mutex mutex;
    FolderResult result;
};

struct WaveResource {
    bool fromModelArchive;
    std::size_t entryIndex;
};

struct LoadedGameData {
    opena2::GameDataDirectory directory;
    opena2::IndexedArchive gameSprites;
    opena2::IndexedArchive mrcImages;
    opena2::IndexedArchive resourceImages;
    opena2::IndexedArchive modelResources;
    std::vector<std::size_t> pcxResourceIndices;
    std::vector<std::size_t> rcsPcxResourceIndices;
    std::vector<std::size_t> modelResourceIndices;
    std::vector<WaveResource> waveResources;
    opena2::IndexedImage currentSprite;
    std::optional<opena2::D3DMesh> currentModel;
    std::optional<opena2::D3DMesh> sandboxCar;
    std::array<std::size_t, categoryCount> selectedIndices{};
    std::size_t currentCategory = static_cast<std::size_t>(AssetCategory::a2mrcsPcx);

    const opena2::IndexedArchive& archiveForCategory(std::size_t category) const
    {
        switch (category) {
        case static_cast<std::size_t>(AssetCategory::mrcImages): return mrcImages;
        case static_cast<std::size_t>(AssetCategory::a2mrcsPcx): return resourceImages;
        case static_cast<std::size_t>(AssetCategory::rcsPcx):
        case static_cast<std::size_t>(AssetCategory::models): return modelResources;
        default: return gameSprites;
        }
    }

    std::size_t entryCount(std::size_t category) const
    {
        switch (static_cast<AssetCategory>(category)) {
        case AssetCategory::a2mrcsPcx:
            return pcxResourceIndices.size();
        case AssetCategory::rcsPcx:
            return rcsPcxResourceIndices.size();
        case AssetCategory::audio:
            return waveResources.size();
        case AssetCategory::models:
            return modelResourceIndices.size();
        case AssetCategory::gameSprites:
        case AssetCategory::mrcImages:
        case AssetCategory::count:
            return archiveForCategory(category).entryCount();
        }
        return 0;
    }

    std::optional<std::string_view> entryName(std::size_t category, std::size_t index) const
    {
        switch (static_cast<AssetCategory>(category)) {
        case AssetCategory::a2mrcsPcx:
            return index < pcxResourceIndices.size()
                ? resourceImages.entryName(pcxResourceIndices[index])
                : std::nullopt;
        case AssetCategory::rcsPcx:
            return index < rcsPcxResourceIndices.size()
                ? modelResources.entryName(rcsPcxResourceIndices[index])
                : std::nullopt;
        case AssetCategory::audio: {
            if (index >= waveResources.size()) {
                return std::nullopt;
            }
            const auto& resource = waveResources[index];
            return (resource.fromModelArchive ? modelResources : resourceImages)
                .entryName(resource.entryIndex);
        }
        case AssetCategory::models:
            return index < modelResourceIndices.size()
                ? modelResources.entryName(modelResourceIndices[index])
                : std::nullopt;
        case AssetCategory::gameSprites:
        case AssetCategory::mrcImages:
        case AssetCategory::count:
            return archiveForCategory(category).entryName(index);
        }
        return std::nullopt;
    }

    const char* categoryName(std::size_t category) const
    {
        switch (static_cast<AssetCategory>(category)) {
        case AssetCategory::gameSprites: return "GAMESPR IMAGES";
        case AssetCategory::mrcImages: return "A2MRC IMAGES";
        case AssetCategory::a2mrcsPcx: return "A2MRCS PCX";
        case AssetCategory::rcsPcx: return "RCS PCX";
        case AssetCategory::audio: return "WAV AUDIO";
        case AssetCategory::models: return "RCS D3D MODELS";
        case AssetCategory::count: return "UNKNOWN";
        }
        return "UNKNOWN";
    }
};

void SDLCALL onFolderSelected(void* userdata, const char* const* filelist, int)
{
    auto& selection = *static_cast<FolderSelection*>(userdata);
    std::lock_guard lock(selection.mutex);
    selection.result.completed = true;

    if (filelist == nullptr) {
        const char* error = SDL_GetError();
        selection.result.error = error[0] == '\0' ? "The folder picker failed." : error;
    } else if (filelist[0] == nullptr) {
        selection.result.cancelled = true;
    } else {
        std::u8string utf8Path;
        const auto* byte = reinterpret_cast<const unsigned char*>(filelist[0]);
        while (*byte != '\0') {
            utf8Path.push_back(static_cast<char8_t>(*byte));
            ++byte;
        }
        selection.result.path = std::filesystem::path(std::move(utf8Path));
    }
}

void requestFolder(SDL_Window* window, FolderSelection& selection)
{
    {
        std::lock_guard lock(selection.mutex);
        selection.result = {};
    }

    SDL_ClearError();
    SDL_ShowOpenFolderDialog(onFolderSelected, &selection, window, nullptr, false);
}

std::optional<LoadedGameData> loadGameData(
    const std::filesystem::path& path,
    std::string& error)
{
    auto directory = opena2::GameDataDirectory::open(path, error);
    if (!directory) {
        return std::nullopt;
    }

    auto gameSprites = opena2::IndexedArchive::open(
        *directory, "GAMESPR.IMG", "GAMESPR.IND", error);
    if (!gameSprites) {
        return std::nullopt;
    }

    auto mrcImages = opena2::IndexedArchive::open(
        *directory, "A2MRC.IMG", "A2MRC.IND", error);
    if (!mrcImages) {
        return std::nullopt;
    }

    auto resourceImages = opena2::IndexedArchive::openNamed(
        *directory, "A2MRCS.IMG", "A2MRCS.IND", error);
    if (!resourceImages) {
        return std::nullopt;
    }
    std::vector<std::size_t> pcxResourceIndices;
    std::vector<std::size_t> rcsPcxResourceIndices;
    std::vector<std::size_t> modelResourceIndices;
    std::vector<WaveResource> waveResources;
    for (std::size_t i = 0; i < resourceImages->entryCount(); ++i) {
        const auto name = resourceImages->entryName(i);
        if (!name || name->size() < 4) {
            continue;
        }
        const auto extension = name->substr(name->size() - 4);
        if (std::toupper(static_cast<unsigned char>(extension[0])) == '.' &&
            std::toupper(static_cast<unsigned char>(extension[1])) == 'P' &&
            std::toupper(static_cast<unsigned char>(extension[2])) == 'C' &&
            std::toupper(static_cast<unsigned char>(extension[3])) == 'X') {
            pcxResourceIndices.push_back(i);
        } else if (std::toupper(static_cast<unsigned char>(extension[0])) == '.' &&
                   std::toupper(static_cast<unsigned char>(extension[1])) == 'W' &&
                   std::toupper(static_cast<unsigned char>(extension[2])) == 'A' &&
                   std::toupper(static_cast<unsigned char>(extension[3])) == 'V') {
            waveResources.push_back({false, i});
        }
    }
    if (pcxResourceIndices.empty()) {
        error = "A2MRCS archive does not contain any PCX images.";
        return std::nullopt;
    }

    auto modelResources = opena2::IndexedArchive::openNamed(
        *directory, "RCS.IMG", "RCS.IND", error);
    if (!modelResources) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < modelResources->entryCount(); ++i) {
        const auto name = modelResources->entryName(i);
        if (name && name->size() >= 4) {
            const auto extension = name->substr(name->size() - 4);
            if (std::toupper(static_cast<unsigned char>(extension[0])) == '.' &&
                std::toupper(static_cast<unsigned char>(extension[1])) == 'W' &&
                std::toupper(static_cast<unsigned char>(extension[2])) == 'A' &&
                std::toupper(static_cast<unsigned char>(extension[3])) == 'V') {
                waveResources.push_back({true, i});
            } else if (std::toupper(static_cast<unsigned char>(extension[0])) == '.' &&
                       std::toupper(static_cast<unsigned char>(extension[1])) == 'P' &&
                       std::toupper(static_cast<unsigned char>(extension[2])) == 'C' &&
                       std::toupper(static_cast<unsigned char>(extension[3])) == 'X') {
                rcsPcxResourceIndices.push_back(i);
            } else if (std::toupper(static_cast<unsigned char>(extension[0])) == '.' &&
                       std::toupper(static_cast<unsigned char>(extension[1])) == 'D' &&
                       std::toupper(static_cast<unsigned char>(extension[2])) == '3' &&
                       std::toupper(static_cast<unsigned char>(extension[3])) == 'D') {
                modelResourceIndices.push_back(i);
            }
        }
    }

    std::optional<std::size_t> sandboxCarIndex;
    for (std::size_t index = 0; index < modelResourceIndices.size(); ++index) {
        const auto name = modelResources->entryName(modelResourceIndices[index]);
        if (!name) {
            continue;
        }
        std::string normalizedName(*name);
        std::transform(normalizedName.begin(), normalizedName.end(),
                       normalizedName.begin(), [](unsigned char character) {
                           return static_cast<char>(std::toupper(character));
                       });
        if (normalizedName == "PORCHE.D3D") {
            sandboxCarIndex = index;
            break;
        }
    }
    if (!sandboxCarIndex && !modelResourceIndices.empty()) {
        sandboxCarIndex = 0;
    }
    std::optional<opena2::D3DMesh> sandboxCar;
    if (sandboxCarIndex) {
        auto bytes = modelResources->readEntry(
            modelResourceIndices[*sandboxCarIndex], error);
        if (!bytes) {
            return std::nullopt;
        }
        sandboxCar = opena2::decodeD3DMesh(*bytes, error);
        if (!sandboxCar || sandboxCar->vertices.empty() || sandboxCar->faces.empty()) {
            if (error.empty()) {
                error = "Could not decode a car model for the driving sandbox.";
            }
            return std::nullopt;
        }
    }

    auto firstSpriteData = gameSprites->readEntry(0, error);
    if (!firstSpriteData) {
        return std::nullopt;
    }

    auto firstSprite = opena2::decodeIndexedImage(*firstSpriteData, error);
    if (!firstSprite) {
        return std::nullopt;
    }

    return LoadedGameData{
        std::move(*directory),
        std::move(*gameSprites),
        std::move(*mrcImages),
        std::move(*resourceImages),
        std::move(*modelResources),
        std::move(pcxResourceIndices),
        std::move(rcsPcxResourceIndices),
        std::move(modelResourceIndices),
        std::move(waveResources),
        std::move(*firstSprite),
        {},
        std::move(sandboxCar),
        {},
        static_cast<std::size_t>(AssetCategory::a2mrcsPcx)};
}

std::optional<opena2::IndexedImage> loadArchiveImage(
    const LoadedGameData& gameData,
    std::size_t category,
    std::size_t entryIndex,
    std::string& error)
{
    const auto& archive = gameData.archiveForCategory(category);
    const auto actualEntryIndex = category ==
            static_cast<std::size_t>(AssetCategory::a2mrcsPcx)
        ? gameData.pcxResourceIndices.at(entryIndex)
        : category == static_cast<std::size_t>(AssetCategory::rcsPcx)
            ? gameData.rcsPcxResourceIndices.at(entryIndex)
            : category == static_cast<std::size_t>(AssetCategory::models)
                ? gameData.modelResourceIndices.at(entryIndex)
                : entryIndex;
    auto entry = archive.readEntry(actualEntryIndex, error);
    if (!entry) {
        return std::nullopt;
    }

    if (category == static_cast<std::size_t>(AssetCategory::a2mrcsPcx) ||
        category == static_cast<std::size_t>(AssetCategory::rcsPcx)) {
        return opena2::decodePcx(*entry, error);
    }
    if (category == static_cast<std::size_t>(AssetCategory::gameSprites) ||
        category == static_cast<std::size_t>(AssetCategory::mrcImages)) {
        return opena2::decodeIndexedImage(*entry, error);
    }
    error = "The selected category does not contain a previewable image.";
    return std::nullopt;
}

std::optional<opena2::PcmWave> loadWave(
    const LoadedGameData& gameData,
    std::size_t selectionIndex,
    std::string& error)
{
    if (selectionIndex >= gameData.waveResources.size()) {
        error = "WAVE selection is out of range.";
        return std::nullopt;
    }
    const auto& resource = gameData.waveResources[selectionIndex];
    const auto& archive = resource.fromModelArchive
        ? gameData.modelResources
        : gameData.resourceImages;
    auto entry = archive.readEntry(resource.entryIndex, error);
    if (!entry) {
        return std::nullopt;
    }
    return opena2::decodePcmWave(*entry, error);
}

std::optional<opena2::D3DMesh> loadModel(
    const LoadedGameData& gameData,
    std::size_t selectionIndex,
    std::string& error)
{
    if (selectionIndex >= gameData.modelResourceIndices.size()) {
        error = "D3D model selection is out of range.";
        return std::nullopt;
    }
    auto entry = gameData.modelResources.readEntry(
        gameData.modelResourceIndices[selectionIndex], error);
    if (!entry) {
        return std::nullopt;
    }
    auto mesh = opena2::decodeD3DMesh(*entry, error);
    if (mesh.vertices.empty() || mesh.faces.empty()) {
        return std::nullopt;
    }
    return mesh;
}

std::optional<LoadedGameData> finishGameDataLoad(
    const std::filesystem::path& path,
    std::string& error)
{
    auto gameData = loadGameData(path, error);
    if (!gameData) {
        return std::nullopt;
    }

    auto firstResource = loadArchiveImage(
        *gameData, static_cast<std::size_t>(AssetCategory::a2mrcsPcx), 0, error);
    if (!firstResource) {
        return std::nullopt;
    }

    gameData->currentSprite = std::move(*firstResource);
    gameData->selectedIndices[static_cast<std::size_t>(AssetCategory::a2mrcsPcx)] = 0;
    return gameData;
}

std::string viewerTitle(const LoadedGameData& gameData)
{
    std::string title = "OpenA2Racer - ";
    const auto category = gameData.currentCategory;
    const auto index = gameData.selectedIndices[category];
    title += gameData.categoryName(category);
    title += " ";
    title += std::to_string(index + 1);
    title += "/";
    title += std::to_string(gameData.entryCount(category));
    if (const auto name = gameData.entryName(category, index)) {
        title += " ";
        title += *name;
    }
    return title;
}

} // namespace

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialize SDL: %s", SDL_GetError());
        return 1;
    }
    const bool audioInitialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!audioInitialized) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Audio preview is unavailable: %s",
                    SDL_GetError());
    }

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not configure OpenGL: %s",
                     SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "OpenA2Racer - Select original CD-ROM folder",
        960,
        540,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not create OpenGL context: %s",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    FolderSelection selection;
    std::string glError;
    if (!glFunctions.load(glError)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", glError.c_str());
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    AssetBrowserUi browserUi;
    if (!browserUi.initialize(glError)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", glError.c_str());
        browserUi.destroy();
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    D3DModelRenderer modelRenderer;
    if (!modelRenderer.initialize(glError)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", glError.c_str());
        modelRenderer.destroy();
        browserUi.destroy();
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    bool dialogPending = true;
    bool closeRequested = false;
    requestFolder(window, selection);

    bool running = true;
    int exitCode = 0;
    std::optional<LoadedGameData> gameData;
    SpriteRenderer renderer;
    AudioPreview audioPreview;
    opena2::VehicleState vehicle;
    bool sandboxMode = false;
    const Uint64 performanceFrequency = SDL_GetPerformanceFrequency();
    Uint64 previousFrameCounter = SDL_GetPerformanceCounter();

    const auto selectCategory = [&](std::size_t category) {
        if (!gameData || category >= categoryCount ||
            gameData->entryCount(category) == 0) {
            return false;
        }
        const auto count = gameData->entryCount(category);
        auto selectedIndex = gameData->selectedIndices[category];
        if (selectedIndex >= count) {
            selectedIndex = 0;
        }
        std::string imageError;
        if (category <= static_cast<std::size_t>(AssetCategory::rcsPcx)) {
            auto image = loadArchiveImage(*gameData, category, selectedIndex, imageError);
            if (!image || !renderer.updateImage(*image, imageError)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not display category %s: %s",
                             gameData->categoryName(category),
                             imageError.c_str());
                return false;
            }
            gameData->currentSprite = std::move(*image);
            gameData->currentModel.reset();
        } else if (category == static_cast<std::size_t>(AssetCategory::models)) {
            auto model = loadModel(*gameData, selectedIndex, imageError);
            if (!model || !modelRenderer.updateMesh(*model, imageError)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not display model category: %s",
                             imageError.c_str());
                return false;
            }
            gameData->currentModel = std::move(*model);
        }
        if (gameData->currentCategory ==
            static_cast<std::size_t>(AssetCategory::audio)) {
            audioPreview.stop();
        }
        gameData->selectedIndices[category] = selectedIndex;
        gameData->currentCategory = category;
        const std::string title = viewerTitle(*gameData);
        SDL_SetWindowTitle(window, title.c_str());
        return true;
    };

    const auto selectAsset = [&](std::size_t index) {
        if (!gameData || index >= gameData->entryCount(gameData->currentCategory)) {
            return false;
        }
        const auto category = gameData->currentCategory;
        if (category == static_cast<std::size_t>(AssetCategory::audio) &&
            index != gameData->selectedIndices[category]) {
            audioPreview.stop();
        }
        std::string imageError;
        if (category <= static_cast<std::size_t>(AssetCategory::rcsPcx)) {
            auto image = loadArchiveImage(*gameData, category, index, imageError);
            if (!image || !renderer.updateImage(*image, imageError)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not preview selected asset: %s",
                             imageError.c_str());
                return false;
            }
            gameData->currentSprite = std::move(*image);
            gameData->currentModel.reset();
        } else if (category == static_cast<std::size_t>(AssetCategory::models)) {
            auto model = loadModel(*gameData, index, imageError);
            if (!model || !modelRenderer.updateMesh(*model, imageError)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not preview selected model: %s",
                             imageError.c_str());
                return false;
            }
            gameData->currentModel = std::move(*model);
        }
        gameData->selectedIndices[category] = index;
        const std::string title = viewerTitle(*gameData);
        SDL_SetWindowTitle(window, title.c_str());
        return true;
    };

    const auto startDrivingDemo = [&]() {
        if (!gameData || !gameData->sandboxCar) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "No car model is available for the driving demo.");
            return false;
        }
        std::string modelError;
        if (!modelRenderer.updateMesh(*gameData->sandboxCar, modelError)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not load the driving-demo car: %s",
                         modelError.c_str());
            return false;
        }
        audioPreview.stop();
        vehicle = {};
        sandboxMode = true;
        SDL_SetWindowTitle(window, "OpenA2Racer - Driving Sandbox");
        return true;
    };

    const auto moveAssetSelection = [&](int direction) {
        if (!gameData) {
            return;
        }
        const auto category = gameData->currentCategory;
        const auto count = gameData->entryCount(category);
        if (count == 0) {
            return;
        }
        const auto current = gameData->selectedIndices[category];
        const auto next = direction > 0
            ? (current + 1) % count
            : (current + count - 1) % count;
        selectAsset(next);
    };

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                if (dialogPending) {
                    closeRequested = true;
                } else {
                    running = false;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat &&
                       event.key.key == SDLK_ESCAPE) {
                if (dialogPending) {
                    closeRequested = true;
                } else {
                    running = false;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat &&
                       !dialogPending &&
                       gameData &&
                       event.key.key == SDLK_G) {
                if (sandboxMode) {
                    sandboxMode = false;
                    if (gameData->currentCategory ==
                            static_cast<std::size_t>(AssetCategory::models) &&
                        gameData->currentModel) {
                        std::string modelError;
                        if (!modelRenderer.updateMesh(*gameData->currentModel, modelError)) {
                            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                         "Could not restore model preview: %s",
                                         modelError.c_str());
                        }
                    }
                    SDL_SetWindowTitle(window, viewerTitle(*gameData).c_str());
                } else {
                    startDrivingDemo();
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat &&
                       !dialogPending &&
                       sandboxMode &&
                       event.key.key == SDLK_R) {
                vehicle = {};
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL &&
                       !dialogPending &&
                       !sandboxMode &&
                       gameData) {
                long long wheelY = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheelY = -wheelY;
                }
                const int steps = static_cast<int>(
                    std::min(std::llabs(wheelY), 20LL));
                for (int step = 0; step < steps; ++step) {
                    moveAssetSelection(wheelY > 0 ? -1 : 1);
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                       !dialogPending &&
                       !sandboxMode &&
                       gameData) {
                int windowWidth = 0;
                int windowHeight = 0;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                const int mouseX = static_cast<int>(event.button.x);
                const int mouseY = static_cast<int>(event.button.y);
                if (mouseX >= 0 &&
                    mouseX < std::min(AssetBrowserUi::sidebarWidth, windowWidth) &&
                    mouseY >= AssetBrowserUi::categoryTop &&
                    mouseY < AssetBrowserUi::categoryTop +
                        static_cast<int>(categoryCount) * 25) {
                    const auto category = static_cast<std::size_t>(
                        (mouseY - AssetBrowserUi::categoryTop) / 25);
                    selectCategory(category);
                } else if (mouseX >= 0 &&
                           mouseX < std::min(AssetBrowserUi::sidebarWidth, windowWidth) &&
                           mouseY >= AssetBrowserUi::assetTop &&
                           windowHeight > 0) {
                    const int visibleRows = std::max(
                        0, (windowHeight - AssetBrowserUi::assetTop - 30) /
                            AssetBrowserUi::rowHeight);
                    const auto category = gameData->currentCategory;
                    const auto selected = gameData->selectedIndices[category];
                    const auto firstRow = selected >= static_cast<std::size_t>(visibleRows)
                        ? selected - static_cast<std::size_t>(visibleRows) + 1
                        : 0;
                    const auto clickedIndex = firstRow +
                        static_cast<std::size_t>(
                            (mouseY - AssetBrowserUi::assetTop) /
                            AssetBrowserUi::rowHeight);
                    if (clickedIndex < gameData->entryCount(category)) {
                        selectAsset(clickedIndex);
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat &&
                       !dialogPending &&
                       !sandboxMode &&
                       gameData &&
                       event.key.key == SDLK_TAB) {
                const auto nextCategory = (gameData->currentCategory + 1) % categoryCount;
                selectCategory(nextCategory);
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat &&
                       !dialogPending &&
                       !sandboxMode &&
                       gameData &&
                       (event.key.key == SDLK_UP || event.key.key == SDLK_LEFT)) {
                moveAssetSelection(-1);
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat &&
                       !dialogPending &&
                       !sandboxMode &&
                       gameData &&
                       (event.key.key == SDLK_DOWN || event.key.key == SDLK_RIGHT)) {
                moveAssetSelection(1);
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat &&
                       !dialogPending &&
                       !sandboxMode &&
                       gameData &&
                       (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN)) {
                if (gameData->currentCategory !=
                    static_cast<std::size_t>(AssetCategory::audio)) {
                    continue;
                }
                if (!audioInitialized) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Audio preview is unavailable because SDL audio did not initialize.");
                } else {
                    std::string audioError;
                    auto wave = loadWave(
                        *gameData,
                        gameData->selectedIndices[
                            static_cast<std::size_t>(AssetCategory::audio)],
                        audioError);
                    if (!wave || !audioPreview.play(*wave, audioError)) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "Could not play archived audio: %s",
                                     audioError.c_str());
                    }
                }
            }
        }

        const Uint64 frameCounter = SDL_GetPerformanceCounter();
        float frameDelta = 0.0F;
        if (performanceFrequency != 0 && frameCounter >= previousFrameCounter) {
            frameDelta = static_cast<float>(
                static_cast<double>(frameCounter - previousFrameCounter) /
                static_cast<double>(performanceFrequency));
        }
        previousFrameCounter = frameCounter;
        if (sandboxMode && gameData) {
            const bool* keyboard = SDL_GetKeyboardState(nullptr);
            const auto isPressed = [keyboard](SDL_Scancode key) {
                return keyboard[key];
            };
            const float throttle = isPressed(SDL_SCANCODE_W) ||
                    isPressed(SDL_SCANCODE_UP)
                ? 1.0F
                : 0.0F;
            const float brake = isPressed(SDL_SCANCODE_S) ||
                    isPressed(SDL_SCANCODE_DOWN)
                ? 1.0F
                : 0.0F;
            const float steering =
                (isPressed(SDL_SCANCODE_D) || isPressed(SDL_SCANCODE_RIGHT) ? 1.0F : 0.0F) -
                (isPressed(SDL_SCANCODE_A) || isPressed(SDL_SCANCODE_LEFT) ? 1.0F : 0.0F);
            opena2::updateVehicle(vehicle, {throttle, brake, steering}, frameDelta);
        }

        FolderResult result;
        {
            std::lock_guard lock(selection.mutex);
            if (selection.result.completed) {
                result = std::move(selection.result);
                selection.result = {};
            }
        }

        if (result.completed) {
            dialogPending = false;
            if (closeRequested) {
                running = false;
            } else if (!result.error.empty()) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not select the game folder: %s",
                             result.error.c_str());
                exitCode = 1;
                running = false;
            } else if (result.cancelled) {
                running = false;
            } else {
                std::string validationError;
                auto selectedGameData = finishGameDataLoad(result.path, validationError);
                if (selectedGameData) {
                    if (!renderer.initialize(selectedGameData->currentSprite, validationError)) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "Could not initialize sprite renderer: %s",
                                     validationError.c_str());
                        exitCode = 1;
                        running = false;
                        continue;
                    }
                    gameData = std::move(selectedGameData);
                    SDL_SetWindowTitle(window, viewerTitle(*gameData).c_str());
                    SDL_Log("Loaded %zu GAMESPR, %zu A2MRC, %zu audio and %zu model entries.",
                            gameData->gameSprites.entryCount(),
                            gameData->mrcImages.entryCount(),
                            gameData->waveResources.size(),
                            gameData->modelResourceIndices.size());
                } else {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "Could not load game data from the selected folder: %s",
                                 validationError.c_str());
                    SDL_SetWindowTitle(
                        window, "OpenA2Racer - Select folder with A2.EXE and image archives");
                    requestFolder(window, selection);
                    dialogPending = true;
                }
            }
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);
        if (sandboxMode) {
            glClearColor(0.32F, 0.55F, 0.72F, 1.0F);
        } else {
            glClearColor(0.08F, 0.10F, 0.16F, 1.0F);
        }
        glClear(GL_COLOR_BUFFER_BIT);
        if (gameData) {
            if (sandboxMode) {
                modelRenderer.drawSandbox(window, vehicle);
                browserUi.drawSandboxHud(window, vehicle);
            } else if (gameData->currentCategory <=
                       static_cast<std::size_t>(AssetCategory::rcsPcx)) {
                renderer.draw(window, gameData->currentSprite);
            } else if (gameData->currentCategory ==
                           static_cast<std::size_t>(AssetCategory::models) &&
                       gameData->currentModel) {
                modelRenderer.draw(
                    window, static_cast<float>(SDL_GetTicks()) / 1000.0F);
            }
            if (!sandboxMode) {
                browserUi.draw(window, *gameData);
            }
        }
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    audioPreview.stop();
    modelRenderer.destroy();
    browserUi.destroy();
    renderer.destroy();
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    if (audioInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    SDL_Quit();
    return exitCode;
}
