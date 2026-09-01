#include "core/EngineMain.h"

// Win32 entry point. A console subsystem executable, so plain main() rather than
// WinMain: the engine logs to stdout and a console is what a developer wants
// while the renderer is still headless. A WinMain variant lands with the window.
int main(int argc, char** argv) { return Engine_Main(argc, argv); }
