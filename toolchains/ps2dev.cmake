set(CMAKE_SYSTEM_NAME Generic)

set(ENGINE_TOOLCHAIN_ID "ps2dev" CACHE INTERNAL "Active toolchain")

if("$ENV{PS2DEV}" STREQUAL "")
    message(FATAL_ERROR "PS2DEV environment variable is not set!")
endif()

set(PS2DEV $ENV{PS2DEV})
set(PS2SDK ${PS2DEV}/ps2sdk)

# Set the compiler tools using find_program so it natively resolves .exe on Windows
find_program(CMAKE_C_COMPILER NAMES mips64r5900el-ps2-elf-gcc ee-gcc PATHS "${PS2DEV}/ee/bin" REQUIRED NO_DEFAULT_PATH)
find_program(CMAKE_CXX_COMPILER NAMES mips64r5900el-ps2-elf-g++ ee-g++ PATHS "${PS2DEV}/ee/bin" REQUIRED NO_DEFAULT_PATH)
find_program(CMAKE_ASM_COMPILER NAMES mips64r5900el-ps2-elf-as ee-as PATHS "${PS2DEV}/ee/bin" REQUIRED NO_DEFAULT_PATH)
find_program(CMAKE_AR NAMES mips64r5900el-ps2-elf-ar ee-ar PATHS "${PS2DEV}/ee/bin" REQUIRED NO_DEFAULT_PATH)
find_program(CMAKE_RANLIB NAMES mips64r5900el-ps2-elf-ranlib ee-ranlib PATHS "${PS2DEV}/ee/bin" REQUIRED NO_DEFAULT_PATH)

set(CMAKE_FIND_ROOT_PATH ${PS2DEV} ${PS2SDK})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Compile Flags
set(_PS2_COMMON_FLAGS "-I${PS2SDK}/ee/include -I${PS2SDK}/common/include -I${PS2SDK}/ports/include -D_EE -G0 -O2 -Wall -gdwarf-2 -gz")
set(CMAKE_C_FLAGS "${_PS2_COMMON_FLAGS} -std=c99 -Wno-int-conversion" CACHE STRING "C Flags" FORCE)
set(CMAKE_CXX_FLAGS "${_PS2_COMMON_FLAGS} -std=c++11 -fno-exceptions -fno-rtti" CACHE STRING "CXX Flags" FORCE)

# Startup linker file and LDFLAGS for Executables
set(CMAKE_EXE_LINKER_FLAGS "-L${PS2SDK}/ee/lib -L${PS2SDK}/ports/lib -Wl,-zmax-page-size=128 -T${PS2SDK}/ee/startup/linkfile" CACHE STRING "Exec Link Flags" FORCE)
