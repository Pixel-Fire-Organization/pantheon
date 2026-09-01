#pragma once

// Platform-neutral startup and frame loop. Each platform's Entry.cpp supplies
// the OS entry symbol (main, WinMain, ...) and immediately calls this; nothing
// platform-specific lives above it.
//
// The game never sees this: it supplies GameInit()/GameUpdate(dt) via GameAPI.h.
int Engine_Main(int argc, char** argv);
