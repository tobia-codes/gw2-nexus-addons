# trailpack_files — crash-safe file I/O and path helpers (pure, static), UTF-8 paths, no streams.

add_library(trailpack_files STATIC src/SafeFile.cpp)
target_include_directories(trailpack_files PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
