# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/dev/m8-sdl3/build_render/_deps/daisysp-src")
  file(MAKE_DIRECTORY "C:/dev/m8-sdl3/build_render/_deps/daisysp-src")
endif()
file(MAKE_DIRECTORY
  "C:/dev/m8-sdl3/build_render/_deps/daisysp-build"
  "C:/dev/m8-sdl3/build_render/_deps/daisysp-subbuild/daisysp-populate-prefix"
  "C:/dev/m8-sdl3/build_render/_deps/daisysp-subbuild/daisysp-populate-prefix/tmp"
  "C:/dev/m8-sdl3/build_render/_deps/daisysp-subbuild/daisysp-populate-prefix/src/daisysp-populate-stamp"
  "C:/dev/m8-sdl3/build_render/_deps/daisysp-subbuild/daisysp-populate-prefix/src"
  "C:/dev/m8-sdl3/build_render/_deps/daisysp-subbuild/daisysp-populate-prefix/src/daisysp-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/dev/m8-sdl3/build_render/_deps/daisysp-subbuild/daisysp-populate-prefix/src/daisysp-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/dev/m8-sdl3/build_render/_deps/daisysp-subbuild/daisysp-populate-prefix/src/daisysp-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
