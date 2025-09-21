# Framework library
add_library(sample_framework STATIC
  application.h
  application.cc
  imgui.h
  image.h
  image.cc
  mesh.h
  mesh.cc
  motion_utils.h
  motion_utils.cc
  profile.h
  profile.cc
  renderer.h
  utils.h
  utils.cc
  internal/camera.h
  internal/camera.cc
  internal/icosphere.h
  internal/immediate.h
  internal/immediate.cc
  internal/imgui_impl.h
  internal/imgui_impl.cc
  internal/renderer_impl.h
  internal/renderer_impl.cc
  internal/shader.h
  internal/shader.cc
  internal/shooter.h
  internal/shooter.cc)

# Samples require OpenGL package.
if(NOT EMSCRIPTEN)
  find_package(OpenGL REQUIRED)
  find_package(glfw3 CONFIG QUIET)
  if(TARGET glfw)
    target_link_libraries(sample_framework PUBLIC glfw)
  elseif(TARGET glfw3)
    target_link_libraries(sample_framework PUBLIC glfw3)
  else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(GLFW3 QUIET glfw3)
    endif()
    if(GLFW3_FOUND)
      target_include_directories(sample_framework PUBLIC ${GLFW3_INCLUDE_DIRS})
      target_link_libraries(sample_framework PUBLIC ${GLFW3_LIBRARIES})
    else()
      message(FATAL_ERROR "glfw3 is required to build sample_framework")
    endif()
  endif()
  target_link_libraries(sample_framework PUBLIC OpenGL::GL)
else()
  target_link_options(sample_framework PUBLIC
    -sUSE_GLFW=2
    -sMIN_WEBGL_VERSION=2
    -sMAX_WEBGL_VERSION=2)
endif()

# Link against ozz libs
if(ozz_build_tools)
  target_link_libraries(sample_framework PUBLIC ozz_options)
endif()
target_link_libraries(sample_framework PRIVATE ozz_geometry ozz_animation_offline)

target_include_directories(sample_framework PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/samples>
  $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/samples>)

if(TARGET BUILD_DATA_SAMPLE)
  add_dependencies(sample_framework BUILD_DATA_SAMPLE)
endif()

set_target_properties(sample_framework PROPERTIES FOLDER "samples")

add_subdirectory(tools)
