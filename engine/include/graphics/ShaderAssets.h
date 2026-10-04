#pragma once

#include <cstddef>
#include <cstdint>

#include "core/EngineMemory.h"

// FORMAT CONSTANTS - shared with tools/cook_shaders.py, identical everywhere.

#define SHADER_SOURCE_MAX_BYTES (256 * 1024)

#define SHADER_ASSET_DIRECTORY "shaders"

#define SHADER_GLSL_CORE_VERSION_LINE "#version 330 core\n"

/// One cooked shader source, NUL-terminated and owned through the platform allocator.
struct ShaderSource
{
    PlatformArray<char> text;
    uint32_t size;
};

/// Read one cooked shader from the shader directory beside the archive.
/// @param name Path inside the shader directory, forward slashes, e.g. "pbr.wgsl" or "legacy/pbr.vert.glsl".
/// @param out Receives the text and its length excluding the terminator; left empty on failure.
/// @return False when the file is absent, empty, past SHADER_SOURCE_MAX_BYTES, short on read or holds a NUL byte.
///         The refusal is logged naming the path and the cook target that produces the file.
bool ShaderAssets_Load(const char* name, ShaderSource* out);
