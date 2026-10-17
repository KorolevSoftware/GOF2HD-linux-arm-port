gof_library(gof_gles libGLESv2 gles-bridge.c)
set_target_properties(gof_gles PROPERTIES NO_SONAME FALSE)
target_link_libraries(gof_gles PRIVATE dl)
