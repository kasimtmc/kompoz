#pragma once

// The little of OpenGL ES 3.0 (with KHR_debug's callback) the OpenGL ES backend
// uses, declared here so that it needs no OpenGL ES headers or libraries: its functions are looked up
// in the context (Gl::load), its constants are Khronos's values.

#include <cstddef>
#include <cstdint>
#include <string>

namespace kompoz::gpu {

#if defined(_WIN32) && !defined(_WIN64)
#define KOMPOZ_GL_API __stdcall
#else
#define KOMPOZ_GL_API
#endif

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;
using GLDEBUGPROC = void(KOMPOZ_GL_API*)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message,
                                         const void* userParam);

inline constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
inline constexpr GLenum GL_BLEND = 0x0BE2;
inline constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
inline constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
inline constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
inline constexpr GLenum GL_DEBUG_OUTPUT = 0x92E0;
inline constexpr GLenum GL_DEBUG_OUTPUT_SYNCHRONOUS = 0x8242;
inline constexpr GLenum GL_DEBUG_SEVERITY_HIGH = 0x9146;
inline constexpr GLenum GL_DEBUG_TYPE_ERROR = 0x824C;
inline constexpr GLenum GL_DITHER = 0x0BD0;
inline constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
inline constexpr GLenum GL_DST_ALPHA = 0x0304;
inline constexpr GLenum GL_DST_COLOR = 0x0306;
inline constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum GL_EXTENSIONS = 0x1F03;
inline constexpr GLenum GL_FALSE = 0;
inline constexpr GLenum GL_FLOAT = 0x1406;
inline constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
inline constexpr GLenum GL_FUNC_ADD = 0x8006;
inline constexpr GLenum GL_FUNC_REVERSE_SUBTRACT = 0x800B;
inline constexpr GLenum GL_FUNC_SUBTRACT = 0x800A;
inline constexpr GLenum GL_GPU_DISJOINT_EXT = 0x8FBB;
inline constexpr GLenum GL_HALF_FLOAT = 0x140B;
inline constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
inline constexpr GLenum GL_LINEAR = 0x2601;
inline constexpr GLenum GL_LINEAR_MIPMAP_NEAREST = 0x2701;
inline constexpr GLenum GL_LINK_STATUS = 0x8B82;
inline constexpr GLenum GL_MAX = 0x8008;
inline constexpr GLenum GL_MIN = 0x8007;
inline constexpr GLenum GL_MIRRORED_REPEAT = 0x8370;
inline constexpr GLenum GL_NEAREST = 0x2600;
inline constexpr GLenum GL_NEAREST_MIPMAP_NEAREST = 0x2700;
inline constexpr GLenum GL_NO_ERROR = 0;
inline constexpr GLenum GL_NUM_EXTENSIONS = 0x821D;
inline constexpr GLenum GL_NUM_PROGRAM_BINARY_FORMATS = 0x87FE;
inline constexpr GLenum GL_ONE = 1;
inline constexpr GLenum GL_ONE_MINUS_DST_ALPHA = 0x0305;
inline constexpr GLenum GL_ONE_MINUS_DST_COLOR = 0x0307;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum GL_ONE_MINUS_SRC_COLOR = 0x0301;
inline constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
inline constexpr GLenum GL_PROGRAM_BINARY_LENGTH = 0x8741;
inline constexpr GLenum GL_PROGRAM_BINARY_RETRIEVABLE_HINT = 0x8257;
inline constexpr GLenum GL_QUERY_RESULT = 0x8866;
inline constexpr GLenum GL_QUERY_RESULT_AVAILABLE = 0x8867;
inline constexpr GLenum GL_R16F = 0x822D;
inline constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
inline constexpr GLenum GL_RED = 0x1903;
inline constexpr GLenum GL_RENDERER = 0x1F01;
inline constexpr GLenum GL_REPEAT = 0x2901;
inline constexpr GLenum GL_RGBA = 0x1908;
inline constexpr GLenum GL_RGBA16F = 0x881A;
inline constexpr GLenum GL_RGBA32F = 0x8814;
inline constexpr GLenum GL_RGBA8 = 0x8058;
inline constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
inline constexpr GLenum GL_SRC_ALPHA = 0x0302;
inline constexpr GLenum GL_SRC_COLOR = 0x0300;
inline constexpr GLenum GL_STREAM_DRAW = 0x88E0;
inline constexpr GLenum GL_TEXTURE0 = 0x84C0;
inline constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum GL_TEXTURE_MAX_LEVEL = 0x813D;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum GL_TIME_ELAPSED_EXT = 0x88BF;
inline constexpr GLenum GL_TRIANGLES = 0x0004;
inline constexpr GLenum GL_UNIFORM_BUFFER = 0x8A11;
inline constexpr GLenum GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT = 0x8A34;
inline constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum GL_UNPACK_ROW_LENGTH = 0x0CF2;
inline constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum GL_UNSIGNED_INT = 0x1405;
inline constexpr GLenum GL_VERSION = 0x1F02;
inline constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
inline constexpr GLenum GL_ZERO = 0;

