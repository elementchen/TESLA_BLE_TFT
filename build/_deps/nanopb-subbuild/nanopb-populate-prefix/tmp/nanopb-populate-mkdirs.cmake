# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-src")
  file(MAKE_DIRECTORY "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-src")
endif()
file(MAKE_DIRECTORY
  "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-build"
  "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-subbuild/nanopb-populate-prefix"
  "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-subbuild/nanopb-populate-prefix/tmp"
  "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-subbuild/nanopb-populate-prefix/src/nanopb-populate-stamp"
  "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-subbuild/nanopb-populate-prefix/src"
  "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-subbuild/nanopb-populate-prefix/src/nanopb-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-subbuild/nanopb-populate-prefix/src/nanopb-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/openclaw/Desktop/projects/Tesla_BLE_Dash/build/_deps/nanopb-subbuild/nanopb-populate-prefix/src/nanopb-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
