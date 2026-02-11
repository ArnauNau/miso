if(NOT DEFINED SRC_DIR)
  message(FATAL_ERROR "SRC_DIR not provided")
endif()

if(NOT DEFINED LINT_CORE)
  set(LINT_CORE ON)
endif()

if(NOT DEFINED LINT_GAME_CAFE)
  set(LINT_GAME_CAFE ON)
endif()

if(NOT DEFINED LINT_TESTBED)
  set(LINT_TESTBED OFF)
endif()

if(NOT DEFINED LINT_ENGINE_PORTABILITY)
  set(LINT_ENGINE_PORTABILITY ON)
endif()

if(DEFINED RG_EXE AND NOT RG_EXE STREQUAL "")
  set(SEARCH_CMD "${RG_EXE}")
  set(SEARCH_HAS_REGEX ON)
else()
  set(SEARCH_CMD grep)
  set(SEARCH_HAS_REGEX OFF)
endif()

function(miso_boundary_lint include_pattern violation_message)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs PATHS)
  cmake_parse_arguments(LINT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(SEARCH_HAS_REGEX)
    execute_process(
      COMMAND "${SEARCH_CMD}" -n "${include_pattern}" ${LINT_PATHS}
      RESULT_VARIABLE lint_rc
      OUTPUT_VARIABLE lint_out
      ERROR_VARIABLE lint_err
    )
  else()
    execute_process(
      COMMAND "${SEARCH_CMD}" -R -n -E "${include_pattern}" ${LINT_PATHS}
      RESULT_VARIABLE lint_rc
      OUTPUT_VARIABLE lint_out
      ERROR_VARIABLE lint_err
    )
  endif()

  if(lint_rc EQUAL 0)
    message(FATAL_ERROR "${violation_message}\n${lint_out}")
  endif()
endfunction()

if(LINT_CORE)
  miso_boundary_lint(
    "#include[[:space:]]+\"miso_cafe_"
    "Boundary violation: miso_core includes miso_cafe_* headers"
    PATHS "${SRC_DIR}/engine/include" "${SRC_DIR}/engine/src"
  )

  miso_boundary_lint(
    "#include[[:space:]]+\"testbed/"
    "Boundary violation: miso_core includes testbed headers"
    PATHS "${SRC_DIR}/engine/include" "${SRC_DIR}/engine/src"
  )
endif()

if(LINT_GAME_CAFE)
  miso_boundary_lint(
    "#include[[:space:]]+\".*engine/src/internal/"
    "Boundary violation: miso_game_cafe includes engine internals"
    PATHS "${SRC_DIR}/prototypes/cafe-tycoon-proto/include" "${SRC_DIR}/prototypes/cafe-tycoon-proto/src"
  )
endif()

if(LINT_TESTBED)
  miso_boundary_lint(
    "#include[[:space:]]+\".*engine/src/internal/"
    "Boundary violation: testbed includes engine internals"
    PATHS "${SRC_DIR}/testbed"
  )

  miso_boundary_lint(
    "#include[[:space:]]+\"miso_cafe_"
    "Boundary violation: testbed includes miso_cafe_* headers"
    PATHS "${SRC_DIR}/testbed"
  )
endif()

if(LINT_ENGINE_PORTABILITY)
  miso_boundary_lint(
    "(^|[^A-Za-z0-9_])(memcpy|memset|memcmp)[[:space:]]*\\("
    "Engine portability rule violation: use SDL_mem* wrappers in miso_core"
    PATHS "${SRC_DIR}/engine/src" "${SRC_DIR}/engine/include"
  )
endif()

message(STATUS "miso boundary lint passed")
