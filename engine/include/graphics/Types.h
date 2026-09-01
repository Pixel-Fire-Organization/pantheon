#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Engine-native graphics types.
//
// These replace the type definitions the engine previously pulled in from
// <raylib.h>. Field names and layouts intentionally mirror raylib's so that
// existing consumers (scripting camera bindings, the resource manager, the
// renderers, input) compile unchanged; only code that called raylib *functions*
// was rewritten. Fonts / sound types are intentionally NOT provided — those
// paths were dropped when raylib was removed.
// ---------------------------------------------------------------------------

// --- Math ---------------------------------------------------------------

// Vector2, 2 components
typedef struct Vector2
{
    float x;
    float y;
} Vector2;

// Vector3, 3 components
typedef struct Vector3
{
    float x;
    float y;
    float z;
} Vector3;

// Vector4, 4 components
typedef struct Vector4
{
    float x;
    float y;
    float z;
    float w;
} Vector4;

// Quaternion, 4 components (Vector4 alias)
typedef Vector4 Quaternion;

// Matrix, 4x4 components, column major, OpenGL style, right-handed
typedef struct Matrix
{
    float m0, m4, m8, m12;
    float m1, m5, m9, m13;
    float m2, m6, m10, m14;
    float m3, m7, m11, m15;
} Matrix;

// Color, 4 components, R8G8B8A8 (32bit)
typedef struct Color
{
    unsigned char r, g, b, a;
} Color;

// --- Texture pixel formats ---------------------------------------------

// Uploadable pixel formats. Cooked textures carry one of these; a backend
// maps it onto whatever its hardware actually stores.
enum class PixelFormat : uint8_t
{
    RGBA32, // 32-bit R8G8B8A8
    RGBA16, // 16-bit R5G5B5A1
    PAL8, // 8-bit indexed + 256-entry R8G8B8A8 CLUT
};

/// How a texture is sampled. Fixed at upload and immutable afterwards, because
/// a backend bakes it into the sampler or the texture register.
enum class TextureFilter : uint8_t
{
    Linear = 0,
    Nearest,

    Count
};

// Maximum mip levels a texture may carry (level 0 + up to 6 downsamples).
#define TEX_MAX_MIP_LEVELS 7

// One texture ready for upload: level-0..N pixel pointers, dimensions, pixel
// format, and (PAL8 only) a 256-entry linear R8G8B8A8 CLUT. Pointers are into
// the decoded payload and must stay valid until UploadTexture returns.
typedef struct TextureUpload
{
    const void* levelPtr[TEX_MAX_MIP_LEVELS]; // per-level pixel pointers; [0] required
    uint8_t mipCount; // 1..TEX_MAX_MIP_LEVELS
    int width; // level-0 width
    int height; // level-0 height
    PixelFormat format;
    TextureFilter filter; // Linear unless the asset asked otherwise
    const void* clut; // 256 x u32 R8G8B8A8, null unless PAL8
} TextureUpload;

/// One screen-space, axis-aligned quad handed to a backend.
///
/// Positions and sizes are whole pixels in framebuffer space, origin top-left.
/// Texture coordinates are normalised across the full unsigned range and are
/// read only when `texture` is non-zero; a zero texture is a solid colour fill.
struct Quad2D
{
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t texture; // backend handle as returned by UploadTexture; 0 = solid
    uint16_t u0;
    uint16_t v0;
    uint16_t u1;
    uint16_t v1;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

// --- Camera ------------------------------------------------------------

#define CAMERA_PERSPECTIVE 0
#define CAMERA_ORTHOGRAPHIC 1

// Camera3D, defines a 3D camera used to render the scene.
typedef struct Camera3D
{
    Vector3 position; // camera position
    Vector3 target; // camera look-at target
    Vector3 up; // camera up vector (rotation over its axis)
    float fovy; // field-of-view aperture in Y (degrees)
    int projection; // CAMERA_PERSPECTIVE or CAMERA_ORTHOGRAPHIC
} Camera3D;

// Camera2D, defines a 2D camera used to render the UI / HUD.
typedef struct Camera2D
{
    Vector2 offset; // camera offset (displacement from target)
    Vector2 target; // camera target (rotation/zoom origin)
    float rotation; // camera rotation in degrees
    float zoom; // camera zoom (scaling), 1.0f by default
} Camera2D;

// --- Texture / Image ----------------------------------------------------

// A texture resident on the graphics device. `id` is a backend-defined handle;
// 0 means "not uploaded / invalid" on every backend.
typedef struct Texture2D
{
    uint32_t id; // backend handle; 0 = invalid
    int width; // texel width
    int height; // texel height
    int format; // PixelFormat value the texture was uploaded with
} Texture2D;

// Image, a decoded texture living in main RAM. Produced by the TIM2 parser and
// consumed by the renderer's UploadTexture; short-lived (freed after upload).
typedef struct Image
{
    void* data; // 16-byte-aligned pixel data in main RAM
    int width;
    int height;
    int format; // PixelFormat value
} Image;

// --- Mesh / Material / Model -------------------------------------------

// Mesh primitive topology.
#define MESH_TOPOLOGY_LIST 0
#define MESH_TOPOLOGY_STRIP 1

// Mesh, separated (stride-0) triangle arrays consumed directly by every
// backend. `indices` is always null for baked models but retained so existing
// "indices != nullptr → unsupported" guards remain valid. Positions are
// `vertexComponents` floats each (3 for primitives and legacy v1 models, 4 for
// baked v2 — a 16-byte stride a vector transform path can consume in place).
typedef struct Mesh
{
    int vertexCount; // number of vertices (list: triangleCount*3; strip: incl. degenerates)
    float* vertices; // vertexComponents floats per vertex
    float* normals; // 3 floats per vertex (may be null)
    float* texcoords; // 2 floats per vertex (may be null)
    unsigned short* indices; // always null for baked models
    Vector3 boundsCenter; // object-space bounding-sphere center
    float boundsRadius; // object-space bounding-sphere radius
    unsigned char topology; // MESH_TOPOLOGY_LIST or MESH_TOPOLOGY_STRIP
    unsigned char vertexComponents; // floats per position (3 or 4)
} Mesh;

#define MATERIAL_MAP_DIFFUSE 0
#define MAX_MATERIAL_MAPS 1

// A single material texture map slot. `textureResourceId` is the resource
// handle of the diffuse texture (-1 if none); because a model's texture
// dependencies stream in asynchronously, renderers resolve it to a live
// Texture2D at draw time rather than caching it here.
typedef struct MaterialMap
{
    Texture2D texture; // optional cached texture (may be zeroed; resolve via id)
    int32_t textureResourceId;
} MaterialMap;

// Material, only the diffuse map is used on the PS2 fixed-function pipeline.
typedef struct Material
{
    MaterialMap maps[MAX_MATERIAL_MAPS];
} Material;

// Model, a collection of unindexed meshes plus their materials.
typedef struct Model
{
    int meshCount; // number of meshes
    int materialCount; // number of materials
    Mesh* meshes; // meshes array
    Material* materials; // materials array
    int* meshMaterial; // material index per mesh (may be null → material 0)
    Vector3 boundsCenter; // object-space bounding sphere over all meshes
    float boundsRadius;
} Model;
