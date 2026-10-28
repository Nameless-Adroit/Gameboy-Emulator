
cmake_minimum_required(VERSION 3.20)

# ============================================================
# Game Boy Emulator - Build Configuration
# Target CPU: Sharp SM83 (Game Boy DMG)
# Language: ISO C99
# ============================================================

project(Gameboy_emulator
    VERSION 0.1.0
    DESCRIPTION "Game Boy DMG Emulator"
    LANGUAGES C
)

# ------------------------------------------------------------
# 1. C Language Configuration
# ------------------------------------------------------------

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Export compilation commands for IDEs and analysis tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Debug for single-configuration generators
# such as Ninja and Unix Makefiles.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------
# 2. Build Options
# ------------------------------------------------------------

option(GB_ENABLE_WARNINGS
    "Enable additional compiler warnings" ON)

option(GB_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# SDL is optional: gbcore and the headless gb runner never depend on it.
option(GB_ENABLE_SDL "Build the SDL2 window frontend" OFF)

option(GB_ENABLE_GRAPHICS_TRACE "Compile optional graphics pipeline instrumentation" OFF)

# CTest defines the BUILD_TESTING option.
include(CTest)

# ------------------------------------------------------------
# 3. Common Compiler Settings
# ------------------------------------------------------------

add_library(gb_options INTERFACE)

target_compile_features(gb_options INTERFACE c_std_99)

if(GB_ENABLE_WARNINGS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gb_options INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wstrict-prototypes
        )
    elseif(MSVC)
        target_compile_options(gb_options INTERFACE /W4)
    endif()
endif()

# ------------------------------------------------------------
# 4. Sanitizers (Optional)
# ------------------------------------------------------------

if(GB_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")

        target_compile_options(gb_options INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )

        target_link_options(gb_options INTERFACE
            -fsanitize=address,undefined
        )

    else()
        message(WARNING
            "Sanitizers are not configured for this compiler")
    endif()
endif()

# ------------------------------------------------------------
# 5. Emulator Core Library
# ------------------------------------------------------------

add_library(gbcore STATIC
    src/graphics_trace.c
    src/cpu.c
    src/memory.c
    src/cartridge.c
    src/timer.c
    src/ppu.c
)

# Headers accessible to applications and tests.
target_include_directories(gbcore PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gbcore PUBLIC gb_options)
if(GB_ENABLE_GRAPHICS_TRACE)
    target_compile_definitions(gbcore PUBLIC GB_GRAPHICS_TRACE=1)
endif()

# ------------------------------------------------------------
# 6. Main Emulator Executable
# ------------------------------------------------------------

add_executable(gb
    src/emulator.c
)

target_link_libraries(gb PRIVATE gbcore)

# Keep all platform presentation dependencies on a separate target. SDL 2.0.12
# provides explicit per-texture nearest-neighbor scaling control.
if(GB_ENABLE_SDL)
    find_package(SDL2 2.0.12 CONFIG REQUIRED)
    add_library(gb_sdl_frontend STATIC src/frontend/sdl_frontend.c)
    target_include_directories(gb_sdl_frontend PUBLIC ${PROJECT_SOURCE_DIR}/src/frontend)
    target_link_libraries(gb_sdl_frontend PUBLIC gbcore SDL2::SDL2)
    add_executable(gb_sdl src/frontend/main.c)
    target_link_libraries(gb_sdl PRIVATE gb_sdl_frontend)
endif()

# ------------------------------------------------------------
# 7. Automated Tests
# ------------------------------------------------------------

if(BUILD_TESTING)

    if(GB_ENABLE_GRAPHICS_TRACE)
        add_executable(graphics_trace_tests tests/test_graphics_trace.c)
        target_link_libraries(graphics_trace_tests PRIVATE gbcore)
        add_test(NAME Graphics_Trace_Tests COMMAND graphics_trace_tests
            "${PROJECT_SOURCE_DIR}/tests/diagnostic/background.gb")
        if(GB_ENABLE_SDL)
            add_test(NAME SDL_Graphics_Trace COMMAND gb_sdl
                "${PROJECT_SOURCE_DIR}/tests/diagnostic/background.gb"
                --frames 2 --graphics-trace)
            set_tests_properties(SDL_Graphics_Trace PROPERTIES
                ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
                PASS_REGULAR_EXPRESSION "SDL_PRESENT.*SDL_ARGB=FFFFFFFF"
                TIMEOUT 15)
        endif()
    endif()

    if(GB_ENABLE_SDL)
        add_executable(sdl_frontend_tests tests/test_sdl_frontend.c)
        target_link_libraries(sdl_frontend_tests PRIVATE gb_sdl_frontend)
        add_test(NAME SDL_Frontend_Tests COMMAND sdl_frontend_tests)
        add_test(NAME SDL_Diagnostic_Smoke COMMAND gb_sdl
            "${PROJECT_SOURCE_DIR}/tests/diagnostic/background.gb" --frames 2)
        set_tests_properties(SDL_Frontend_Tests SDL_Diagnostic_Smoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
            TIMEOUT 15)
        set_tests_properties(SDL_Diagnostic_Smoke PROPERTIES
            PASS_REGULAR_EXPRESSION "Presented 2 frame")
    endif()

    add_executable(bg_diagnostic_tests tests/test_bg_diagnostic.c)
    target_link_libraries(bg_diagnostic_tests PRIVATE gbcore)
    add_test(NAME BG_Diagnostic_ROM COMMAND bg_diagnostic_tests
        "${PROJECT_SOURCE_DIR}/tests/diagnostic/background.gb")

    add_executable(ppu_render_tests tests/test_ppu_render.c)
    target_link_libraries(ppu_render_tests PRIVATE gbcore)
    add_test(NAME PPU_Render_Tests COMMAND ppu_render_tests)

    add_executable(ppu_tests tests/test_ppu.c)
    target_link_libraries(ppu_tests PRIVATE gbcore)
    add_test(NAME PPU_Tests COMMAND ppu_tests)

    add_executable(timer_tests tests/test_timer.c)
    target_link_libraries(timer_tests PRIVATE gbcore)
    add_test(NAME Timer_Tests COMMAND timer_tests)

    add_executable(cartridge_tests tests/test_cartridge.c)
    target_link_libraries(cartridge_tests PRIVATE gbcore)
    add_test(NAME Cartridge_Tests COMMAND cartridge_tests)

    add_executable(cpu_tests
        tests/test_cpu.c
    )

    target_link_libraries(cpu_tests PRIVATE gbcore)

    add_test(
        NAME CPU_Instruction_Tests
        COMMAND cpu_tests
    )

    # Register additional tests only when their source
    # files exist. This allows gradual development.

    if(EXISTS "${PROJECT_SOURCE_DIR}/tests/test_memory.c")

        add_executable(memory_tests
            tests/test_memory.c
        )

        target_link_libraries(memory_tests PRIVATE gbcore)

        add_test(
            NAME Memory_Bus_Tests
            COMMAND memory_tests
        )

    endif()

endif()

# ------------------------------------------------------------
# 8. Build Information
# ------------------------------------------------------------

message(STATUS "Game Boy Emulator ${PROJECT_VERSION}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Testing enabled: ${BUILD_TESTING}")
message(STATUS "Sanitizers enabled: ${GB_ENABLE_SANITIZERS}")
