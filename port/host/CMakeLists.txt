add_executable(gof2hd
  gof2hd.c config.c engine_bridge.c
  touch_fifo.c jni.c wrap_overlay.c)
set_target_properties(gof2hd PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GOF_OUT}" LINKER_LANGUAGE C)
target_compile_options(gof2hd PRIVATE -UNDEBUG ${port_flags})
target_include_directories(gof2hd PRIVATE "${SDL2_INCLUDE_ROOT}")
target_link_options(gof2hd PRIVATE ${port_flags} -rdynamic)
target_link_libraries(gof2hd PRIVATE gof_gles
  "${SDL2MAIN_LIBRARY}" "${SDL2_LIBRARY}" dl gcc_s stdc++ pthread m)
