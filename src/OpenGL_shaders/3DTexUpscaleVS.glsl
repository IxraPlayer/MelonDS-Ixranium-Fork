#version 140

// Self-contained fullscreen triangle - generated entirely from
// gl_VertexID, no vertex buffer needed. Deliberately not sharing the
// fullscreen-quad VBO the 2D renderer's shaders use (see
// 2DBGUpscaleVS.glsl): this pass is invoked from the 3D texture cache
// (GPU3D_Texcache.h/.cpp), a completely different part of the renderer
// with no guarantee about what VAO/VBO is currently bound, so it must
// not depend on any external vertex state.
void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
