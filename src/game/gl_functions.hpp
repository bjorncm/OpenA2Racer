#pragma once

#include <SDL3/SDL_opengl.h>

#include <initializer_list>
#include <string>

namespace opena2::game {

using DrawArraysProc = void (APIENTRYP)(GLenum mode, GLint first, GLsizei count);
using DrawElementsProc = void (APIENTRYP)(GLenum mode, GLsizei count, GLenum type, const void* indices);

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
    PFNGLUNIFORM3FPROC uniform3f = nullptr;
    PFNGLUNIFORM4FPROC uniform4f = nullptr;
    PFNGLUNIFORMMATRIX4FVPROC uniformMatrix4fv = nullptr;
    PFNGLACTIVETEXTUREPROC activeTexture = nullptr;
    DrawArraysProc drawArrays = nullptr;
    DrawElementsProc drawElements = nullptr;
    PFNGLGENFRAMEBUFFERSPROC genFramebuffers = nullptr;
    PFNGLBINDFRAMEBUFFERPROC bindFramebuffer = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC deleteFramebuffers = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC framebufferTexture2D = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC framebufferRenderbuffer = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC checkFramebufferStatus = nullptr;
    PFNGLGENRENDERBUFFERSPROC genRenderbuffers = nullptr;
    PFNGLBINDRENDERBUFFERPROC bindRenderbuffer = nullptr;
    PFNGLDELETERENDERBUFFERSPROC deleteRenderbuffers = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC renderbufferStorage = nullptr;
    PFNGLBLITFRAMEBUFFERPROC blitFramebuffer = nullptr;

    bool load(std::string& error);
};

// OpenGL 2.0+ entry points, loaded once the game's GL context exists.
extern OpenGLFunctions gl;

// Compiles and links a GLSL program; `attributes` get locations 0, 1, ...
// Returns 0 and sets `error` on failure.
GLuint linkProgram(const char* vertexSource, const char* fragmentSource,
                   std::initializer_list<const char*> attributes, std::string& error);

} // namespace opena2::game
