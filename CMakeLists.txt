cmake_minimum_required(VERSION 3.20)

# gw2-nexus-addons — monorepo of Raidcore Nexus addons for Guild Wars 2.
#
#   cmake -S . -B build && cmake --build build                    # all addon DLLs (MinGW cross-compile)
#   cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host && ctest --test-dir build-host
#
# Each addon lives in its own folder and is added below; shared third-party code
# (ImGui, nlohmann/json, Nexus API header) lives in shared/.

option(GW2_HOST_TESTS "Build native host test executables (disables MinGW cross-compile)" OFF)

if(NOT GW2_HOST_TESTS)
    # Cross-compilation setup for Windows on Linux (must be before project())
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
    set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
endif()

project(GW2NexusAddons CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT GW2_HOST_TESTS)
    set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
    set(CMAKE_SHARED_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -DWIN32_LEAN_AND_MEAN")
else()
    enable_testing()
endif()

add_subdirectory(shared)
add_subdirectory(libs)

# ── Addons ────────────────────────────────────────────────────────────────
add_subdirectory(trailpack-memo)
add_subdirectory(trailpack-core)
