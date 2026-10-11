#pragma once

#include "gl_functions.hpp"

#include "opena2/d3d_mesh.hpp"
#include "opena2/indexed_image.hpp"
#include "opena2/race/race_math.hpp"
#include "opena2/race/track.hpp"
#include "opena2/screen_transform.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace opena2::game {

// Direct3D immediate mode of A2SPEL.DAT on OpenGL 3.2. Like the original in
// its 8-bit configurations, textures keep their palette indices and are drawn
// through one palette (SURSCRN.PCX), index 0xff is the colour key, and
// nothing is lit (DAT_004764f4 stays 0, so every vertex is a white
// D3DLVERTEX). Fog is off (init_render_state) and texture filtering is
// nearest.
class RaceRenderer {
public:
    RaceRenderer() = default;
    RaceRenderer(const RaceRenderer&) = delete;
    RaceRenderer& operator=(const RaceRenderer&) = delete;
    ~RaceRenderer();

    bool initialize(std::string& error);
    void destroy();

    void setPalette(std::span<const std::uint8_t> paletteRgb);
    // Returns a texture id, or -1 for a null image.
    int addTexture(const IndexedImage* image);
    // Returns a mesh id.
    int addMesh(const D3DMesh& mesh);
    // The road: one mesh per segment, drawn with the road and verge textures.
    void setRoad(std::span<const race::RoadSegmentMesh> segments, int roadTexture, int vergeTexture);

    struct Object {
        int mesh = -1;
        int texture = -1;
        race::Mat4 world;
    };

    struct Frame {
        // The viewport in logical 640x480 coordinates.
        float viewportX = 0.0F;
        float viewportY = 0.0F;
        float viewportWidth = 640.0F;
        float viewportHeight = 480.0F;
        // The viewport background (set_background_texture), stretched over it.
        int background = -1;
        race::Mat4 view;
        std::vector<Object> objects;
        // Road segments to draw, in order.
        std::vector<std::size_t> segments;
    };

    void draw(const Frame& frame, const ScreenTransform& transform, int pixelHeight);

private:
    struct Texture {
        GLuint id = 0;
    };
    struct Mesh {
        GLuint vertexArray = 0;
        GLuint vertexBuffer = 0;
        GLuint indexBuffer = 0;
        GLsizei indexCount = 0;
    };
    struct Range {
        GLsizei roadFirst = 0;
        GLsizei roadCount = 0;
        GLsizei vergeFirst = 0;
        GLsizei vergeCount = 0;
    };

    static Mesh uploadMesh(std::span<const float> vertices, std::span<const std::uint32_t> indices);
    static void deleteMesh(Mesh& mesh);
    void bindTexture(int texture) const;
    void setWorld(const race::Mat4& world) const;

    GLuint program_ = 0;
    GLuint backgroundProgram_ = 0;
    GLint worldUniform_ = -1;
    GLint viewUniform_ = -1;
    GLint projectionUniform_ = -1;
    GLint extentUniform_ = -1;
    GLuint paletteTexture_ = 0;
    GLuint quadArray_ = 0;
    GLuint quadBuffer_ = 0;
    std::vector<Texture> textures_;
    std::vector<Mesh> meshes_;
    Mesh road_;
    std::vector<Range> segments_;
    int roadTexture_ = -1;
    int vergeTexture_ = -1;
};

} // namespace opena2::game
