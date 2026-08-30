#ifndef GPU3D_TEXCACHEOPENGL
#define GPU3D_TEXCACHEOPENGL

#include "GPU3D_Texcache.h"
#include "OpenGLSupport.h"

namespace melonDS
{

template <typename, typename>
class Texcache;

class TexcacheOpenGLLoader
{
public:
    TexcacheOpenGLLoader(bool compute) : IsCompute(compute) {}

    GLuint GenerateTexture(u32 width, u32 height, u32 layers);
    void UploadTexture(GLuint handle, u32 width, u32 height, u32 layer, void* data);
    void DeleteTexture(GLuint handle);

    // GPU replacement for the CPU EagleUpscale4x + TextureSharpenAndSaturate
    // pair in GPU3D_Texcache.h - see 3DTexUpscaleFS.glsl for the actual
    // math (a direct integer-texture port of the same CPU logic). Reads
    // `nativeW`x`nativeH` raw decoded texel data from `rawDecodedData`
    // (same layout/format GPU3D_Texcache.h's DecodingBuffer already
    // uses - GL_RGBA_INTEGER/GL_UNSIGNED_BYTE), and renders the
    // upscaled+sharpened+saturated 4x result directly into
    // `destLayer` of the `destArray` texture array (no separate CPU
    // upload of the final result needed - the shader writes straight
    // into the destination).
    //
    // Returns false (does nothing) if the compute-path loader is in
    // use, or if the shader failed to compile - GetTexture falls back
    // to the CPU pipeline in either case. Only the classic (non-
    // compute) OpenGL path has been wired up here; the compute-shader
    // texture-array lifecycle wasn't something this could be verified
    // safely against without a GPU to test on, so it deliberately keeps
    // using the CPU path rather than guessing at compute-specific
    // synchronization requirements.
    bool GPUUpscaleSharpenSaturate(GLuint destArray, u32 nativeW, u32 nativeH, u32 destLayer,
                                    const void* rawDecodedData, float sharpenStrength, float satFactor);

private:
    bool IsCompute;

    // Lazily initialised on first GPUUpscaleSharpenSaturate() call.
    bool UpscaleShaderInitAttempted = false;
    bool UpscaleShaderReady = false;
    GLuint UpscaleShader = 0;
    GLint UpscaleShaderSrcSizeULoc = -1;
    GLint UpscaleShaderStrengthULoc = -1;
    GLint UpscaleShaderSatULoc = -1;
    GLuint UpscaleFBO = 0;
    GLuint UpscaleScratchTex = 0;
    u32 UpscaleScratchW = 0, UpscaleScratchH = 0;
};

using TexcacheOpenGL = Texcache<TexcacheOpenGLLoader, GLuint>;

}

#endif