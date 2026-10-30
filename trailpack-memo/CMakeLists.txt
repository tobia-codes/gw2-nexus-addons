# Trailpack Memo — built from the repository root (see ../CMakeLists.txt).

if(NOT GW2_HOST_TESTS)
    # ── DLL target (MinGW cross-compile) ─────────────────────────────────────

    add_library(TrailpackMemo SHARED
        src/dllmain.cpp
        src/ui/Theme.cpp
        src/ui/FontScale.cpp
        src/notes/NotesStore.cpp
        src/core/I18n.cpp
        src/core/Reset.cpp
        src/core/Settings.cpp
        src/core/Backup.cpp
        src/ui/FileDialog.cpp
        src/events/Events.cpp
        src/notes/NotesWindow.cpp
        src/notes/NotesList.cpp
        src/notes/NotesDetail.cpp
        src/notes/NotesModals.cpp
        src/notes/NotesImportExport.cpp
        src/notes/NotesUi.cpp
        src/ui/HelpWindow.cpp
        src/markdown/Markdown.cpp
        src/markdown/MarkdownRender.cpp
        src/markdown/MarkdownEdit.cpp
        src/chips/ChipResolver.cpp
        src/chips/ChipColor.cpp
        src/chips/ChipCodec.cpp
        src/chips/ChipTextEdit.cpp
        src/chips/ChipRich.cpp
        src/chips/EventChip.cpp
        src/events/EventPicker.cpp
        src/integrations/CoreClient.cpp
        src/chips/IconUrl.cpp
        src/chips/IconCache.cpp
        TrailpackMemo.def
    )
    set_target_properties(TrailpackMemo PROPERTIES SUFFIX ".dll" PREFIX "")
    trailpack_release(TrailpackMemo)

    target_include_directories(TrailpackMemo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../trailpack-core/public   # Trailpack Core ABI header
    )
    target_link_libraries(TrailpackMemo PRIVATE trailpack_chatlinks trailpack_files trailpack_ui gw2_imgui gw2_headers gdi32 shell32 comdlg32)
    target_link_options(TrailpackMemo PRIVATE
        -Wl,--out-implib,TrailpackMemo.lib
        -Wl,--strip-all
    )

    # All addon DLLs land side by side in <build>/ for easy copying.
    set_target_properties(TrailpackMemo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

else()
    # ── Native host-test target ───────────────────────────────────────────────
    # Pure C++ units only — no Nexus, no ImGui, no Win32

    add_executable(trailpack_host_tests
        tests/host_tests_main.cpp
        src/notes/NotesStore.cpp
        src/core/I18n.cpp
        src/core/Reset.cpp
        src/core/Settings.cpp
        src/core/Backup.cpp
        src/events/Events.cpp
        src/markdown/Markdown.cpp
        src/markdown/MarkdownEdit.cpp
        src/chips/ChipResolver.cpp
        src/chips/ChipColor.cpp
        src/chips/ChipCodec.cpp
        src/chips/IconUrl.cpp
    )
    target_include_directories(trailpack_host_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../trailpack-core/public   # Trailpack Core ABI header
    )
    target_link_libraries(trailpack_host_tests PRIVATE trailpack_chatlinks trailpack_files gw2_headers)
    set_target_properties(trailpack_host_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(NAME trailpack_memo COMMAND trailpack_host_tests)
endif()
