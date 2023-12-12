if(LCM_DO_ALBANY_CMAKE)
  return()
endif()
set(LCM_DO_ALBANY_CMAKE true)

include(${CMAKE_CURRENT_LIST_DIR}/snl_helpers.cmake)

function(lcm_do_albany)
  set(BOOL_OPTS
      "CLEAN_BUILD"
      "CLEAN_INSTALL"
      "DO_UPDATE"
      "DO_CONFIG"
      "DO_BUILD"
      "DO_TEST"
     )
  set(UNARY_OPTS
      "BUILD_THREADS"
      "RESULT_VARIABLE"
      "BUILD_ID_STRING"
    )
  message("lcm_do_albany(${ARGN})")
  cmake_parse_arguments(ARG "${BOOL_OPTS}" "${UNARY_OPTS}" "" ${ARGN}) 
  if (ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
        "lcm_do_albany called with unrecognized arguments ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  set(CONFIG_OPTS
    "-DALBANY_CTEST_TIMEOUT:INTEGER=60"
    "-DALBANY_TRILINOS_DIR:FILEPATH=$ENV{LCM_DIR}/trilinos-install-${ARG_BUILD_ID_STRING}"
    "-DCMAKE_CXX_FLAGS:STRING=$ENV{LCM_CXX_FLAGS}"
    "-DCMAKE_CXX_COMPILER:FILEPATH=$ENV{MPI_BIN}/mpicxx"
    "-DCMAKE_C_COMPILER:FILEPATH=$ENV{MPI_BIN}/mpicc"
    "-DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF"
    "-DENABLE_LAME:BOOL=OFF"
    "-DENABLE_LAMENT:BOOL=OFF"
    "-DENABLE_CHECK_FPE:BOOL=$ENV{LCM_FPE_SWITCH}"
    "-DENABLE_FLUSH_DENORMALS:BOOL=$ENV{LCM_DENORMAL_SWITCH}"
    )
  if (DEFINED ENV{LCM_LINK_FLAGS})
    set(CONFIG_OPTS ${CONFIG_OPTS}
        "-DCMAKE_EXE_LINKER_FLAGS:STRING=$ENV{LCM_LINK_FLAGS}"
        "-DCMAKE_SHARED_LINKER_FLAGS:STRING=$ENV{LCM_LINK_FLAGS}"
       )
  endif()
  set(ARG_BOOL_OPTS)
  foreach (BOOL_OPT IN LISTS BOOL_OPTS)
    if (ARG_${BOOL_OPT})
      set(ARG_BOOL_OPTS ${ARG_BOOL_OPTS} ${BOOL_OPT})
    endif()
  endforeach()
  snl_do_subproject(${ARG_BOOL_OPTS}
      DO_PROJECT
      "PROJECT" "Albany"
      SOURCE_DIR "$ENV{LCM_DIR}/LCM"
      BUILD_DIR "$ENV{LCM_DIR}/lcm-build-${ARG_BUILD_ID_STRING}"
      CONFIG_OPTS "${CONFIG_OPTS}"
      BUILD_THREADS "${ARG_BUILD_THREADS}"
      RESULT_VARIABLE ERR
      )
  if (ARG_RESULT_VARIABLE)
    set(${ARG_RESULT_VARIABLE} ${ERR} PARENT_SCOPE)
  endif()
endfunction(lcm_do_albany)
