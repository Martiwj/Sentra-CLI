cmake_minimum_required(VERSION 3.16)
project(sentra_cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(sentra
  src/main.cpp
  src/cli/repl.cpp
  src/core/orchestrator.cpp
  src/core/model_registry.cpp
  src/core/session_store.cpp
  src/runtime/mock_runtime.cpp
  src/runtime/local_binary_runtime.cpp
)

target_include_directories(sentra PRIVATE include)

if (MSVC)
  target_compile_options(sentra PRIVATE /W4)
else()
  target_compile_options(sentra PRIVATE -Wall -Wextra -Wpedantic)
endif()
