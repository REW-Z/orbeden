#include "ThirdParty/glad/glad.h"

PFNGLVIEWPORTPROC glad_glViewport = nullptr;
PFNGLCLEARCOLORPROC glad_glClearColor = nullptr;
PFNGLCLEARPROC glad_glClear = nullptr;
PFNGLENABLEPROC glad_glEnable = nullptr;
PFNGLDISABLEPROC glad_glDisable = nullptr;
PFNGLDEPTHFUNCPROC glad_glDepthFunc = nullptr;
PFNGLBLENDFUNCPROC glad_glBlendFunc = nullptr;
PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC glad_glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glad_glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glad_glBufferData = nullptr;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray = nullptr;
PFNGLCREATESHADERPROC glad_glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glad_glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glad_glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glad_glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC glad_glDeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC glad_glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glad_glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glad_glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glad_glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog = nullptr;
PFNGLDELETEPROGRAMPROC glad_glDeleteProgram = nullptr;
PFNGLUSEPROGRAMPROC glad_glUseProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv = nullptr;
PFNGLUNIFORM3FPROC glad_glUniform3f = nullptr;
PFNGLUNIFORM4FPROC glad_glUniform4f = nullptr;
PFNGLUNIFORM1IPROC glad_glUniform1i = nullptr;
PFNGLACTIVETEXTUREPROC glad_glActiveTexture = nullptr;
PFNGLGENTEXTURESPROC glad_glGenTextures = nullptr;
PFNGLBINDTEXTUREPROC glad_glBindTexture = nullptr;
PFNGLTEXPARAMETERIPROC glad_glTexParameteri = nullptr;
PFNGLTEXIMAGE2DPROC glad_glTexImage2D = nullptr;
PFNGLDELETETEXTURESPROC glad_glDeleteTextures = nullptr;
PFNGLDRAWELEMENTSPROC glad_glDrawElements = nullptr;

namespace
{
    template<typename T>
    bool Load(T& target, GLADloadproc load, const char* name)
    {
        target = reinterpret_cast<T>(load(name));
        return target != nullptr;
    }
}

int gladLoadGLLoader(GLADloadproc load)
{
    if (!load) return 0;

    bool ok = true;
    ok &= Load(glad_glViewport, load, "glViewport");
    ok &= Load(glad_glClearColor, load, "glClearColor");
    ok &= Load(glad_glClear, load, "glClear");
    ok &= Load(glad_glEnable, load, "glEnable");
    ok &= Load(glad_glDisable, load, "glDisable");
    ok &= Load(glad_glDepthFunc, load, "glDepthFunc");
    ok &= Load(glad_glBlendFunc, load, "glBlendFunc");
    ok &= Load(glad_glGenVertexArrays, load, "glGenVertexArrays");
    ok &= Load(glad_glBindVertexArray, load, "glBindVertexArray");
    ok &= Load(glad_glDeleteVertexArrays, load, "glDeleteVertexArrays");
    ok &= Load(glad_glGenBuffers, load, "glGenBuffers");
    ok &= Load(glad_glBindBuffer, load, "glBindBuffer");
    ok &= Load(glad_glBufferData, load, "glBufferData");
    ok &= Load(glad_glDeleteBuffers, load, "glDeleteBuffers");
    ok &= Load(glad_glVertexAttribPointer, load, "glVertexAttribPointer");
    ok &= Load(glad_glEnableVertexAttribArray, load, "glEnableVertexAttribArray");
    ok &= Load(glad_glCreateShader, load, "glCreateShader");
    ok &= Load(glad_glShaderSource, load, "glShaderSource");
    ok &= Load(glad_glCompileShader, load, "glCompileShader");
    ok &= Load(glad_glGetShaderiv, load, "glGetShaderiv");
    ok &= Load(glad_glGetShaderInfoLog, load, "glGetShaderInfoLog");
    ok &= Load(glad_glDeleteShader, load, "glDeleteShader");
    ok &= Load(glad_glCreateProgram, load, "glCreateProgram");
    ok &= Load(glad_glAttachShader, load, "glAttachShader");
    ok &= Load(glad_glLinkProgram, load, "glLinkProgram");
    ok &= Load(glad_glGetProgramiv, load, "glGetProgramiv");
    ok &= Load(glad_glGetProgramInfoLog, load, "glGetProgramInfoLog");
    ok &= Load(glad_glDeleteProgram, load, "glDeleteProgram");
    ok &= Load(glad_glUseProgram, load, "glUseProgram");
    ok &= Load(glad_glGetUniformLocation, load, "glGetUniformLocation");
    ok &= Load(glad_glUniformMatrix4fv, load, "glUniformMatrix4fv");
    ok &= Load(glad_glUniform3f, load, "glUniform3f");
    ok &= Load(glad_glUniform4f, load, "glUniform4f");
    ok &= Load(glad_glUniform1i, load, "glUniform1i");
    ok &= Load(glad_glActiveTexture, load, "glActiveTexture");
    ok &= Load(glad_glGenTextures, load, "glGenTextures");
    ok &= Load(glad_glBindTexture, load, "glBindTexture");
    ok &= Load(glad_glTexParameteri, load, "glTexParameteri");
    ok &= Load(glad_glTexImage2D, load, "glTexImage2D");
    ok &= Load(glad_glDeleteTextures, load, "glDeleteTextures");
    ok &= Load(glad_glDrawElements, load, "glDrawElements");
    return ok ? 1 : 0;
}

