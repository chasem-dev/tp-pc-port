cmake_minimum_required(VERSION 3.16)
project(tp_pc CXX C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Do NOT set a default build type. Any optimization level (-O1, -O2) exposes
# undefined behavior in decomp code causing crashes. An empty build type gives
# GCC's default (-O0) which works correctly.

# Options
option(PC_CONSOLE "Show console window on Windows" OFF)

# =============================================================================
# Platform info
# =============================================================================
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "Building as 64-bit (pointer size = 8 bytes)")
else()
    message(STATUS "Building as 32-bit (pointer size = 4 bytes)")
endif()

set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_EXTENSIONS ON)

# =============================================================================
# Compiler definitions
# =============================================================================
add_compile_definitions(
    TARGET_PC
    VERSION=0       # GCN USA
    NDEBUG
)
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

# Common compiler flags
if(MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(
        -fno-strict-aliasing
        -fwrapv
        -fcommon    # Allow duplicate global definitions (REL module boilerplate)
    )
endif()

# =============================================================================
# Include paths
# =============================================================================
set(DECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

include_directories(
    ${DECOMP_ROOT}/include
    ${DECOMP_ROOT}/src
    ${DECOMP_ROOT}/libs/JSystem/include
    ${DECOMP_ROOT}/libs/dolphin/include
    ${DECOMP_ROOT}/libs/dolphin/include/dolphin
    ${DECOMP_ROOT}/libs/revolution/include
    ${DECOMP_ROOT}/assets/DZDE01
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glad/include
)

# =============================================================================
# Find SDL2
# =============================================================================
if(WIN32)
    set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/SDL2/cmake" CACHE PATH "SDL2 cmake dir")
endif()
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
endif()
if(NOT SDL2_FOUND)
    message(FATAL_ERROR
        "SDL2 not found!\n"
        "  macOS:         brew install sdl2\n"
        "  Linux:         sudo apt install libsdl2-dev\n"
        "  Windows/MSYS2: pacman -S mingw-w64-x86_64-SDL2\n")
endif()
include_directories(${SDL2_INCLUDE_DIRS})
message(STATUS "SDL2 include: ${SDL2_INCLUDE_DIRS}")
message(STATUS "SDL2 libraries: ${SDL2_LIBRARIES}")

# =============================================================================
# GLAD (OpenGL 3.3 loader)
# =============================================================================
add_library(glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glad/src/gl.c
)
target_include_directories(glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glad/include
)

# =============================================================================
# Collect game source files
# =============================================================================
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    "${DECOMP_ROOT}/src/*.cpp"
    "${DECOMP_ROOT}/src/*.c"
)

file(GLOB_RECURSE JSYSTEM_SOURCES CONFIGURE_DEPENDS
    "${DECOMP_ROOT}/libs/JSystem/src/*.cpp"
)

# Exclude Dolphin SDK sources (replaced by pc_*.cpp stubs)
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/libs/.*")

# Exclude platform-specific directories we don't need
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/src/REL/.*")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/src/odemuexi2/.*")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/src/odenotstub/.*")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/src/NdevExi2A/.*")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/src/amcstubs/.*")

# Exclude files with switch/case cross-initialization that GCC/Clang reject in C++
# (MWCC allows jumping past variable initialization, standard C++ does not)
# Exclude .cpp files that are #included from other .cpp files (unity build pattern)
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_a_npc2\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_a_npc4\\.cpp$")

list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_a_mg_rod\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_a_movie_player\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_cursor_mng\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_event_debug\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_home_button\\.cpp$")

# Exclude JStudio editor/previewer wrappers (deep dependency chains into empty stub source files)
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_jcam_editor\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/d_jpreviewer\\.cpp$")

# Exclude JSystem debug HostIO files (use JORReflexible debug members not available with NDEBUG)
list(FILTER JSYSTEM_SOURCES EXCLUDE REGEX ".*/JAHostIO/.*")
list(FILTER JSYSTEM_SOURCES EXCLUDE REGEX ".*/JHostIO/.*")

