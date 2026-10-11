#include "race_renderer.hpp"

#include "opena2/race/scene.hpp"

#include <algorithm>
#include <array>

namespace opena2::game {
namespace {

// The D3D projection (x' = 2x, y' = 2y, z' = z - 1, w = z) followed by the
// viewport clip limits: `extent` holds 2 * the visible half-extents of
// x / z and y / z, and D3D depth z' / w in 0..1 becomes GL's -1..1.
constexpr const char* sceneVertexSource = R"glsl(#version 150 core
in vec3 position;
in vec2 texcoord;
uniform mat4 world;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 extent;
out vec2 uv;
void main()
{
    vec4 clip = projection * view * world * vec4(position, 1.0);
    gl_Position = vec4(clip.x / extent.x, clip.y / extent.y, 2.0 * clip.z - clip.w, clip.w);
    uv = texcoord;
}
)glsl";

constexpr const char* backgroundVertexSource = R"glsl(#version 150 core
in vec3 position;
in vec2 texcoord;
out vec2 uv;
void main()
{
    gl_Position = vec4(position.xy, 0.0, 1.0);
    uv = texcoord;
}
)glsl";

// Index 0xff is the colour key of every texture (load_texture).
constexpr const char* fragmentSource = R"glsl(#version 150 core
uniform sampler2D indices;
uniform sampler2D palette;
in vec2 uv;
out vec4 color;
void main()
{
    float index = texture(indices, uv).r * 255.0;
    if (index > 254.5) {
        discard;
    }
    color = vec4(texture(palette, vec2((index + 0.5) / 256.0, 0.5)).rgb, 1.0);
}
)glsl";

constexpr GLsizei vertexStride = 5 * sizeof(float);

std::array<float, 16> toGl(const race::Mat4& matrix)
{
    // Row-major D3D data read as column-major is the transpose, which turns
    // the row-vector product p * M into GLSL's M' * p.
    std::array<float, 16> values{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            values[static_cast<std::size_t>(row * 4 + column)] = matrix.m[row][column];
        }
    }
    return values;
}

GLuint uploadIndexTexture(int width, int height, const std::uint8_t* pixels, GLint wrap)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

} // namespace

RaceRenderer::~RaceRenderer()
{
    destroy();
}

bool RaceRenderer::initialize(std::string& error)
{
    program_ = linkProgram(sceneVertexSource, fragmentSource, {"position", "texcoord"}, error);
    backgroundProgram_ =
        program_ == 0 ? 0 : linkProgram(backgroundVertexSource, fragmentSource, {"position", "texcoord"}, error);
    if (backgroundProgram_ == 0) {
        destroy();
        return false;
    }
    for (const GLuint program : {program_, backgroundProgram_}) {
        gl.useProgram(program);
        gl.uniform1i(gl.getUniformLocation(program, "indices"), 0);
        gl.uniform1i(gl.getUniformLocation(program, "palette"), 1);
    }
    gl.useProgram(0);
    worldUniform_ = gl.getUniformLocation(program_, "world");
    viewUniform_ = gl.getUniformLocation(program_, "view");
    projectionUniform_ = gl.getUniformLocation(program_, "projection");
    extentUniform_ = gl.getUniformLocation(program_, "extent");

    const std::array<std::uint8_t, 768> black{};
    glGenTextures(1, &paletteTexture_);
    glBindTexture(GL_TEXTURE_2D, paletteTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, black.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // The background quad, in normalised device coordinates.
    constexpr std::array<float, 20> quad{
        -1.0F, 1.0F, 0.0F, 0.0F, 0.0F,
        1.0F, 1.0F, 0.0F, 1.0F, 0.0F,
        -1.0F, -1.0F, 0.0F, 0.0F, 1.0F,
        1.0F, -1.0F, 0.0F, 1.0F, 1.0F,
    };
    gl.genVertexArrays(1, &quadArray_);
    gl.bindVertexArray(quadArray_);
    gl.genBuffers(1, &quadBuffer_);
    gl.bindBuffer(GL_ARRAY_BUFFER, quadBuffer_);
    gl.bufferData(GL_ARRAY_BUFFER, sizeof(quad), quad.data(), GL_STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertexStride, nullptr);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertexStride,
                           reinterpret_cast<const void*>(3 * sizeof(float)));
    gl.bindVertexArray(0);
    return true;
}

