# Shared third-party code for all addons.
#
#   gw2_headers  INTERFACE  Nexus API (nexus/Nexus.h) + nlohmann/json (<nlohmann/json.hpp>)
#   gw2_imgui    STATIC     Dear ImGui 1.80 (the version Nexus ships) + imgui_stdlib
#                           (DLL builds only; every addon DLL links its own copy and
#                           binds it to Nexus's context at load).

add_library(gw2_headers INTERFACE)
target_include_directories(gw2_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/nlohmann
)

if(NOT GW2_HOST_TESTS)
    add_library(gw2_imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/misc/cpp/imgui_stdlib.cpp
    )
    target_include_directories(gw2_imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/misc/cpp
    )
endif()
