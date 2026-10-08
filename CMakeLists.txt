
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
    src/cpu.c
    src/memory.c
)

# Headers accessible to applications and tests.
target_include_directories(gbcore PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gbcore PUBLIC gb_options)

# ------------------------------------------------------------
# 6. Main Emulator Executable
# ------------------------------------------------------------

add_executable(gb
    src/emulator.c
)

target_link_libraries(gb PRIVATE gbcore)

# ------------------------------------------------------------
# 7. Automated Tests
# ------------------------------------------------------------

if(BUILD_TESTING)

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
