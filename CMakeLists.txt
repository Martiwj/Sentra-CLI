cmake_minimum_required(VERSION 3.16)
project(sentra_cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(sentra
  src/main.cpp
  src/cli/repl.cpp
)

add_library(sentra_lib
  src/core/model_registry.cpp
  src/core/orchestrator.cpp
  src/core/session_store.cpp
  src/core/context_window.cpp
  src/core/app_state.cpp
  src/runtime/mock_runtime.cpp
  src/runtime/local_binary_runtime.cpp
  src/runtime/llama_inproc_runtime.cpp
)

target_include_directories(sentra_lib PUBLIC include)

find_path(LLAMA_CPP_INCLUDE_DIR llama.h
  PATHS /opt/homebrew/include /usr/local/include
)
find_library(LLAMA_CPP_LIBRARY llama
  PATHS /opt/homebrew/lib /usr/local/lib
)
find_library(GGML_LIBRARY ggml
  PATHS /opt/homebrew/lib /usr/local/lib
)

if (LLAMA_CPP_INCLUDE_DIR AND LLAMA_CPP_LIBRARY)
  target_compile_definitions(sentra_lib PRIVATE SENTRA_HAS_LLAMA_CPP=1)
  target_include_directories(sentra_lib PRIVATE ${LLAMA_CPP_INCLUDE_DIR})
  if (GGML_LIBRARY)
    target_link_libraries(sentra_lib PUBLIC ${LLAMA_CPP_LIBRARY} ${GGML_LIBRARY})
  else()
    target_link_libraries(sentra_lib PUBLIC ${LLAMA_CPP_LIBRARY})
  endif()
endif()
target_link_libraries(sentra PRIVATE sentra_lib)

add_executable(sentra_tests
  tests/test_main.cpp
)
target_include_directories(sentra_tests PRIVATE include)
target_link_libraries(sentra_tests PRIVATE sentra_lib)

if (MSVC)
  target_compile_options(sentra PRIVATE /W4)
else()
  target_compile_options(sentra PRIVATE -Wall -Wextra -Wpedantic)
endif()
