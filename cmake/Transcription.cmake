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

set(SESSIO_MODELS_DIR "${CMAKE_BINARY_DIR}/transcription-models" CACHE PATH
    "Where the transcription model files are downloaded to (cache this in CI)")
set(_sessio_models_stamp "${SESSIO_MODELS_DIR}/.stamp")
add_custom_command(
  OUTPUT "${_sessio_models_stamp}"
  COMMAND ${CMAKE_COMMAND} -DMODELS_DIR=${SESSIO_MODELS_DIR}
          -P ${CMAKE_SOURCE_DIR}/cmake/FetchTranscriptionModels.cmake
  COMMAND ${CMAKE_COMMAND} -E touch "${_sessio_models_stamp}"
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/FetchTranscriptionModels.cmake
  COMMENT "Fetching transcription models"
  VERBATIM)
add_custom_target(sessio_models ALL DEPENDS "${_sessio_models_stamp}")

# Directory of ONNX Runtime fetched by sherpa-onnx.
FetchContent_GetProperties(onnxruntime SOURCE_DIR _sessio_ort_dir)
set(SESSIO_ONNXRUNTIME_LIB_DIR "${_sessio_ort_dir}/lib")

# ONNX Runtime: libonnxruntime.so* (Linux), libonnxruntime*.dylib (macOS),
# onnxruntime*.dll (Windows; the DLL sits in lib/ or ../bin of the archive).
file(GLOB SESSIO_ONNXRUNTIME_LIBS
    "${SESSIO_ONNXRUNTIME_LIB_DIR}/libonnxruntime*.so*"
    "${SESSIO_ONNXRUNTIME_LIB_DIR}/libonnxruntime*.dylib"
    "${SESSIO_ONNXRUNTIME_LIB_DIR}/onnxruntime*.dll"
    "${SESSIO_ONNXRUNTIME_LIB_DIR}/../bin/onnxruntime*.dll")
if(NOT SESSIO_ONNXRUNTIME_LIBS)
  message(FATAL_ERROR
      "ONNX Runtime libraries not found. Searched: "
      "${SESSIO_ONNXRUNTIME_LIB_DIR} and ${SESSIO_ONNXRUNTIME_LIB_DIR}/../bin "
      "(patterns libonnxruntime*.so*, libonnxruntime*.dylib, onnxruntime*.dll). "
      "sherpa-onnx may have changed where it unpacks ONNX Runtime.")
endif()