void RaceRenderer::destroy()
{
    for (auto& texture : textures_) {
        glDeleteTextures(1, &texture.id);
    }
    textures_.clear();
    for (auto& mesh : meshes_) {
        deleteMesh(mesh);
    }
    meshes_.clear();
    deleteMesh(road_);
    segments_.clear();
    if (paletteTexture_ != 0) glDeleteTextures(1, &paletteTexture_);
    if (quadBuffer_ != 0) gl.deleteBuffers(1, &quadBuffer_);
    if (quadArray_ != 0) gl.deleteVertexArrays(1, &quadArray_);
    if (program_ != 0) gl.deleteProgram(program_);
    if (backgroundProgram_ != 0) gl.deleteProgram(backgroundProgram_);
    paletteTexture_ = quadBuffer_ = quadArray_ = program_ = backgroundProgram_ = 0;
}

void RaceRenderer::setPalette(std::span<const std::uint8_t> paletteRgb)
{
    std::array<std::uint8_t, 768> palette{};
    std::copy_n(paletteRgb.begin(), std::min(paletteRgb.size(), palette.size()), palette.begin());
    glBindTexture(GL_TEXTURE_2D, paletteTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, palette.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

int RaceRenderer::addTexture(const IndexedImage* image)
{
    if (image == nullptr || image->width == 0 || image->height == 0) {
        return -1;
    }
    // Direct3D's default texture address mode is wrap.
    textures_.push_back({uploadIndexTexture(image->width, image->height, image->pixels.data(), GL_REPEAT)});
    return static_cast<int>(textures_.size() - 1);
}

int RaceRenderer::addMesh(const D3DMesh& mesh)
{
    std::vector<float> vertices;
    vertices.reserve(mesh.vertices.size() * 5);
    for (const auto& vertex : mesh.vertices) {
        vertices.insert(vertices.end(), {vertex.position[0], vertex.position[1], vertex.position[2],
                                         vertex.textureCoordinate[0], vertex.textureCoordinate[1]});
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(mesh.faces.size() * 3);
    for (const auto& face : mesh.faces) {
        if (std::all_of(face.indices.begin(), face.indices.end(),
                        [&mesh](std::uint16_t index) { return index < mesh.vertices.size(); })) {
            indices.insert(indices.end(), face.indices.begin(), face.indices.end());
        }
    }
    meshes_.push_back(uploadMesh(vertices, indices));
    return static_cast<int>(meshes_.size() - 1);
}

void RaceRenderer::setRoad(std::span<const race::RoadSegmentMesh> segments, int roadTexture, int vergeTexture)
{
    deleteMesh(road_);
    segments_.clear();
    roadTexture_ = roadTexture;
    vergeTexture_ = vergeTexture;
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    for (const auto& segment : segments) {
        const auto base = static_cast<std::uint32_t>(vertices.size() / 5);
        for (const auto& vertex : segment.vertices) {
            vertices.insert(vertices.end(), {vertex.position.x, vertex.position.y, vertex.position.z,
                                             vertex.u, vertex.v});
        }
        Range range;
        range.roadFirst = static_cast<GLsizei>(indices.size());
        for (const auto& triangle : segment.roadTriangles) {
            for (const auto index : triangle) indices.push_back(base + index);
        }
        range.roadCount = static_cast<GLsizei>(indices.size()) - range.roadFirst;
        range.vergeFirst = static_cast<GLsizei>(indices.size());
        for (const auto& triangle : segment.vergeTriangles) {
            for (const auto index : triangle) indices.push_back(base + index);
        }
        range.vergeCount = static_cast<GLsizei>(indices.size()) - range.vergeFirst;
        segments_.push_back(range);
    }
    road_ = uploadMesh(vertices, indices);
}

RaceRenderer::Mesh RaceRenderer::uploadMesh(std::span<const float> vertices,
                                            std::span<const std::uint32_t> indices)
{
    Mesh mesh;
    gl.genVertexArrays(1, &mesh.vertexArray);
    gl.bindVertexArray(mesh.vertexArray);
    gl.genBuffers(1, &mesh.vertexBuffer);
    gl.bindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    gl.bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(),
                  GL_STATIC_DRAW);
    gl.genBuffers(1, &mesh.indexBuffer);
    gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
    gl.bufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data(),
                  GL_STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertexStride, nullptr);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertexStride,
                           reinterpret_cast<const void*>(3 * sizeof(float)));
    gl.bindVertexArray(0);
    mesh.indexCount = static_cast<GLsizei>(indices.size());
    return mesh;
}

