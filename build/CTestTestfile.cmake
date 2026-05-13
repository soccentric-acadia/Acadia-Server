# CMake generated Testfile for 
# Source directory: /home/sandesh/work/Acadia/Acadia-Server
# Build directory: /home/sandesh/work/Acadia/Acadia-Server/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/home/sandesh/work/Acadia/Acadia-Server/build/acadia-unit-tests[1]_include.cmake")
include("/home/sandesh/work/Acadia/Acadia-Server/build/acadia-integration-tests[1]_include.cmake")
add_test([=[config-test]=] "/home/sandesh/work/Acadia/Acadia-Server/build/config-test")
set_tests_properties([=[config-test]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/sandesh/work/Acadia/Acadia-Server/CMakeLists.txt;249;add_test;/home/sandesh/work/Acadia/Acadia-Server/CMakeLists.txt;0;")
subdirs("_deps/tomlplusplus-build")
