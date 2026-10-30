# trailpack_chatlinks — GW2 chat-link codec (pure, static), linked by every addon.

add_library(trailpack_chatlinks STATIC src/ChatLinks.cpp)
target_include_directories(trailpack_chatlinks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(GW2_HOST_TESTS)
    add_executable(trailpack_chatlinks_tests tests/chatlinks_tests.cpp)
    target_link_libraries(trailpack_chatlinks_tests PRIVATE trailpack_chatlinks)
    set_target_properties(trailpack_chatlinks_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    add_test(NAME trailpack_chatlinks COMMAND trailpack_chatlinks_tests)
endif()
