# Trailpack Core — built from the repository root (see ../CMakeLists.txt).

# Pure units: shared by the DLL and the host tests.
set(TRAILPACK_CORE_PURE
    src/api/Service.cpp
    src/generated/EventData.cpp
    src/generated/GameData.cpp
    src/events/EventSchedule.cpp
    src/net/TokenBucket.cpp
    src/resolve/Labels.cpp
    src/resolve/Offline.cpp
    src/resolve/Parse.cpp
    src/resolve/Resolver.cpp
    src/resolve/Text.cpp
)

if(NOT GW2_HOST_TESTS)
    # ── DLL target (MinGW cross-compile) ─────────────────────────────────────
    add_library(TrailpackCore SHARED
        src/dllmain.cpp
        src/core/I18n.cpp
        src/core/Settings.cpp
        src/net/HttpWinInet.cpp
        ${TRAILPACK_CORE_PURE}
        TrailpackCore.def
    )
    set_target_properties(TrailpackCore PROPERTIES SUFFIX ".dll" PREFIX "")
    target_include_directories(TrailpackCore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/public
    )
    target_link_libraries(TrailpackCore PRIVATE
        trailpack_chatlinks trailpack_files trailpack_ui gw2_imgui gw2_headers wininet)
    target_link_options(TrailpackCore PRIVATE
        -Wl,--out-implib,TrailpackCore.lib
        -Wl,--strip-all
    )
    # All addon DLLs land side by side in <build>/ for easy copying.
    set_target_properties(TrailpackCore PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
else()
    # ── Native host-test target ───────────────────────────────────────────────
    add_executable(trailpack_core_tests tests/core_tests.cpp src/core/I18n.cpp src/core/Settings.cpp
                   ${TRAILPACK_CORE_PURE})
    target_include_directories(trailpack_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/public)
    target_compile_definitions(trailpack_core_tests PRIVATE
        TPC_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(trailpack_core_tests PRIVATE trailpack_chatlinks trailpack_files gw2_headers)
    find_package(Threads REQUIRED)
    target_link_libraries(trailpack_core_tests PRIVATE Threads::Threads)
    set_target_properties(trailpack_core_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    add_test(NAME trailpack_core COMMAND trailpack_core_tests)
endif()
