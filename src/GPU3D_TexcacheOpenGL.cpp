#include "GPU3D_TexcacheOpenGL.h"

#include "OpenGL_shaders/3DTexUpscaleVS.h"
#include "OpenGL_shaders/3DTexUpscaleFS.h"

namespace melonDS
{

GLuint TexcacheOpenGLLoader::GenerateTexture(u32 width, u32 height, u32 layers)
{
    GLuint texarray;
    glGenTextures(1, &texarray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texarray);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (IsCompute)
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8UI, width, height, layers);
    else
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8UI, width, height, layers, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    return texarray;
}

void TexcacheOpenGLLoader::UploadTexture(GLuint handle, u32 width, u32 height, u32 layer, void* data)
{
    glBindTexture(GL_TEXTURE_2D_ARRAY, handle);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
        0, 0, 0, layer,
        width, height, 1,
        GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, data);
}

void TexcacheOpenGLLoader::DeleteTexture(GLuint handle)
{
    glDeleteTextures(1, &handle);
}

bool TexcacheOpenGLLoader::GPUUpscaleSharpenSaturate(GLuint destArray, u32 nativeW, u32 nativeH, u32 destLayer,
                                                      const void* rawDecodedData, float sharpenStrength, float satFactor)
{
    // See this method's declaration comment - the compute-path texture
    // lifecycle wasn't something that could be safely verified here, so
    // it keeps using the (working, already-shipped) CPU pipeline.
    if (IsCompute)
        return false;

    if (!UpscaleShaderInitAttempted)
    {
        UpscaleShaderInitAttempted = true;
        UpscaleShaderReady = OpenGL::CompileVertexFragmentProgram(UpscaleShader,
            k3DTexUpscaleVS, k3DTexUpscaleFS,
            "3DTexUpscaleShader",
            {}, // no vertex inputs - fullscreen triangle is built from gl_VertexID
            {{"oColor", 0}});

        if (UpscaleShaderReady)
        {
            glUseProgram(UpscaleShader);
            GLint srcTexULoc = glGetUniformLocation(UpscaleShader, "SrcTex");
            glUniform1i(srcTexULoc, 0);
            UpscaleShaderSrcSizeULoc = glGetUniformLocation(UpscaleShader, "uSrcSize");
            UpscaleShaderStrengthULoc = glGetUniformLocation(UpscaleShader, "uSharpenStrength");
            UpscaleShaderSatULoc = glGetUniformLocation(UpscaleShader, "uSaturationBoost");

            glGenFramebuffers(1, &UpscaleFBO);
            glGenTextures(1, &UpscaleScratchTex);
            // Every other draw pass in this codebase (GPU2D_OpenGL.cpp,
            // GPU3D_OpenGL.cpp, GPU_OpenGL.cpp, Screen.cpp) creates and
            // binds its own VAO before drawing, even when it has no
            // real per-vertex attributes to describe - core-profile
            // OpenGL requires *some* VAO bound for glDrawArrays to be
            // valid at all, even for a gl_VertexID-only vertex shader
            // like this pass's. This was missing from the first version
            // of this function - whether it happened to work depended
            // entirely on some VAO being left bound by whatever ran
            // before GetTexture() was called, which isn't something to
            // rely on. Fixed: this pass now owns and binds its own
            // (attribute-less) VAO, matching the rest of the codebase.
            glGenVertexArrays(1, &UpscaleVAO);
        }
    }

    if (!UpscaleShaderReady)
        return false;

    // Save the state this pass touches, so GetTexture's caller (the 3D
    // renderer, mid-scene-setup) sees everything back exactly as it was
    // once this call returns.
    GLint prevFBO = 0, prevProgram = 0, prevActiveTex = 0, prevTexBinding = 0, prevVAO = 0;
    GLint prevViewport[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexBinding);

    // Scratch input texture: holds the raw (native-resolution, not yet
    // upscaled) decoded texel data so the shader can sample it. Resized
    // only when it's too small for the current texture, reused
    // otherwise - most textures in a given game cluster around a
    // handful of sizes, so this avoids reallocating on every call.
    if (UpscaleScratchW < nativeW || UpscaleScratchH < nativeH)
    {
        UpscaleScratchW = std::max(UpscaleScratchW, nativeW);
        UpscaleScratchH = std::max(UpscaleScratchH, nativeH);
        glBindTexture(GL_TEXTURE_2D, UpscaleScratchTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, UpscaleScratchW, UpscaleScratchH, 0,
            GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, UpscaleScratchTex);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, nativeW, nativeH,
        GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, rawDecodedData);

    glUseProgram(UpscaleShader);
    glUniform2i(UpscaleShaderSrcSizeULoc, (GLint)nativeW, (GLint)nativeH);
    glUniform1f(UpscaleShaderStrengthULoc, sharpenStrength);
    glUniform1f(UpscaleShaderSatULoc, satFactor);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, UpscaleFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, destArray, 0, (GLint)destLayer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, (GLsizei)(nativeW * 4), (GLsizei)(nativeH * 4));
    glBindVertexArray(UpscaleVAO);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // restore
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevFBO);
    glUseProgram((GLuint)prevProgram);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindVertexArray((GLuint)prevVAO);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexBinding);
    glActiveTexture((GLenum)prevActiveTex);

    return true;
}

}