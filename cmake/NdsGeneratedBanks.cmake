include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../external/arm-recomp-core/cmake/ArmRecompCore.cmake")
# Title-owned bank identities/options feed one generation contract. Registration
# order and CPU profile files remain with their title because they are runtime ABI.
function(ndsrecomp_generate_bank)
    cmake_parse_arguments(B "" "STAMP;DIRECTORY;COMMENT" "BYPRODUCTS;ARGS;DEPENDS" ${ARGN})
    if(NOT B_STAMP OR NOT B_DIRECTORY OR NOT B_ARGS)
        message(FATAL_ERROR "ndsrecomp_generate_bank requires STAMP, DIRECTORY and ARGS")
    endif()
    add_custom_command(
        OUTPUT "${B_STAMP}"
        BYPRODUCTS ${B_BYPRODUCTS}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${B_DIRECTORY}"
        COMMAND $<TARGET_FILE:nds_recompile> ${B_ARGS}
        COMMAND ${CMAKE_COMMAND} -E touch "${B_STAMP}"
        DEPENDS nds_recompile ${B_DEPENDS}
        COMMENT "${B_COMMENT}"
        VERBATIM)
endfunction()

# Current generated C consumes both the DS runtime facade and the pinned shared
# ARM C ABI. Keep that include contract independent of each title's CPU/bank map.
function(ndsrecomp_target_generated_banks target)
    arm_recomp_core_include_dirs(_nds_bank_includes armv5te_nds)
    target_include_directories(${target} PRIVATE
        "${NDSRECOMP_ROOT}/recompiler/armv4t" ${_nds_bank_includes})
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
    ndsrecomp_limit_generated_compiles(${target})
    ndsrecomp_limit_link(${target})
endfunction()