void RaceRenderer::deleteMesh(Mesh& mesh)
{
    if (mesh.indexBuffer != 0) gl.deleteBuffers(1, &mesh.indexBuffer);
    if (mesh.vertexBuffer != 0) gl.deleteBuffers(1, &mesh.vertexBuffer);
    if (mesh.vertexArray != 0) gl.deleteVertexArrays(1, &mesh.vertexArray);
    mesh = {};
}

void RaceRenderer::bindTexture(int texture) const
{
    const GLuint id = texture >= 0 && static_cast<std::size_t>(texture) < textures_.size()
                          ? textures_[static_cast<std::size_t>(texture)].id
                          : 0;
    glBindTexture(GL_TEXTURE_2D, id);
}

void RaceRenderer::setWorld(const race::Mat4& world) const
{
    const auto values = toGl(world);
    gl.uniformMatrix4fv(worldUniform_, 1, GL_FALSE, values.data());
}

void RaceRenderer::draw(const Frame& frame, const ScreenTransform& transform, int pixelHeight)
{
    if (program_ == 0) {
        return;
    }
    const PixelRect rect =
        transform.toTarget(frame.viewportX, frame.viewportY, frame.viewportWidth, frame.viewportHeight);
    const int width = std::max(0, rect.x1 - rect.x0);
    const int height = std::max(0, rect.y1 - rect.y0);
    if (width == 0 || height == 0) {
        return;
    }
    glViewport(rect.x0, pixelHeight - rect.y1, width, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x0, pixelHeight - rect.y1, width, height);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);

    gl.activeTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, paletteTexture_);
    gl.activeTexture(GL_TEXTURE0);

    // The viewport background material: back.pcx stretched over the viewport.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    if (frame.background >= 0) {
        gl.useProgram(backgroundProgram_);
        bindTexture(frame.background);
        gl.bindVertexArray(quadArray_);
        gl.drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Z-buffer on with LESSEQUAL (FUN_0041cfed). Direct3D culls
    // counter-clockwise triangles by default, so clockwise ones are the front.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);

    gl.useProgram(program_);
    // The viewport is a logical 640x480 area, so the clip limits follow its logical size.
    const auto extent = race::viewportExtent(static_cast<int>(frame.viewportWidth),
                                             static_cast<int>(frame.viewportHeight));
    gl.uniform2f(extentUniform_, 2.0F * extent.halfX, 2.0F * extent.halfY);
    const auto view = toGl(frame.view);
    gl.uniformMatrix4fv(viewUniform_, 1, GL_FALSE, view.data());
    const auto projectionValues = toGl(race::projectionMatrix());
    gl.uniformMatrix4fv(projectionUniform_, 1, GL_FALSE, projectionValues.data());

    // draw_exec_buffer: the objects, then the road (FUN_004203ac).
    for (const auto& object : frame.objects) {
        if (object.mesh < 0 || static_cast<std::size_t>(object.mesh) >= meshes_.size()) {
            continue;
        }
        const Mesh& mesh = meshes_[static_cast<std::size_t>(object.mesh)];
        setWorld(object.world);
        bindTexture(object.texture);
        gl.bindVertexArray(mesh.vertexArray);
        gl.drawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    setWorld(race::Mat4::identity());
    gl.bindVertexArray(road_.vertexArray);
    for (const std::size_t index : frame.segments) {
        if (index >= segments_.size()) {
            continue;
        }
        const Range& range = segments_[index];
        bindTexture(roadTexture_);
        gl.drawElements(GL_TRIANGLES, range.roadCount, GL_UNSIGNED_INT,
                        reinterpret_cast<const void*>(static_cast<std::size_t>(range.roadFirst) * 4));
        bindTexture(vergeTexture_);
        gl.drawElements(GL_TRIANGLES, range.vergeCount, GL_UNSIGNED_INT,
                        reinterpret_cast<const void*>(static_cast<std::size_t>(range.vergeFirst) * 4));
    }

    gl.bindVertexArray(0);
    gl.useProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_SCISSOR_TEST);
}

} // namespace opena2::game
