# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/sandesh/work/Acadia/Acadia-Server/build/_deps/tomlplusplus-src"
  "/home/sandesh/work/Acadia/Acadia-Server/build/_deps/tomlplusplus-build"
  "/mnt/data/workspace/Acadia/Acadia-Server/build/_deps/tomlplusplus-subbuild/tomlplusplus-populate-prefix"
  "/mnt/data/workspace/Acadia/Acadia-Server/build/_deps/tomlplusplus-subbuild/tomlplusplus-populate-prefix/tmp"
  "/mnt/data/workspace/Acadia/Acadia-Server/build/_deps/tomlplusplus-subbuild/tomlplusplus-populate-prefix/src/tomlplusplus-populate-stamp"
  "/mnt/data/workspace/Acadia/Acadia-Server/build/_deps/tomlplusplus-subbuild/tomlplusplus-populate-prefix/src"
  "/mnt/data/workspace/Acadia/Acadia-Server/build/_deps/tomlplusplus-subbuild/tomlplusplus-populate-prefix/src/tomlplusplus-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/mnt/data/workspace/Acadia/Acadia-Server/build/_deps/tomlplusplus-subbuild/tomlplusplus-populate-prefix/src/tomlplusplus-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/mnt/data/workspace/Acadia/Acadia-Server/build/_deps/tomlplusplus-subbuild/tomlplusplus-populate-prefix/src/tomlplusplus-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
