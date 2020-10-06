#=========================================================================================
# (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
#
# This program was produced under U.S. Government contract 89233218CNA000001 for Los
# Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
# for the U.S. Department of Energy/National Nuclear Security Administration. All rights
# in the program are reserved by Triad National Security, LLC, and the U.S. Department
# of Energy/National Nuclear Security Administration. The Government is granted for
# itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
# license in this material to reproduce, prepare derivative works, distribute copies to
# the public, perform publicly and display publicly, and to permit others to do so.
#=========================================================================================

include_guard(GLOBAL)

if (PARTHENON_LINT_DEFAULT)
    set(ALL "ALL")
endif()

add_custom_target(
    lint ${ALL}
    COMMENT "Linted project")

function(lint_file SOURCE_DIR INPUT OUTPUT)
    get_filename_component(OUTPUT_DIR ${OUTPUT} DIRECTORY)
    if (OUTPUT_DIR)
        set(MKDIR_COMMAND COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR})
    endif()

    if( EXISTS ${INPUT} )
      set(FILE_TO_LINT ${INPUT} )
    elseif( EXISTS ${SOURCE_DIR}/${INPUT})
      set(FILE_TO_LINT ${SOURCE_DIR}/${INPUT})
    else()
      message(WARNING "Cannot lint file ${INPUT} does not appear to exist.")
    endif()

    add_custom_command(
        OUTPUT ${OUTPUT}
        COMMAND
        ${parthenon_SOURCE_DIR}/tst/style/cpplint.py
                --counting=detailed
                --quiet ${FILE_TO_LINT}
        ${MKDIR_COMMAND}
        COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
        DEPENDS ${INPUT}
        COMMENT "Linting ${INPUT}"
    )
endfunction(lint_file)

function(lint_target TARGET_NAME)
    get_target_property(TARGET_SOURCES ${TARGET_NAME} SOURCES)
    get_target_property(TARGET_SOURCE_DIR ${TARGET_NAME} SOURCE_DIR)

    set(TARGET_OUTPUTS)
    foreach(SOURCE ${TARGET_SOURCES})
        lint_file(${TARGET_SOURCE_DIR} ${SOURCE} ${SOURCE}.lint)
        list(APPEND TARGET_OUTPUTS ${SOURCE}.lint)
    endforeach()

    add_custom_target(
        ${TARGET_NAME}-lint
        DEPENDS ${TARGET_OUTPUTS}
        COMMENT "Linted ${TARGET_NAME}")
    add_dependencies(lint ${TARGET_NAME}-lint)
endfunction(lint_target)
