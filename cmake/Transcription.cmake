# Live call transcription: sherpa-onnx (built from source) plus the GigaAM and
# Silero VAD model files. ON on every platform.
option(SESSIO_ENABLE_TRANSCRIPTION "Build live call transcription (sherpa-onnx + models)" ON)
set(SESSIO_TRANSCRIPTION_ENABLED ${SESSIO_ENABLE_TRANSCRIPTION})

if(NOT SESSIO_ENABLE_TRANSCRIPTION)
  return()
endif()

include(FetchContent)

# The prebuilt Linux sherpa-onnx tarballs use the old std::string ABI and cannot
# be used with cxx-api.h, so build from source on every platform.
foreach(_opt PYTHON TESTS CHECK PORTAUDIO WEBSOCKET BINARY TTS SPEAKER_DIARIZATION GPU)
  set(SHERPA_ONNX_ENABLE_${_opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(SHERPA_ONNX_ENABLE_C_API ON CACHE BOOL "" FORCE)
set(SHERPA_ONNX_BUILD_C_API_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(sherpa_onnx
  GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
  GIT_TAG v1.13.8
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL)

# A normal (non-cache) variable keeps shared libraries scoped to sherpa-onnx.
set(_sessio_saved_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS ON)
FetchContent_MakeAvailable(sherpa_onnx)
set(BUILD_SHARED_LIBS "${_sessio_saved_shared}")
set(SESSIO_SHERPA_SOURCE_DIR "${sherpa_onnx_SOURCE_DIR}")
