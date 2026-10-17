separate_arguments(port_flags UNIX_COMMAND "${GOF_CFLAGS}")

find_path(SDL2_INCLUDE_ROOT SDL2/SDL.h REQUIRED)
find_library(SDL2_LIBRARY NAMES SDL2 HINTS "${GOF_SYS_LIB}" REQUIRED)
find_library(SDL2MAIN_LIBRARY NAMES SDL2main HINTS "${GOF_SYS_LIB}" REQUIRED)

function(gof_library target output)
  add_library(${target} SHARED ${ARGN})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${output}" PREFIX "" NO_SONAME TRUE
    LIBRARY_OUTPUT_DIRECTORY "${GOF_OUT}")
  target_compile_options(${target} PRIVATE -UNDEBUG ${port_flags})
  target_link_options(${target} PRIVATE ${port_flags})
endfunction()

function(gof_version_script target path)
  target_link_options(${target} PRIVATE "-Wl,--version-script=${path}")
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${path}")
endfunction()

add_subdirectory(shim)
add_subdirectory(gles-stub)
add_subdirectory(fmodex-stub)
add_subdirectory(host)
