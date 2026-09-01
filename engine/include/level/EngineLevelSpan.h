#pragma once

#include <cstddef>
#include <cstdint>

/// Whether a byte range an on-disc level blob declares lies inside that blob.
///
/// The offset and the length are widened before they are added, so a pair
/// chosen to wrap a 32-bit size_t cannot pass the comparison.
/// @param offset Byte offset from the start of the blob.
/// @param bytes Length of the range.
/// @param blobSize Size of the blob the range must fit inside.
/// @return Whether the whole range is inside the blob.
inline bool Level_SpanFits(uint64_t offset, uint64_t bytes, size_t blobSize) { return offset + bytes <= static_cast<uint64_t>(blobSize); }

/// @param offset Byte offset from the start of the blob.
/// @param alignment Required alignment in bytes; a power of two.
/// @return Whether a struct view cast at that offset would be correctly
///         aligned for the target.
inline bool Level_OffsetAligned(uint64_t offset, uint64_t alignment) { return (offset & (alignment - 1u)) == 0u; }
