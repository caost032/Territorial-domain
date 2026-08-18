cmake_minimum_required(VERSION 3.18)
project(odpar_territorial_domain LANGUAGES C)

set(ODPAR_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../engine")

add_library(odpar_territorial_domain SHARED
  "${ODPAR_ENGINE_ROOT}/src/platform.c"
  "${ODPAR_ENGINE_ROOT}/src/game.c"
  "${ODPAR_ENGINE_ROOT}/src/sim.c"
  "${ODPAR_ENGINE_ROOT}/src/render.c"
  "${ODPAR_ENGINE_ROOT}/vendor/odm_status.c"
  "${ODPAR_ENGINE_ROOT}/vendor/odm_rng.c"
)

target_include_directories(odpar_territorial_domain PRIVATE
  "${ODPAR_ENGINE_ROOT}/include"
  "${ODPAR_ENGINE_ROOT}/src"
  "${ODPAR_ENGINE_ROOT}/vendor"
)

target_compile_features(odpar_territorial_domain PRIVATE c_std_11)
set_target_properties(odpar_territorial_domain PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
  OUTPUT_NAME "odpar_territorial_domain"
)

target_compile_options(odpar_territorial_domain PRIVATE
  -fno-builtin
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Werror
  $<$<CONFIG:Release>:-O3>
  $<$<NOT:$<CONFIG:Release>>:-O1>
  $<$<NOT:$<CONFIG:Release>>:-g3>
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_link_options(odpar_territorial_domain PRIVATE
    "LINKER:--no-undefined"
    "LINKER:-z,relro"
    "LINKER:-z,now"
    "LINKER:-z,noexecstack"
    # NDK r28+ aligns this by default. Keeping the explicit values also makes
    # builds made with r27 or older loadable on 16 KiB page-size devices.
    "LINKER:-z,max-page-size=16384"
    "LINKER:-z,common-page-size=16384"
  )
endif()

# The engine owns the public ABI allowlist. Older development snapshots still
# build; release snapshots use the map to hide every internal helper.
set(ODPAR_EXPORT_MAP
    "${ODPAR_ENGINE_ROOT}/odpar_territorial_domain.exports.map")
if(EXISTS "${ODPAR_EXPORT_MAP}" AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_link_options(odpar_territorial_domain PRIVATE
    "LINKER:--version-script=${ODPAR_EXPORT_MAP}"
  )
  set_property(TARGET odpar_territorial_domain APPEND PROPERTY
    LINK_DEPENDS "${ODPAR_EXPORT_MAP}")
endif()
