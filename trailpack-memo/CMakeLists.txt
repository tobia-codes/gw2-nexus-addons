# Trailpack Memo — built from the repository root (see ../CMakeLists.txt).

if(NOT GW2_HOST_TESTS)
    # ── DLL target (MinGW cross-compile) ─────────────────────────────────────

    add_library(TrailpackMemo SHARED
        src/dllmain.cpp
        src/Theme.cpp
        src/FontScale.cpp
        src/NotesStore.cpp
        src/I18n.cpp
        src/Reset.cpp
        src/SafeFile.cpp
        src/Settings.cpp
        src/Backup.cpp
        src/FileDialog.cpp
        src/events/EventData.cpp
        src/Events.cpp
        src/NotesWindow.cpp
        src/NotesList.cpp
        src/NotesDetail.cpp
        src/NotesModals.cpp
        src/NotesImportExport.cpp
        src/NotesUi.cpp
        src/HelpWindow.cpp
        src/Markdown.cpp
        src/MarkdownRender.cpp
        src/MarkdownEdit.cpp
        src/chat/ChatLinks.cpp
        src/ChipResolver.cpp
        src/ChipColor.cpp
        src/ChipCodec.cpp
        src/ChipTextEdit.cpp
        src/ChipRich.cpp
        src/EventChip.cpp
        src/EventPicker.cpp
        src/DecoderClient.cpp
        src/PieUiClient.cpp
        src/IconUrl.cpp
        src/IconCache.cpp
        TrailpackMemo.def
    )
    set_target_properties(TrailpackMemo PROPERTIES SUFFIX ".dll" PREFIX "")

    target_include_directories(TrailpackMemo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/public
    )
    target_link_libraries(TrailpackMemo PRIVATE gw2_imgui gw2_headers gdi32 wininet shell32 comdlg32)
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
        src/NotesStore.cpp
        src/I18n.cpp
        src/Reset.cpp
        src/SafeFile.cpp
        src/Settings.cpp
        src/Backup.cpp
        src/events/EventData.cpp
        src/Events.cpp
        src/Markdown.cpp
        src/MarkdownEdit.cpp
        src/chat/ChatLinks.cpp
        src/ChipResolver.cpp
        src/ChipColor.cpp
        src/ChipCodec.cpp
        src/IconUrl.cpp
    )
    target_include_directories(trailpack_host_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/public
    )
    target_link_libraries(trailpack_host_tests PRIVATE gw2_headers)
    set_target_properties(trailpack_host_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(NAME trailpack_memo COMMAND trailpack_host_tests)
endif()
