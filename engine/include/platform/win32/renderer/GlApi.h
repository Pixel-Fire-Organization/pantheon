#pragma once

#include <windows.h>

#include <GL/gl.h>

#include <cstdint>

// Types GL 1.1 headers predate.
typedef ptrdiff_t GLsizeiptrARB_;
typedef ptrdiff_t GLintptrARB_;
typedef char GLchar_;

// --- constants the 1.1 header does not carry --------------------------------
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE0 0x84C0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_MULTISAMPLE 0x809D
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_BINDING 0x8CA6

// --- WGL context creation ---------------------------------------------------
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXT)(int);

extern PFNWGLCREATECONTEXTATTRIBSARB gl_wglCreateContextAttribsARB;
extern PFNWGLSWAPINTERVALEXT gl_wglSwapIntervalEXT;

// --- buffers ----------------------------------------------------------------
typedef void(APIENTRY* PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_glBindBuffer)(GLenum, GLuint);
typedef void(APIENTRY* PFN_glBufferData)(GLenum, GLsizeiptrARB_, const void*, GLenum);
typedef void(APIENTRY* PFN_glBufferSubData)(GLenum, GLintptrARB_, GLsizeiptrARB_, const void*);
typedef void(APIENTRY* PFN_glDeleteBuffers)(GLsizei, const GLuint*);

extern PFN_glGenBuffers gl_GenBuffers;
extern PFN_glBindBuffer gl_BindBuffer;
extern PFN_glBufferData gl_BufferData;
extern PFN_glBufferSubData gl_BufferSubData;
extern PFN_glDeleteBuffers gl_DeleteBuffers;

// --- vertex arrays (3.0+; absent on the 2.1 path) ---------------------------
typedef void(APIENTRY* PFN_glGenVertexArrays)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_glBindVertexArray)(GLuint);
typedef void(APIENTRY* PFN_glDeleteVertexArrays)(GLsizei, const GLuint*);

extern PFN_glGenVertexArrays gl_GenVertexArrays;
extern PFN_glBindVertexArray gl_BindVertexArray;
extern PFN_glDeleteVertexArrays gl_DeleteVertexArrays;

// --- framebuffer objects (RenderToImage3D only) -----------------------------
// Core since GL 3.0; on the legacy 2.1 path it depends on GL_ARB/EXT_
// framebuffer_object, which a software/virtualised driver may not carry -- the
// exact reason that path exists at all (see kVertex120's comment). Loaded as
// optional on every path; RenderToImage3D checks gl_GenFramebuffers itself and
// answers 0, the same honest "unsupported" every other backend without an
// offscreen target gives, rather than failing renderer construction over it.
typedef void(APIENTRY* PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void(APIENTRY* PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void(APIENTRY* PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef GLenum(APIENTRY* PFN_glCheckFramebufferStatus)(GLenum);
typedef void(APIENTRY* PFN_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void(APIENTRY* PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void(APIENTRY* PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void(APIENTRY* PFN_glDeleteRenderbuffers)(GLsizei, const GLuint*);

extern PFN_glGenFramebuffers gl_GenFramebuffers;
extern PFN_glBindFramebuffer gl_BindFramebuffer;
extern PFN_glFramebufferTexture2D gl_FramebufferTexture2D;
extern PFN_glDeleteFramebuffers gl_DeleteFramebuffers;
extern PFN_glCheckFramebufferStatus gl_CheckFramebufferStatus;
extern PFN_glGenRenderbuffers gl_GenRenderbuffers;
extern PFN_glBindRenderbuffer gl_BindRenderbuffer;
extern PFN_glRenderbufferStorage gl_RenderbufferStorage;
extern PFN_glFramebufferRenderbuffer gl_FramebufferRenderbuffer;
extern PFN_glDeleteRenderbuffers gl_DeleteRenderbuffers;

// --- shaders ----------------------------------------------------------------
typedef GLuint(APIENTRY* PFN_glCreateShader)(GLenum);
typedef void(APIENTRY* PFN_glShaderSource)(GLuint, GLsizei, const GLchar_* const*, const GLint*);
typedef void(APIENTRY* PFN_glCompileShader)(GLuint);
typedef void(APIENTRY* PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar_*);
typedef void(APIENTRY* PFN_glDeleteShader)(GLuint);
typedef GLuint(APIENTRY* PFN_glCreateProgram)(void);
typedef void(APIENTRY* PFN_glAttachShader)(GLuint, GLuint);
typedef void(APIENTRY* PFN_glLinkProgram)(GLuint);
typedef void(APIENTRY* PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar_*);
typedef void(APIENTRY* PFN_glUseProgram)(GLuint);
typedef void(APIENTRY* PFN_glDeleteProgram)(GLuint);
typedef void(APIENTRY* PFN_glBindAttribLocation)(GLuint, GLuint, const GLchar_*);

extern PFN_glCreateShader gl_CreateShader;
extern PFN_glShaderSource gl_ShaderSource;
extern PFN_glCompileShader gl_CompileShader;
extern PFN_glGetShaderiv gl_GetShaderiv;
extern PFN_glGetShaderInfoLog gl_GetShaderInfoLog;
extern PFN_glDeleteShader gl_DeleteShader;
extern PFN_glCreateProgram gl_CreateProgram;
extern PFN_glAttachShader gl_AttachShader;
extern PFN_glLinkProgram gl_LinkProgram;
extern PFN_glGetProgramiv gl_GetProgramiv;
extern PFN_glGetProgramInfoLog gl_GetProgramInfoLog;
extern PFN_glUseProgram gl_UseProgram;
extern PFN_glDeleteProgram gl_DeleteProgram;
extern PFN_glBindAttribLocation gl_BindAttribLocation;

// --- uniforms and attributes ------------------------------------------------
typedef GLint(APIENTRY* PFN_glGetUniformLocation)(GLuint, const GLchar_*);
typedef void(APIENTRY* PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void(APIENTRY* PFN_glUniform1i)(GLint, GLint);
typedef void(APIENTRY* PFN_glEnableVertexAttribArray)(GLuint);
typedef void(APIENTRY* PFN_glDisableVertexAttribArray)(GLuint);
typedef void(APIENTRY* PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void(APIENTRY* PFN_glActiveTexture)(GLenum);

extern PFN_glGetUniformLocation gl_GetUniformLocation;
extern PFN_glUniformMatrix4fv gl_UniformMatrix4fv;
extern PFN_glUniform1i gl_Uniform1i;
extern PFN_glEnableVertexAttribArray gl_EnableVertexAttribArray;
extern PFN_glDisableVertexAttribArray gl_DisableVertexAttribArray;
extern PFN_glVertexAttribPointer gl_VertexAttribPointer;
extern PFN_glActiveTexture gl_ActiveTexture;

// Load every entry point above through wglGetProcAddress. A current context is
// required. `coreProfile` decides whether the VAO functions are mandatory:
// they do not exist on the 2.1 path and their absence is not an error there.
// Returns false only when something the chosen path genuinely needs is missing.
bool Gl_LoadFunctions(bool coreProfile);
