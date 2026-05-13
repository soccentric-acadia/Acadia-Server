cmake_minimum_required(VERSION 3.22)
project(acadia-server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ── Optional coverage ─────────────────────────────────────────────────────────
option(ENABLE_COVERAGE "Enable gcov coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

# ── Dependencies via vcpkg / find_package ─────────────────────────────────────
find_package(gRPC       CONFIG REQUIRED)
find_package(protobuf   CONFIG REQUIRED)
find_package(Drogon     CONFIG REQUIRED)
find_package(spdlog     CONFIG REQUIRED)
find_package(yaml-cpp   CONFIG REQUIRED)
find_package(GTest      CONFIG REQUIRED)
find_package(OpenSSL    REQUIRED)

# prometheus-cpp (optional — expose metrics endpoint)
find_package(prometheus-cpp CONFIG QUIET)

# libargon2
find_library(ARGON2_LIB argon2 REQUIRED)
find_path(ARGON2_INCLUDE argon2.h REQUIRED)

# ── Proto / gRPC code generation ──────────────────────────────────────────────
find_program(PROTOC_EXE      NAMES protoc           REQUIRED)
find_program(GRPC_CPP_PLUGIN NAMES grpc_cpp_plugin  REQUIRED)

set(PROTO_SRC_DIR "${CMAKE_SOURCE_DIR}/../protos")
set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/generated/proto")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

# Collect all .proto files recursively
file(GLOB_RECURSE PROTO_FILES
  "${PROTO_SRC_DIR}/*.proto"
)

set(PROTO_SRCS "")
set(PROTO_HDRS "")

foreach(PROTO_FILE ${PROTO_FILES})
  get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME_WE)
  get_filename_component(PROTO_DIR  "${PROTO_FILE}" DIRECTORY)

  set(PROTO_CC  "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.cc")
  set(PROTO_H   "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.h")
  set(GRPC_CC   "${PROTO_GEN_DIR}/${PROTO_NAME}.grpc.pb.cc")
  set(GRPC_H    "${PROTO_GEN_DIR}/${PROTO_NAME}.grpc.pb.h")

  add_custom_command(
    OUTPUT  "${PROTO_CC}" "${PROTO_H}" "${GRPC_CC}" "${GRPC_H}"
    COMMAND "${PROTOC_EXE}"
            --proto_path="${PROTO_SRC_DIR}"
            --proto_path="${PROTO_DIR}"
            --cpp_out="${PROTO_GEN_DIR}"
            --grpc_out="${PROTO_GEN_DIR}"
            --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN}"
            "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}"
    COMMENT "Generating C++ from ${PROTO_FILE}"
    VERBATIM
  )

  list(APPEND PROTO_SRCS "${PROTO_CC}" "${GRPC_CC}")
  list(APPEND PROTO_HDRS "${PROTO_H}"  "${GRPC_H}")
endforeach()

# Proto static library
add_library(acadia-proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(acadia-proto PUBLIC "${PROTO_GEN_DIR}")
target_link_libraries(acadia-proto PUBLIC
  gRPC::grpc++
  protobuf::libprotobuf
)
target_compile_options(acadia-proto PRIVATE -Wno-unused-parameter)

# ── Version header ────────────────────────────────────────────────────────────
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/version.hpp.in"
  "${GENERATED_DIR}/version.hpp"
  @ONLY
)

# ── Migration resources ───────────────────────────────────────────────────────
# Embed the migrations directory path so MigrationRunner can find SQL files.
set(MIGRATIONS_DIR "${CMAKE_SOURCE_DIR}/migrations")

# ── Collect source files ───────────────────────────────────────────────────────
file(GLOB_RECURSE ACADIA_CORE_SRCS
  "${CMAKE_SOURCE_DIR}/src/config/*.cc"
  "${CMAKE_SOURCE_DIR}/src/logging/*.cc"
  "${CMAKE_SOURCE_DIR}/src/registry/*.cc"
  "${CMAKE_SOURCE_DIR}/src/db/*.cc"
  "${CMAKE_SOURCE_DIR}/src/repositories/*.cc"
  "${CMAKE_SOURCE_DIR}/src/services/*.cc"
  "${CMAKE_SOURCE_DIR}/src/grpc_server/*.cc"
  "${CMAKE_SOURCE_DIR}/src/rest_api/*.cc"
  "${CMAKE_SOURCE_DIR}/src/orchestration/*.cc"
  "${CMAKE_SOURCE_DIR}/src/notifications/*.cc"
  "${CMAKE_SOURCE_DIR}/src/util/*.cc"
)

