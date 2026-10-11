#include "gl_functions.hpp"

#include <SDL3/SDL_video.h>

#include <algorithm>

namespace opena2::game {

OpenGLFunctions gl;

bool OpenGLFunctions::load(std::string& error)
{
#define LOAD_GAME_GL(member, symbol) \
    member = reinterpret_cast<decltype(member)>(SDL_GL_GetProcAddress(symbol)); \
    if (member == nullptr) { \
        error = "Could not load OpenGL function " symbol "."; \
        return false; \
    }
    LOAD_GAME_GL(createShader, "glCreateShader")
    LOAD_GAME_GL(shaderSource, "glShaderSource")
    LOAD_GAME_GL(compileShader, "glCompileShader")
    LOAD_GAME_GL(getShaderiv, "glGetShaderiv")
    LOAD_GAME_GL(getShaderInfoLog, "glGetShaderInfoLog")
    LOAD_GAME_GL(deleteShader, "glDeleteShader")
    LOAD_GAME_GL(createProgram, "glCreateProgram")
    LOAD_GAME_GL(attachShader, "glAttachShader")
    LOAD_GAME_GL(bindAttribLocation, "glBindAttribLocation")
    LOAD_GAME_GL(linkProgram, "glLinkProgram")
    LOAD_GAME_GL(getProgramiv, "glGetProgramiv")
    LOAD_GAME_GL(getProgramInfoLog, "glGetProgramInfoLog")
    LOAD_GAME_GL(deleteProgram, "glDeleteProgram")
    LOAD_GAME_GL(getUniformLocation, "glGetUniformLocation")
    LOAD_GAME_GL(genVertexArrays, "glGenVertexArrays")
    LOAD_GAME_GL(bindVertexArray, "glBindVertexArray")
    LOAD_GAME_GL(deleteVertexArrays, "glDeleteVertexArrays")
    LOAD_GAME_GL(genBuffers, "glGenBuffers")
    LOAD_GAME_GL(bindBuffer, "glBindBuffer")
    LOAD_GAME_GL(deleteBuffers, "glDeleteBuffers")
    LOAD_GAME_GL(bufferData, "glBufferData")
    LOAD_GAME_GL(enableVertexAttribArray, "glEnableVertexAttribArray")
    LOAD_GAME_GL(vertexAttribPointer, "glVertexAttribPointer")
    LOAD_GAME_GL(useProgram, "glUseProgram")
    LOAD_GAME_GL(uniform2f, "glUniform2f")
    LOAD_GAME_GL(uniform1i, "glUniform1i")
    LOAD_GAME_GL(uniform3f, "glUniform3f")
    LOAD_GAME_GL(uniform4f, "glUniform4f")
    LOAD_GAME_GL(uniformMatrix4fv, "glUniformMatrix4fv")
    LOAD_GAME_GL(activeTexture, "glActiveTexture")
    LOAD_GAME_GL(drawArrays, "glDrawArrays")
    LOAD_GAME_GL(drawElements, "glDrawElements")
    LOAD_GAME_GL(genFramebuffers, "glGenFramebuffers")
    LOAD_GAME_GL(bindFramebuffer, "glBindFramebuffer")
    LOAD_GAME_GL(deleteFramebuffers, "glDeleteFramebuffers")
    LOAD_GAME_GL(framebufferTexture2D, "glFramebufferTexture2D")
    LOAD_GAME_GL(framebufferRenderbuffer, "glFramebufferRenderbuffer")
    LOAD_GAME_GL(checkFramebufferStatus, "glCheckFramebufferStatus")
    LOAD_GAME_GL(genRenderbuffers, "glGenRenderbuffers")
    LOAD_GAME_GL(bindRenderbuffer, "glBindRenderbuffer")
    LOAD_GAME_GL(deleteRenderbuffers, "glDeleteRenderbuffers")
    LOAD_GAME_GL(renderbufferStorage, "glRenderbufferStorage")
    LOAD_GAME_GL(blitFramebuffer, "glBlitFramebuffer")
#undef LOAD_GAME_GL
    return true;
}

GLuint linkProgram(const char* vertexSource, const char* fragmentSource,
                   std::initializer_list<const char*> attributes, std::string& error)
{
    const auto compile = [&error](GLenum type, const char* source) {
        const GLuint shader = gl.createShader(type);
        gl.shaderSource(shader, 1, &source, nullptr);
        gl.compileShader(shader);
        GLint result = GL_FALSE;
        gl.getShaderiv(shader, GL_COMPILE_STATUS, &result);
        if (result == GL_FALSE) {
            GLint length = 0;
            gl.getShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            gl.getShaderInfoLog(shader, length, nullptr, log.data());
            error = "Could not compile a shader: " + log;
            gl.deleteShader(shader);
            return GLuint{0};
        }
        return shader;
    };
    const GLuint vertexShader = compile(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = vertexShader == 0 ? 0 : compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) gl.deleteShader(vertexShader);
        return 0;
    }
    const GLuint program = gl.createProgram();
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    GLuint location = 0;
    for (const char* attribute : attributes) {
        gl.bindAttribLocation(program, location++, attribute);
    }
    gl.linkProgram(program);
    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);
    GLint linked = GL_FALSE;
    gl.getProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        error = "Could not link a shader.";
        gl.deleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace opena2::game
