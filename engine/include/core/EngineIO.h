#pragma once

#include <stddef.h>

// FORMAT CONSTANT - identical on every platform, part of the .ps2a header size.
#define IO_FILE_MAX_PATH 256

typedef void (*IO_Callback)(const void* data, size_t size, void* userData);

bool Engine_IO_Init();

bool Engine_IO_ReadAsync(const char* filepath, IO_Callback callback, void* userData);

void Engine_IO_Update();

// Block until every queued read has completed and every callback has been
// dispatched. Call before tearing down anything a read could write into: the
// worker writes into the shared buffer and callbacks write into resource
// slots, so resetting under one corrupts both. Returns false if requests were
// still outstanding when the wait gave up, which is a bug rather than a delay.
bool Engine_IO_Drain();

void Engine_IO_Shutdown();

void Engine_IO_AcquireFileAccess();
void Engine_IO_ReleaseFileAccess();
