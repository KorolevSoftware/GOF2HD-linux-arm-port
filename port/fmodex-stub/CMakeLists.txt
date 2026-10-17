gof_library(gof_pthread pthread_bionic pthread_bionic.c)
target_link_libraries(gof_pthread PRIVATE dl pthread)
gof_version_script(gof_pthread "${CMAKE_CURRENT_SOURCE_DIR}/pthread_bionic.map")
gof_library(gof_cpuinfo cpuinfo_fake cpuinfo_fake.c)
target_link_libraries(gof_cpuinfo PRIVATE dl)

# This .c file contains extern "C" and was compiled with g++ by the shell build.
set_source_files_properties(libstdcxx_stub.c PROPERTIES LANGUAGE CXX)
gof_library(gof_stdcxx libstdc++ libstdcxx_stub.c)
gof_library(gof_opensles libOpenSLES libOpenSLES.c)
target_include_directories(gof_opensles PRIVATE "${SDL2_INCLUDE_ROOT}")
target_link_libraries(gof_opensles PRIVATE "${SDL2_LIBRARY}" "${SDL2MAIN_LIBRARY}")
gof_library(gof_filesystem libfmod_filesystem fmod_filesystem.c)
target_link_libraries(gof_filesystem PRIVATE dl)
