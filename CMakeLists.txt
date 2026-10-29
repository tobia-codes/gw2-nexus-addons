cmake_minimum_required(VERSION 3.20)

option(TRAILPACK_HOST_TESTS "Build native host test executable (disables MinGW cross-compile)" OFF)

if(NOT TRAILPACK_HOST_TESTS)
    # Cross-compilation setup for Windows on Linux (must be before project())
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
    set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
endif()

project(TrailpackMemo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT TRAILPACK_HOST_TESTS)
    # ── DLL target (MinGW cross-compile) ─────────────────────────────────────

    set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
    set(CMAKE_SHARED_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -DWIN32_LEAN_AND_MEAN")

    include_directories(${CMAKE_SOURCE_DIR}/include)
    include_directories(${CMAKE_SOURCE_DIR}/include/nlohmann)
    include_directories(${CMAKE_SOURCE_DIR}/lib)
    include_directories(${CMAKE_SOURCE_DIR}/lib/imgui)
    include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/backends)
    include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/misc/cpp)
    include_directories(${CMAKE_SOURCE_DIR}/public)
    include_directories(${CMAKE_SOURCE_DIR}/src)

    set(IMGUI_SOURCES
        lib/imgui/imgui.cpp
        lib/imgui/imgui_draw.cpp
        lib/imgui/imgui_tables.cpp
        lib/imgui/imgui_widgets.cpp
        lib/imgui/misc/cpp/imgui_stdlib.cpp
    )

    set(SOURCES
        src/dllmain.cpp
        src/Theme.cpp
        src/NotesStore.cpp
        src/NotesWindow.cpp
        src/Markdown.cpp
        src/MarkdownRender.cpp
        src/MarkdownEdit.cpp
        src/chat/ChatLinks.cpp
        src/ChipResolver.cpp
        src/ChipColor.cpp
        src/ChipCodec.cpp
        src/ChipTextEdit.cpp
        src/ChipRich.cpp
        src/DecoderClient.cpp
        src/PieUiClient.cpp
        src/IconUrl.cpp
        src/IconCache.cpp
        ${IMGUI_SOURCES}
    )

    add_library(TrailpackMemo SHARED ${SOURCES})
    set_target_properties(TrailpackMemo PROPERTIES SUFFIX ".dll" PREFIX "")

    target_link_libraries(TrailpackMemo gdi32 wininet shell32 comdlg32)
    target_link_options(TrailpackMemo PRIVATE
        -Wl,--out-implib,TrailpackMemo.lib
        -Wl,--strip-all
    )
    target_sources(TrailpackMemo PRIVATE ${CMAKE_SOURCE_DIR}/TrailpackMemo.def)

    set_target_properties(TrailpackMemo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

else()
    # ── Native host-test target ───────────────────────────────────────────────
    # Pure C++ units only — no Nexus, no ImGui, no Win32

    set(HOST_TEST_SOURCES
        tests/host_tests_main.cpp
        src/NotesStore.cpp
        src/Markdown.cpp
        src/MarkdownEdit.cpp
        src/chat/ChatLinks.cpp
        src/ChipResolver.cpp
        src/ChipColor.cpp
        src/ChipCodec.cpp
        src/IconUrl.cpp
    )

    add_executable(trailpack_host_tests ${HOST_TEST_SOURCES})

    target_include_directories(trailpack_host_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/public
    )

    set_target_properties(trailpack_host_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

endif()