// The functions: each X(return type, name without its gl, parameters).
#define KOMPOZ_GL_FUNCTIONS(X) \
    X(void, ActiveTexture, (GLenum texture)) \
    X(void, AttachShader, (GLuint program, GLuint shader)) \
    X(void, BeginQuery, (GLenum target, GLuint id)) \
    X(void, BindBuffer, (GLenum target, GLuint buffer)) \
    X(void, BindBufferBase, (GLenum target, GLuint index, GLuint buffer)) \
    X(void, BindBufferRange, (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)) \
    X(void, BindFramebuffer, (GLenum target, GLuint framebuffer)) \
    X(void, BindSampler, (GLuint unit, GLuint sampler)) \
    X(void, BindTexture, (GLenum target, GLuint texture)) \
    X(void, BindVertexArray, (GLuint array)) \
    X(void, BlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha)) \
    X(void, BlendFuncSeparate, (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)) \
    X(void, BlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)) \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data)) \
    X(GLenum, CheckFramebufferStatus, (GLenum target)) \
    X(void, Clear, (GLbitfield mask)) \
    X(void, ClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)) \
    X(void, CompileShader, (GLuint shader)) \
    X(GLuint, CreateProgram, (void)) \
    X(GLuint, CreateShader, (GLenum type)) \
    X(void, DeleteBuffers, (GLsizei n, const GLuint *buffers)) \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
    X(void, DeleteProgram, (GLuint program)) \
    X(void, DeleteQueries, (GLsizei n, const GLuint *ids)) \
    X(void, DeleteSamplers, (GLsizei count, const GLuint *samplers)) \
    X(void, DeleteShader, (GLuint shader)) \
    X(void, DeleteTextures, (GLsizei n, const GLuint *textures)) \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
    X(void, Disable, (GLenum cap)) \
    X(void, DisableVertexAttribArray, (GLuint index)) \
    X(void, DrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instancecount)) \
    X(void, Enable, (GLenum cap)) \
    X(void, EnableVertexAttribArray, (GLuint index)) \
    X(void, EndQuery, (GLenum target)) \
    X(void, Finish, (void)) \
    X(void, Flush, (void)) \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(void, GenBuffers, (GLsizei n, GLuint *buffers)) \
    X(void, GenFramebuffers, (GLsizei n, GLuint *framebuffers)) \
    X(void, GenQueries, (GLsizei n, GLuint *ids)) \
    X(void, GenSamplers, (GLsizei count, GLuint *samplers)) \
    X(void, GenTextures, (GLsizei n, GLuint *textures)) \
    X(void, GenVertexArrays, (GLsizei n, GLuint *arrays)) \
    X(void, GenerateMipmap, (GLenum target)) \
    X(GLenum, GetError, (void)) \
    X(void, GetIntegerv, (GLenum pname, GLint *data)) \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
    X(void, GetQueryObjectuiv, (GLuint id, GLenum pname, GLuint *params)) \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
    X(const GLubyte *, GetString, (GLenum name)) \
    X(const GLubyte *, GetStringi, (GLenum name, GLuint index)) \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar *name)) \
    X(void, LinkProgram, (GLuint program)) \
    X(void, PixelStorei, (GLenum pname, GLint param)) \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)) \
    X(void, SamplerParameteri, (GLuint sampler, GLenum pname, GLint param)) \
    X(void, Scissor, (GLint x, GLint y, GLsizei width, GLsizei height)) \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length)) \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param)) \
    X(void, TexStorage2D, (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)) \
    X(void, TexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)) \
    X(void, Uniform1i, (GLint location, GLint v0)) \
    X(void, UseProgram, (GLuint program)) \
    X(void, VertexAttribDivisor, (GLuint index, GLuint divisor)) \
    X(void, VertexAttribIPointer, (GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)) \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)) \
    X(void, Viewport, (GLint x, GLint y, GLsizei width, GLsizei height))

struct Gl {
#define KOMPOZ_GL_MEMBER(ret, name, args) ret(KOMPOZ_GL_API* name) args = nullptr;
    KOMPOZ_GL_FUNCTIONS(KOMPOZ_GL_MEMBER)
#undef KOMPOZ_GL_MEMBER

    // The debug output's callback, not required: KHR_debug's and OpenGL ES 3.2's (the same function
    // under two names, each to call only where it is the context's: the core one is an error on an
    // older context).
    void(KOMPOZ_GL_API* DebugMessageCallbackKHR)(GLDEBUGPROC callback, const void* userParam) = nullptr;
    void(KOMPOZ_GL_API* DebugMessageCallback)(GLDEBUGPROC callback, const void* userParam) = nullptr;
    // Programs' binaries, not required: OpenGL ES 3.0 has them, WebGL hasn't.
    void(KOMPOZ_GL_API* GetProgramBinary)(GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat, void* binary) = nullptr;
    void(KOMPOZ_GL_API* ProgramBinary)(GLuint program, GLenum binaryFormat, const void* binary, GLsizei length) = nullptr;
    void(KOMPOZ_GL_API* ProgramParameteri)(GLuint program, GLenum pname, GLint value) = nullptr;

    // Every function from `lookup` (glName); false (and *missing the first one's name) if one isn't
    // there.
    bool load(void* (*lookup)(const char* name), std::string* missing) {
#define KOMPOZ_GL_LOAD(ret, name, args)                          \
    name = reinterpret_cast<decltype(name)>(lookup("gl" #name)); \
    if (!name) {                                                 \
        if (missing) *missing = "gl" #name;                      \
        return false;                                            \
    }
        KOMPOZ_GL_FUNCTIONS(KOMPOZ_GL_LOAD)
#undef KOMPOZ_GL_LOAD
        DebugMessageCallbackKHR = reinterpret_cast<decltype(DebugMessageCallbackKHR)>(lookup("glDebugMessageCallbackKHR"));
        DebugMessageCallback = reinterpret_cast<decltype(DebugMessageCallback)>(lookup("glDebugMessageCallback"));
        GetProgramBinary = reinterpret_cast<decltype(GetProgramBinary)>(lookup("glGetProgramBinary"));
        ProgramBinary = reinterpret_cast<decltype(ProgramBinary)>(lookup("glProgramBinary"));
        ProgramParameteri = reinterpret_cast<decltype(ProgramParameteri)>(lookup("glProgramParameteri"));
        return true;
    }
};

} // namespace kompoz::gpu
