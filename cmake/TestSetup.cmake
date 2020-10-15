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



# Search for the python interpreter
# Version number has been intentionally excluded from find_package call, so that latest version 
# will be grabbed. Including the version number would prioritise the version provided over more 
#
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if( ${Python3_VERSION} VERSION_LESS "3.5")
  message(FATAL_ERROR "Python version requirements not satisfied")
endif()

# Ensure all required packages are present
include(${PROJECT_SOURCE_DIR}/cmake/PythonModuleCheck.cmake)
required_python_modules_found("${REQUIRED_PYTHON_MODULES}")

# Adds the drivers used in the regression tests to a global cmake property: DRIVERS_USED_IN_TESTS
function(record_driver arg)
    list(LENGTH arg len_list)
    math(EXPR list_end "${len_list} - 1")
    foreach(ind RANGE ${list_end})
      list(GET arg ${ind} arg2)
      if("${arg2}" STREQUAL "--driver")
        MATH(EXPR ind "${ind}+1")
        list(GET arg ${ind} driver)
        get_filename_component(driver ${driver} NAME)
        set_property(GLOBAL APPEND PROPERTY DRIVERS_USED_IN_TESTS "${driver}" )
      endif()
    endforeach()
endfunction()

# Adds test that will run in serial
# test output will be sent to /tst/regression/outputs/dir
# test property labels: regression, mpi-no
function(setup_test dir arg extra_labels)
  separate_arguments(arg) 
  list(APPEND labels "regression;mpi-no")
  list(APPEND labels "${extra_labels}")
  add_test( NAME regression_test:${dir} COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/run_test.py" 
    ${arg} --test_dir "${CMAKE_CURRENT_SOURCE_DIR}/test_suites/${dir}"
    --output_dir "${PROJECT_BINARY_DIR}/tst/regression/outputs/${dir}")
  set_tests_properties(regression_test:${dir} PROPERTIES LABELS "${labels}" )
  record_driver("${arg}")
endfunction()

# Adds test that will run in serial with code coverage
# test output will be sent to /tst/regression/outputs/dir_cov
# test property labels: regression, mpi-no; coverage
function(setup_test_coverage dir arg extra_labels)
  if( CODE_COVERAGE )
    separate_arguments(arg) 

    list(APPEND labels "regression;coverage;mpi-no")
    list(APPEND labels "${extra_labels}")
    add_test( NAME regression_coverage_test:${dir} COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/run_test.py" 
      ${arg} 
      --coverage
      --test_dir "${CMAKE_CURRENT_SOURCE_DIR}/test_suites/${dir}"
      --output_dir "${PROJECT_BINARY_DIR}/tst/regression/outputs/${dir}_cov")
    set_tests_properties(regression_coverage_test:${dir} PROPERTIES LABELS "${labels}" )
    record_driver("${arg}")
  endif()
endfunction()

function(process_mpi_args nproc)
  list(APPEND TMPARGS "--mpirun")
  # use custom mpiexec
  if (TEST_MPIEXEC)
    list(APPEND TMPARGS "${TEST_MPIEXEC}")
  # use CMake determined mpiexec
  else()
    list(APPEND TMPARGS "${MPIEXEC_EXECUTABLE}")
  endif()
  # use custom numproc flag
  if (TEST_NUMPROC_FLAG)
    list(APPEND TMPARGS "--mpirun_opts=${TEST_NUMPROC_FLAG}")
  # use CMake determined numproc flag
  else()
    list(APPEND TMPARGS "--mpirun_opts=${MPIEXEC_NUMPROC_FLAG}")
  endif()
  list(APPEND TMPARGS "--mpirun_opts=${nproc}")
  # set additional options from machine configuration
  foreach(MPIARG ${TEST_MPIOPTS})
    list(APPEND TMPARGS "--mpirun_opts=${MPIARG}")
  endforeach()

  # make the result accessible in the calling function
  set(MPIARGS ${TMPARGS} PARENT_SCOPE)
endfunction()

# Adds test that will run in parallel with mpi
# test output will be sent to /tst/regression/outputs/dir_mpi
# test property labels: regression, mpi-yes
function(setup_test_mpi nproc dir arg extra_labels)
  if( MPI_FOUND )
    separate_arguments(arg) 
    list(APPEND labels "regression;mpi-yes")
    list(APPEND labels "${extra_labels}")

    if( "${Kokkos_ENABLE_CUDA}" )
      set(PARTHENON_KOKKOS_TEST_ARGS "--kokkos-num-devices=${NUM_GPU_DEVICES_PER_NODE}")
      list(APPEND labels "cuda")
    endif()
    process_mpi_args(${nproc})
    add_test( NAME regression_mpi_test:${dir} COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_test.py
      ${MPIARGS} ${arg}
      --test_dir ${CMAKE_CURRENT_SOURCE_DIR}/test_suites/${dir}
      --output_dir "${PROJECT_BINARY_DIR}/tst/regression/outputs/${dir}_mpi"
      --kokkos_args=${PARTHENON_KOKKOS_TEST_ARGS})
    set_tests_properties(regression_mpi_test:${dir} PROPERTIES LABELS "${labels}" RUN_SERIAL ON )
    record_driver("${arg}")
  else()
    message(STATUS "MPI not found, not building regression tests with mpi")
  endif()
endfunction()

# Adds test that will run in parallel with mpi and code coverage
# test output will be sent to /tst/regression/outputs/dir_mpi_cov
# test property labels: regression, mpi-yes, coverage
function(setup_test_mpi_coverage nproc dir arg extra_labels)
  if( MPI_FOUND )
    if( CODE_COVERAGE )

      list(APPEND labels "regression;coverage;mpi-yes")
      list(APPEND labels "${extra_labels}")
      separate_arguments(arg) 
      process_mpi_args(${nproc})
      add_test( NAME regression_mpi_coverage_test:${dir} COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_test.py
        --coverage
        ${MPIARGS} ${arg}
        --test_dir ${CMAKE_CURRENT_SOURCE_DIR}/test_suites/${dir}
        --output_dir "${PROJECT_BINARY_DIR}/tst/regression/outputs/${dir}_mpi_cov"
        )
      set_tests_properties(regression_mpi_coverage_test:${dir} PROPERTIES LABELS "${labels}" RUN_SERIAL ON )
    endif()
  else()
    message(STATUS "MPI not found, not building coverage regression tests with mpi")
  endif()
endfunction()


