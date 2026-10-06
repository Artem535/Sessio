# Throwaway spike code (issue #116) — not part of the Sessio build.
cmake_minimum_required(VERSION 3.28)
project(asr_smoke LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headers come from the v1.13.8 source tag (the shared-lib tarball ships only lib/).
set(SHERPA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx-v1.13.8-linux-x64-shared-lib")
add_executable(asr_smoke asr_smoke.cc)
target_include_directories(asr_smoke PRIVATE "${SHERPA_ROOT}/include")
target_link_directories(asr_smoke PRIVATE "${SHERPA_ROOT}/lib")
target_link_libraries(asr_smoke PRIVATE sherpa-onnx-c-api onnxruntime)
set_target_properties(asr_smoke PROPERTIES BUILD_RPATH "${SHERPA_ROOT}/lib")
