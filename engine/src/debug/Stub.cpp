#include "debug/EngineTestbed.h"

// Compiled instead of the testbed in a release build, where none of the scenes,
// their strings or the catalogue exist. See docs/subsystems/TESTBED.md.

void Engine_Testbed_RequestSubsystems() {}

bool Engine_Testbed_Init() { return false; }

void Engine_Testbed_Shutdown() {}

void Engine_Testbed_Update(float dt) { (void)dt; }

bool Engine_Testbed_IsOpen() { return false; }
