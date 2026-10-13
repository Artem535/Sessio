find_package(GTest REQUIRED)
include(GoogleTest)

# PRE_TEST discovery everywhere: nothing runs at build time (the model tests
# need the sherpa/ONNX Runtime DLLs and the model files).
add_executable(Sessio_transcription_tests
    transcription_locator_tests.cpp
    transcription_resampler_tests.cpp
    transcription_segmenter_tests.cpp
    transcription_engine_tests.cpp
    transcription_test_support.h
)
target_link_libraries(Sessio_transcription_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_transcription
)
gtest_discover_tests(Sessio_transcription_tests DISCOVERY_MODE PRE_TEST)

add_executable(Sessio_transcription_model_tests transcription_model_tests.cpp)
target_include_directories(Sessio_transcription_model_tests PRIVATE "${SESSIO_SHERPA_SOURCE_DIR}")
target_link_libraries(Sessio_transcription_model_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_transcription
    Sessio_transcription_sherpa
    sherpa-onnx-cxx-api
)
set_target_properties(Sessio_transcription_model_tests PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/lib;${SESSIO_ONNXRUNTIME_LIB_DIR}")
if(WIN32)
  # The exe needs the sherpa/ONNX Runtime DLLs beside it (no rpath on Windows).
  add_custom_command(TARGET Sessio_transcription_model_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:sherpa-onnx-c-api>
          $<TARGET_FILE:sherpa-onnx-cxx-api>
          ${SESSIO_ONNXRUNTIME_LIBS}
          $<TARGET_FILE_DIR:Sessio_transcription_model_tests>
      VERBATIM)
endif()
gtest_discover_tests(Sessio_transcription_model_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS models
               ENVIRONMENT "SESSIO_MODELS_DIR=${SESSIO_MODELS_DIR}")
add_dependencies(Sessio_transcription_model_tests sessio_models)
