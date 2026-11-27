# Reusable runtime services; DS hardware scheduling and presentation stay local.
set(NDS_SHARED_CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../external" CACHE PATH
    "Directory containing pinned recomp runtime service submodules")
set(_nds_saved_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
set(RECOMP_INPUT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RECOMP_AUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
foreach(_core persistence input audio diagnostics)
    add_subdirectory("${NDS_SHARED_CORE_ROOT}/recomp-${_core}-core"
                     "${CMAKE_BINARY_DIR}/shared/${_core}" EXCLUDE_FROM_ALL)
endforeach()
set(BUILD_TESTING "${_nds_saved_build_testing}")
add_library(nds_shared_runtime INTERFACE)
target_link_libraries(nds_shared_runtime INTERFACE
    recomp-persistence-core::core recomp-input::core recomp-audio::core
    recomp-diagnostics-core::core)
# Existing standalone device/state fixtures compile the same production files.
link_libraries(nds_shared_runtime)
