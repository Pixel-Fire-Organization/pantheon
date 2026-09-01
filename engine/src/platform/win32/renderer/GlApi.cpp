#include "platform/win32/renderer/GlApi.h"

#include "core/EngineDebug.h"

PFNWGLCREATECONTEXTATTRIBSARB gl_wglCreateContextAttribsARB = nullptr;
PFNWGLSWAPINTERVALEXT gl_wglSwapIntervalEXT = nullptr;

PFN_glGenBuffers gl_GenBuffers = nullptr;
PFN_glBindBuffer gl_BindBuffer = nullptr;
PFN_glBufferData gl_BufferData = nullptr;
PFN_glBufferSubData gl_BufferSubData = nullptr;
PFN_glDeleteBuffers gl_DeleteBuffers = nullptr;
PFN_glCreateShader gl_CreateShader = nullptr;
PFN_glShaderSource gl_ShaderSource = nullptr;
PFN_glCompileShader gl_CompileShader = nullptr;
PFN_glGetShaderiv gl_GetShaderiv = nullptr;
PFN_glGetShaderInfoLog gl_GetShaderInfoLog = nullptr;
PFN_glDeleteShader gl_DeleteShader = nullptr;
PFN_glCreateProgram gl_CreateProgram = nullptr;
PFN_glAttachShader gl_AttachShader = nullptr;
PFN_glLinkProgram gl_LinkProgram = nullptr;
PFN_glGetProgramiv gl_GetProgramiv = nullptr;
PFN_glGetProgramInfoLog gl_GetProgramInfoLog = nullptr;
PFN_glUseProgram gl_UseProgram = nullptr;
PFN_glDeleteProgram gl_DeleteProgram = nullptr;
PFN_glBindAttribLocation gl_BindAttribLocation = nullptr;
PFN_glGetUniformLocation gl_GetUniformLocation = nullptr;
PFN_glUniformMatrix4fv gl_UniformMatrix4fv = nullptr;
PFN_glUniform1i gl_Uniform1i = nullptr;
PFN_glEnableVertexAttribArray gl_EnableVertexAttribArray = nullptr;
PFN_glDisableVertexAttribArray gl_DisableVertexAttribArray = nullptr;
PFN_glVertexAttribPointer gl_VertexAttribPointer = nullptr;
PFN_glActiveTexture gl_ActiveTexture = nullptr;
PFN_glGenVertexArrays gl_GenVertexArrays = nullptr;
PFN_glBindVertexArray gl_BindVertexArray = nullptr;
PFN_glDeleteVertexArrays gl_DeleteVertexArrays = nullptr;

PFN_glGenFramebuffers gl_GenFramebuffers = nullptr;
PFN_glBindFramebuffer gl_BindFramebuffer = nullptr;
PFN_glFramebufferTexture2D gl_FramebufferTexture2D = nullptr;
PFN_glDeleteFramebuffers gl_DeleteFramebuffers = nullptr;
PFN_glCheckFramebufferStatus gl_CheckFramebufferStatus = nullptr;
PFN_glGenRenderbuffers gl_GenRenderbuffers = nullptr;
PFN_glBindRenderbuffer gl_BindRenderbuffer = nullptr;
PFN_glRenderbufferStorage gl_RenderbufferStorage = nullptr;
PFN_glFramebufferRenderbuffer gl_FramebufferRenderbuffer = nullptr;
PFN_glDeleteRenderbuffers gl_DeleteRenderbuffers = nullptr;

namespace
{

    // wglGetProcAddress only resolves functions the CURRENT context implements,
    // and on some drivers returns 1/2/3/-1 rather than null for a miss. Falling
    // back to the opengl32.dll export table covers the GL 1.1 names that
    // wglGetProcAddress is not required to answer for.
    void* GetGlProc(const char* name)
    {
        void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
        const intptr_t v = reinterpret_cast<intptr_t>(p);
        if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1)
        {
            static HMODULE s_gl = LoadLibraryA("opengl32.dll");
            p = s_gl ? reinterpret_cast<void*>(GetProcAddress(s_gl, name)) : nullptr;
        }
        return p;
    }

    bool Load(void** slot, const char* name, bool required)
    {
        *slot = GetGlProc(name);
        if (!*slot && required)
        {
            Engine_LogError("OpenGl: driver does not provide %s", name);
            return false;
        }
        return *slot != nullptr;
    }

} // namespace

