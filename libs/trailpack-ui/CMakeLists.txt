# trailpack_ui — shared ImGui style helpers (palette, rounding, settings pages, locked items).
# DLL builds only: it needs ImGui, which the host-test build does not compile.

if(NOT GW2_HOST_TESTS)
    add_library(trailpack_ui STATIC src/Style.cpp)
    target_include_directories(trailpack_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(trailpack_ui PUBLIC gw2_imgui)
endif()
