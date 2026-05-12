cmake_minimum_required(VERSION 3.20)
project(acadia-server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(FetchContent)

# toml++ — header-only TOML parser
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

# Version header (build-time injected)
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/version.hpp"
  @ONLY
)

# ── Internal static lib (shared by binary and tests) ──────────────────────────
add_library(acadia-internal STATIC
  internal/config/config.cpp
  internal/logging/logger.cpp
)

target_include_directories(acadia-internal PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(acadia-internal PUBLIC
  tomlplusplus::tomlplusplus
)

target_compile_options(acadia-internal PRIVATE
  -Wall -Wextra -Wno-unused-parameter
)

# ── Main binary ────────────────────────────────────────────────────────────────
add_executable(acadia-server
  cmd/acadia-server/main.cpp
)

target_link_libraries(acadia-server PRIVATE acadia-internal)

target_compile_options(acadia-server PRIVATE
  -Wall -Wextra -Wno-unused-parameter
)

install(TARGETS acadia-server DESTINATION bin)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()

add_executable(config-test test/config_test.cpp)
target_link_libraries(config-test PRIVATE acadia-internal)
target_compile_options(config-test PRIVATE -Wall -Wextra)
add_test(NAME config-test COMMAND config-test)
