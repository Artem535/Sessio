# Throwaway spike code (issue #116) — not part of the Sessio build.
# Same GigaAM + Silero VAD smoke test as ../asr_smoke.cc, but through the C++ API and with
# sherpa-onnx built from source via FetchContent, so it uses this toolchain's std::string ABI
# (the prebuilt Linux tarballs use the old one and cannot be used with cxx-api.h).
cmake_minimum_required(VERSION 3.28)
project(asr_smoke_cxx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_CHECK OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_TTS OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_C_API ON CACHE BOOL "" FORCE)
set(SHERPA_ONNX_BUILD_C_API_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_GPU OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sherpa_onnx
  GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
  GIT_TAG v1.13.8
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(sherpa_onnx)

add_executable(asr_smoke_cxx asr_smoke_cxx.cc)
target_include_directories(asr_smoke_cxx PRIVATE "${sherpa_onnx_SOURCE_DIR}")
target_link_libraries(asr_smoke_cxx PRIVATE sherpa-onnx-cxx-api)