bool Gl_LoadFunctions(bool coreProfile)
{
    bool ok = true;
    ok &= Load(reinterpret_cast<void**>(&gl_GenBuffers), "glGenBuffers", true);
    ok &= Load(reinterpret_cast<void**>(&gl_BindBuffer), "glBindBuffer", true);
    ok &= Load(reinterpret_cast<void**>(&gl_BufferData), "glBufferData", true);
    ok &= Load(reinterpret_cast<void**>(&gl_BufferSubData), "glBufferSubData", true);
    ok &= Load(reinterpret_cast<void**>(&gl_DeleteBuffers), "glDeleteBuffers", true);
    ok &= Load(reinterpret_cast<void**>(&gl_CreateShader), "glCreateShader", true);
    ok &= Load(reinterpret_cast<void**>(&gl_ShaderSource), "glShaderSource", true);
    ok &= Load(reinterpret_cast<void**>(&gl_CompileShader), "glCompileShader", true);
    ok &= Load(reinterpret_cast<void**>(&gl_GetShaderiv), "glGetShaderiv", true);
    ok &= Load(reinterpret_cast<void**>(&gl_GetShaderInfoLog), "glGetShaderInfoLog", true);
    ok &= Load(reinterpret_cast<void**>(&gl_DeleteShader), "glDeleteShader", true);
    ok &= Load(reinterpret_cast<void**>(&gl_CreateProgram), "glCreateProgram", true);
    ok &= Load(reinterpret_cast<void**>(&gl_AttachShader), "glAttachShader", true);
    ok &= Load(reinterpret_cast<void**>(&gl_LinkProgram), "glLinkProgram", true);
    ok &= Load(reinterpret_cast<void**>(&gl_GetProgramiv), "glGetProgramiv", true);
    ok &= Load(reinterpret_cast<void**>(&gl_GetProgramInfoLog), "glGetProgramInfoLog", true);
    ok &= Load(reinterpret_cast<void**>(&gl_UseProgram), "glUseProgram", true);
    ok &= Load(reinterpret_cast<void**>(&gl_DeleteProgram), "glDeleteProgram", true);
    ok &= Load(reinterpret_cast<void**>(&gl_BindAttribLocation), "glBindAttribLocation", true);
    ok &= Load(reinterpret_cast<void**>(&gl_GetUniformLocation), "glGetUniformLocation", true);
    ok &= Load(reinterpret_cast<void**>(&gl_UniformMatrix4fv), "glUniformMatrix4fv", true);
    ok &= Load(reinterpret_cast<void**>(&gl_Uniform1i), "glUniform1i", true);
    ok &= Load(reinterpret_cast<void**>(&gl_EnableVertexAttribArray), "glEnableVertexAttribArray", true);
    ok &= Load(reinterpret_cast<void**>(&gl_DisableVertexAttribArray), "glDisableVertexAttribArray", true);
    ok &= Load(reinterpret_cast<void**>(&gl_VertexAttribPointer), "glVertexAttribPointer", true);
    ok &= Load(reinterpret_cast<void**>(&gl_ActiveTexture), "glActiveTexture", true);

    // VAOs are a core-profile requirement and simply absent on the 2.1 path,
    // where client-side attribute setup is used instead. Only fail on them when
    // the core path was actually chosen.
    bool vaoOk = true;
    vaoOk &= Load(reinterpret_cast<void**>(&gl_GenVertexArrays), "glGenVertexArrays", coreProfile);
    vaoOk &= Load(reinterpret_cast<void**>(&gl_BindVertexArray), "glBindVertexArray", coreProfile);
    vaoOk &= Load(reinterpret_cast<void**>(&gl_DeleteVertexArrays), "glDeleteVertexArrays", coreProfile);
    if (coreProfile && !vaoOk)
        ok = false;

    // Optional: without it the swap interval cannot be set and the frame runs
    // uncapped, which is a nuisance rather than a failure.
    Load(reinterpret_cast<void**>(&gl_wglSwapIntervalEXT), "wglSwapIntervalEXT", false);

    // Optional: see the comment beside these declarations in GlApi.h.
    // RenderToImage3D checks gl_GenFramebuffers itself before using any of
    // this, so a miss here never fails renderer construction.
    Load(reinterpret_cast<void**>(&gl_GenFramebuffers), "glGenFramebuffers", false);
    Load(reinterpret_cast<void**>(&gl_BindFramebuffer), "glBindFramebuffer", false);
    Load(reinterpret_cast<void**>(&gl_FramebufferTexture2D), "glFramebufferTexture2D", false);
    Load(reinterpret_cast<void**>(&gl_DeleteFramebuffers), "glDeleteFramebuffers", false);
    Load(reinterpret_cast<void**>(&gl_CheckFramebufferStatus), "glCheckFramebufferStatus", false);
    Load(reinterpret_cast<void**>(&gl_GenRenderbuffers), "glGenRenderbuffers", false);
    Load(reinterpret_cast<void**>(&gl_BindRenderbuffer), "glBindRenderbuffer", false);
    Load(reinterpret_cast<void**>(&gl_RenderbufferStorage), "glRenderbufferStorage", false);
    Load(reinterpret_cast<void**>(&gl_FramebufferRenderbuffer), "glFramebufferRenderbuffer", false);
    Load(reinterpret_cast<void**>(&gl_DeleteRenderbuffers), "glDeleteRenderbuffers", false);

    return ok;
}
