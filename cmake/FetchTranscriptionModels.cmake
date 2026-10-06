# Downloads the GigaAM v3 RNN-T model and Silero VAD, verifies each file against a
# pinned SHA-256 and lays them out as:
#   <MODELS_DIR>/silero_vad.onnx
#   <MODELS_DIR>/gigaam-v3-rnnt/{encoder.int8.onnx,decoder.onnx,joiner.onnx,tokens.txt}
#   <MODELS_DIR>/gigaam-v3-rnnt/test_wavs/example.wav   (used by the model tests)
if(NOT MODELS_DIR)
  message(FATAL_ERROR "MODELS_DIR is required")
endif()

set(_base "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models")
set(_archive "sherpa-onnx-nemo-transducer-giga-am-v3-russian-2025-12-16")
set(_model "${MODELS_DIR}/gigaam-v3-rnnt")

# relative path inside _model -> sha256
set(_files
  "encoder.int8.onnx|0ec54e5f130a91c0f228d21795ede10b31c992a1f73e45dcd827e843a71a9b50"
  "decoder.onnx|5d1cfef155d8e07f213bd2f3229b5f6c5c29355c7fa5c1b9eb62cb1925725254"
  "joiner.onnx|fd1d02f45c2ad3d6b67cc149811ad794ab4b020ed49a0a9e2790a8619d1cddd8"
  "tokens.txt|17cc514451bcceac9c280068c71502f8448f99e9fb1456b8d0761651fd0392f2"
  "test_wavs/example.wav|d8aaaa18a5098d7c6de0595ae7ac1e64cacd0d4022af3595213bdaf23be77e69")
set(_vad_sha "9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6")

function(_file_ok path sha result_var)
  set(${result_var} FALSE PARENT_SCOPE)
  if(EXISTS "${path}")
    file(SHA256 "${path}" _actual)
    if(_actual STREQUAL sha)
      set(${result_var} TRUE PARENT_SCOPE)
    endif()
  endif()
endfunction()

set(_model_ok TRUE)
foreach(_entry IN LISTS _files)
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _rel)
  list(GET _parts 1 _sha)
  _file_ok("${_model}/${_rel}" "${_sha}" _ok)
  if(NOT _ok)
    set(_model_ok FALSE)
  endif()
endforeach()

if(NOT _model_ok)
  file(MAKE_DIRECTORY "${MODELS_DIR}")
  set(_tarball "${MODELS_DIR}/${_archive}.tar.bz2")
  message(STATUS "Downloading GigaAM v3 RNN-T (about 160 MB)")
  file(DOWNLOAD "${_base}/${_archive}.tar.bz2" "${_tarball}" SHOW_PROGRESS STATUS _status)
  list(GET _status 0 _code)
  if(NOT _code EQUAL 0)
    file(REMOVE "${_tarball}")
    message(FATAL_ERROR "GigaAM download failed: ${_status}")
  endif()
  file(REMOVE_RECURSE "${MODELS_DIR}/extract")
  file(ARCHIVE_EXTRACT INPUT "${_tarball}" DESTINATION "${MODELS_DIR}/extract")
  file(REMOVE_RECURSE "${_model}")
  file(MAKE_DIRECTORY "${_model}/test_wavs")
  foreach(_entry IN LISTS _files)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _rel)
    list(GET _parts 1 _sha)
    get_filename_component(_dir "${_rel}" DIRECTORY)
    file(COPY "${MODELS_DIR}/extract/${_archive}/${_rel}" DESTINATION "${_model}/${_dir}")
  endforeach()
  file(REMOVE_RECURSE "${MODELS_DIR}/extract")
  file(REMOVE "${_tarball}")
endif()

foreach(_entry IN LISTS _files)
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _rel)
  list(GET _parts 1 _sha)
  _file_ok("${_model}/${_rel}" "${_sha}" _ok)
  if(NOT _ok)
    message(FATAL_ERROR "Hash mismatch for ${_model}/${_rel} (expected ${_sha})")
  endif()
endforeach()

_file_ok("${MODELS_DIR}/silero_vad.onnx" "${_vad_sha}" _vad_ok)
if(NOT _vad_ok)
  message(STATUS "Downloading Silero VAD")
  file(DOWNLOAD "${_base}/silero_vad.onnx" "${MODELS_DIR}/silero_vad.onnx"
       EXPECTED_HASH SHA256=${_vad_sha} SHOW_PROGRESS)
endif()