# Exclude JAWExtSystem (debug audio tool windows) -- stubs provided in pc_stubs.cpp
list(FILTER JSYSTEM_SOURCES EXCLUDE REGEX ".*/JAWExtSystem/.*")

# =============================================================================
# PC port source files
# =============================================================================
set(PC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_gx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_gx_tev.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_gx_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_os.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_dvd.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_pad.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_card.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_vi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_audio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_aram.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_mtx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_misc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_stubs.cpp
)

# Rename game's main() to tp_main_entry() to avoid conflict with PC main()
set_source_files_properties(
    "${DECOMP_ROOT}/src/m_Do/m_Do_main.cpp"
    PROPERTIES COMPILE_DEFINITIONS "main=tp_main_entry"
)

# =============================================================================
# Per-source warning settings
# =============================================================================
# Decomp code: suppress all warnings
# -fcommon: Put uninitialized globals in common section (allows duplicates from REL modules)
# -fweak-global: Not a real flag, but -fcommon handles C globals
set(DECOMP_WARN_FLAGS "-w;-fpermissive;-Wno-return-type;-Wno-error=return-type;-Wno-narrowing;-fcommon;-fno-threadsafe-statics")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND DECOMP_WARN_FLAGS "-Wno-c99-designator" "-Wno-initializer-overrides")
endif()
set_source_files_properties(${GAME_SOURCES} ${JSYSTEM_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${DECOMP_WARN_FLAGS}"
)

# PC port code: enable warnings
set(PC_WARN_SUPPRESS "-Wno-unused-parameter;-Wno-sign-compare;-Wno-comment;-Wno-unused-function")
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND PC_WARN_SUPPRESS "-Wno-builtin-declaration-mismatch")
endif()
set_source_files_properties(${PC_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;${PC_WARN_SUPPRESS};-fpermissive"
)

# =============================================================================
# Build game code as static library for non-Apple, directly linked for Apple.
# On macOS, -force_load of a large static library triggers a dyld/CoreFoundation
# initialization crash. Compiling all sources into one target avoids this.
# =============================================================================
if(APPLE)
    add_executable(tp_pc
        ${PC_SOURCES}
        ${GAME_SOURCES}
        ${JSYSTEM_SOURCES}
    )
else()
    add_library(tp_game STATIC
        ${GAME_SOURCES}
        ${JSYSTEM_SOURCES}
    )
    add_executable(tp_pc
        ${PC_SOURCES}
    )
    target_link_libraries(tp_pc -Wl,--whole-archive tp_game -Wl,--no-whole-archive -Wl,--allow-multiple-definition)
endif()

# Link SDL2
if(TARGET SDL2::SDL2)
    target_link_libraries(tp_pc glad SDL2::SDL2)
else()
    list(FILTER SDL2_LIBRARIES EXCLUDE REGEX "SDL2main")
    target_link_libraries(tp_pc glad ${SDL2_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(tp_pc opengl32 winmm imm32 version setupapi)
elseif(APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(tp_pc OpenGL::GL "-framework Cocoa" "-framework IOKit" pthread m)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(tp_pc OpenGL::GL dl pthread m)
endif()

# Allow multiple definitions from decomp headers (functions defined in headers
# get emitted into multiple object files — MWCC handles this differently)
if(APPLE)
    target_link_options(tp_pc PRIVATE -Wl,-multiply_defined,suppress)
else()
    target_link_options(tp_pc PRIVATE -Wl,--allow-multiple-definition)
endif()

set_target_properties(tp_pc PROPERTIES
    OUTPUT_NAME "TwilightPrincess"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy shader files to output directory
add_custom_command(TARGET tp_pc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/default.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/default.frag"
        "${CMAKE_BINARY_DIR}/bin/shaders/"
    COMMENT "Copying shader files to bin/shaders/"
)

# =============================================================================
# Install rules
# =============================================================================
install(TARGETS tp_pc RUNTIME DESTINATION bin)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shaders/"
    DESTINATION bin/shaders
    FILES_MATCHING PATTERN "*.vert" PATTERN "*.frag"
)