# ── Core library ──────────────────────────────────────────────────────────────
add_library(acadia-core STATIC ${ACADIA_CORE_SRCS})

target_include_directories(acadia-core PUBLIC
  "${CMAKE_SOURCE_DIR}/src"
  "${GENERATED_DIR}"
  "${ARGON2_INCLUDE}"
)

set(ACADIA_CORE_LIBS
  acadia-proto
  Drogon::Drogon
  spdlog::spdlog
  yaml-cpp::yaml-cpp
  OpenSSL::SSL
  OpenSSL::Crypto
  "${ARGON2_LIB}"
  gRPC::grpc++
  gRPC::grpc++_reflection
  protobuf::libprotobuf
)

if(prometheus-cpp_FOUND)
  list(APPEND ACADIA_CORE_LIBS
    prometheus-cpp::pull
    prometheus-cpp::core
  )
  target_compile_definitions(acadia-core PUBLIC ACADIA_HAS_PROMETHEUS=1)
endif()

target_link_libraries(acadia-core PUBLIC ${ACADIA_CORE_LIBS})

target_compile_options(acadia-core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ── Main binary ───────────────────────────────────────────────────────────────
add_executable(acadia-server src/main.cpp)
target_link_libraries(acadia-server PRIVATE acadia-core)
target_compile_options(acadia-server PRIVATE -Wall -Wextra -Wno-unused-parameter)
install(TARGETS acadia-server DESTINATION bin)

# ── Unit tests ────────────────────────────────────────────────────────────────
enable_testing()

file(GLOB_RECURSE UNIT_TEST_SRCS "${CMAKE_SOURCE_DIR}/tests/unit/*.cc")
if(UNIT_TEST_SRCS)
  add_executable(acadia-unit-tests ${UNIT_TEST_SRCS})
  target_include_directories(acadia-unit-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${GENERATED_DIR}"
    "${ARGON2_INCLUDE}"
  )
  target_link_libraries(acadia-unit-tests PRIVATE
    acadia-core
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
  )
  target_compile_options(acadia-unit-tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
  include(GoogleTest)
  gtest_discover_tests(acadia-unit-tests)
endif()

# ── Integration tests ─────────────────────────────────────────────────────────
file(GLOB_RECURSE INTEG_TEST_SRCS "${CMAKE_SOURCE_DIR}/tests/integration/*.cc")
if(INTEG_TEST_SRCS)
  add_executable(acadia-integration-tests ${INTEG_TEST_SRCS})
  target_include_directories(acadia-integration-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${GENERATED_DIR}"
    "${ARGON2_INCLUDE}"
  )
  target_link_libraries(acadia-integration-tests PRIVATE
    acadia-core
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
  )
  target_compile_options(acadia-integration-tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
  include(GoogleTest)
  gtest_discover_tests(acadia-integration-tests
    PROPERTIES LABELS "integration"
  )
endif()

# ── Legacy internal lib + config-test (kept for compatibility) ─────────────────
include(FetchContent)
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

add_library(acadia-internal STATIC
  internal/config/config.cpp
  internal/logging/logger.cpp
)
target_include_directories(acadia-internal PUBLIC
  "${CMAKE_SOURCE_DIR}"
  "${GENERATED_DIR}"
)
target_link_libraries(acadia-internal PUBLIC tomlplusplus::tomlplusplus)
target_compile_options(acadia-internal PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(config-test test/config_test.cpp)
target_link_libraries(config-test PRIVATE acadia-internal)
target_compile_options(config-test PRIVATE -Wall -Wextra)
add_test(NAME config-test COMMAND config-test)
