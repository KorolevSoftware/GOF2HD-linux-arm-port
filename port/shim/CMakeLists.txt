gof_library(gof_libc libc
  shim.c sscanf.c stdio.c abi.c)
target_compile_options(gof_libc PRIVATE -std=gnu89)
target_link_libraries(gof_libc PRIVATE dl)
gof_version_script(gof_libc "${CMAKE_CURRENT_SOURCE_DIR}/version.map")

gof_library(gof_log liblog liblog.c)
gof_library(gof_math libm libm.c)
target_link_libraries(gof_math PRIVATE dl)
gof_version_script(gof_math "${CMAKE_CURRENT_SOURCE_DIR}/libm.map")

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/empty.c" CONTENT "")
foreach(name libandroid libdl)
  gof_library(gof_${name} ${name} "${CMAKE_CURRENT_BINARY_DIR}/empty.c")
  gof_version_script(gof_${name} "${CMAKE_CURRENT_SOURCE_DIR}/emptyver.map")
endforeach()
