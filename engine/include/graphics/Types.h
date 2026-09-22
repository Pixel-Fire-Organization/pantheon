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
    float* colors; // 4 floats per vertex, RGBA baked colour (may be null); PSEC only, see MaterialFormat.h
    unsigned short* indices; // always null for baked models
    Vector3 boundsCenter; // object-space bounding-sphere center
    float boundsRadius; // object-space bounding-sphere radius
    unsigned char topology; // MESH_TOPOLOGY_LIST or MESH_TOPOLOGY_STRIP
    unsigned char vertexComponents; // floats per position (3 or 4)
} Mesh;

// A material names a shader type plus a fixed, generic set of parameter
// slots whose MEANING depends on that shader type -- see
// docs/formats/MATERIAL_FORMAT.md. This is what lets a future shader type
// (e.g. water) add its own parameters without widening this struct: it
// defines its own mapping over the same slot arrays, rather than the struct
// growing a field per shader type that ever existed.
#define MATERIAL_MAX_FLOAT_PARAMS 8
#define MATERIAL_MAX_COLOR_PARAMS 4
#define MATERIAL_MAX_TEXTURE_SLOTS 4

// Same "grows by appending a value, exhaustive -Wswitch, no default:" idiom
// as PlatformCapability/RendererId.
enum class MaterialShaderType : uint8_t
{
    PbrStandard = 0,
    Count
};

// PbrStandard's slot mapping -- the single source of truth mirrored by
// tools/cook_assets.py and every backend's shading code.
#define MATERIAL_PBR_FLOAT_METALLIC 0
#define MATERIAL_PBR_FLOAT_ROUGHNESS 1
#define MATERIAL_PBR_FLOAT_NORMAL_SCALE 2
#define MATERIAL_PBR_FLOAT_ALPHA_CUTOFF 3

#define MATERIAL_PBR_COLOR_BASE 0 // baseColorFactor, RGBA
#define MATERIAL_PBR_COLOR_EMISSIVE 1 // emissiveFactor, RGB (alpha unused)

#define MATERIAL_PBR_TEX_ALBEDO 0
#define MATERIAL_PBR_TEX_NORMAL 1
#define MATERIAL_PBR_TEX_ORM 2 // packed occlusion/roughness/metallic

#define MATERIAL_FLAG_ALPHA_MASK (1u << 0)
#define MATERIAL_FLAG_ALPHA_BLEND (1u << 1)
#define MATERIAL_FLAG_DOUBLE_SIDED (1u << 2)

// A resolved material: parameter slots plus resolved texture resource
// handles (-1 = none), ready to sample/bind at draw time. Populated by
// Material_LoadBaked (MaterialFormat.h).
typedef struct Material
{
    uint8_t shaderType; // MaterialShaderType
    uint8_t flags; // MATERIAL_FLAG_* bitmask
    float floatParams[MATERIAL_MAX_FLOAT_PARAMS];
    float colorParams[MATERIAL_MAX_COLOR_PARAMS][4]; // RGBA
    int32_t textureRefs[MATERIAL_MAX_TEXTURE_SLOTS]; // resource handles, -1 = none
} Material;

// Model, a collection of unindexed meshes plus the materials they reference.
typedef struct Model
{
    int meshCount; // number of meshes
    int materialCount; // number of materials this model references
    Mesh* meshes; // meshes array
    int32_t* materials; // RES_MATERIAL resource handles, one per referenced material
    int* meshMaterial; // index into materials[] per mesh (may be null → material 0)
    Vector3 boundsCenter; // object-space bounding sphere over all meshes
    float boundsRadius;
} Model;
