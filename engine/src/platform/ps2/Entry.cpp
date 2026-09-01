#include "core/EngineMain.h"

// PS2 entry point. argv[0] is the boot path the console launched us with
// ("cdrom0:\MAIN.ELF;1", "host:main.elf"), which the platform turns into its
// resource token.
int main(int argc, char** argv) { return Engine_Main(argc, argv); }
