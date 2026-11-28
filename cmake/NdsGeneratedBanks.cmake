include_guard(GLOBAL)
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
