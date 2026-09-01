#pragma once
#include <cstdint>

#include "PlatformConstants.h"
#include "Types.h"

enum class Primitive3D : uint8_t
{
    Cube,
    Sphere,
    Cylinder,
};

typedef int8_t CameraID;

struct Color3
{
    float r;
    float g;
    float b;
};

// What Renderer::RenderToImage3D draws: a loaded model (modelId >= 0) or a
// primitive shape, never both -- the same one-or-the-other rule
// AddModelToDrawList/AddPrimitiveToDrawList already express as separate
// calls, folded into one struct so the image-target path needs a single
// entry point rather than mirroring every existing overload. The subject is
// always drawn at the origin; frame it with the camera rather than moving it.
struct Renderable3D
{
    int32_t modelId; // >= 0 selects the model; the primitive fields are ignored
    Primitive3D primitive;
    Color3 color;
    int32_t textureId; // primitive only, -1 = untextured; ignored for a model
    Vector3 rotation;
    Vector3 scale;
};

class Transform3D final
{
    float posX = 0;
    float posY = 0;
    float posZ = 0;
    float rotX = 0;
    float rotY = 0;
    float rotZ = 0;
    float scaX = 1;
    float scaY = 1;
    float scaZ = 1;

public:
    Transform3D();
    Transform3D(const Vector3& position, const Vector3& rotation, const Vector3& scale);

    Vector3 GetPosition() const;
    Vector3 GetRotation() const;
    Vector3 GetScale() const;

    void SetPosition(const Vector3&);
    void SetRotation(const Vector3&);
    void SetScale(const Vector3&);
};

enum class RendererType : uint8_t
{
    Null, // headless - accepts every call, draws nothing
    Ps2Gl, // PS2: ps2gl (a GL 1.1 subset over the GS), NOT desktop OpenGL
    GifTag, // PS2: direct GS packets via packet2/draw
    OpenGl, // desktop OpenGL: Win32 2.1 / 3.3 / 4.x, nx 4.3 core through Mesa
    WebGpu, // desktop WebGPU (wgpu-native)
    Gxm, // Vita: sceGxm packets
    VitaGl, // Vita: vitaGL (a fixed-function subset over sceGxm), NOT desktop OpenGL
    Gu, // PSP: sceGu display lists
    PspGl, // PSP: pspgl (a fixed-function subset over the same hardware), NOT desktop OpenGL
    Deko3d // nx: deko3d command lists
};
