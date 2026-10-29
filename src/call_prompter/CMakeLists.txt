cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

# Agreement extraction pipeline: protocol validation and prompts. Free of
# LiteRT-LM so it can be tested with a scripted model.
set(TARGET_NAME ${PROJECT_NAME}_call_prompter)
add_library(${TARGET_NAME} STATIC
  prompter_pipeline.h
  prompter_pipeline.cpp
  notes_common.h
  notes_common.cpp
  live_points_pipeline.h
  live_points_pipeline.cpp
  summary_pipeline.h
  summary_pipeline.cpp
  prompter_engine.h
  cloud_engine.h
  cloud_engine.cpp
)
qt_add_resources(${TARGET_NAME} call_prompter_prompts
  PREFIX /call-prompter
  BASE prompts
  FILES prompts/extract.txt prompts/match.txt prompts/live_points.txt
        prompts/summary_leaf.txt prompts/summary_overview.txt)
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(reflectcpp CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC Qt6::Core Qt6::Network PRIVATE reflectcpp::reflectcpp)

# Gemma through the LiteRT-LM C++ API, or a stub without the runtime.
set(ENGINE_TARGET ${PROJECT_NAME}_call_prompter_engine)
if(SESSIO_LITERTLM_FOUND)
  add_library(${ENGINE_TARGET} STATIC gemma_engine.cpp)
  target_link_libraries(${ENGINE_TARGET} PRIVATE sessio::litert_lm)
else()
  add_library(${ENGINE_TARGET} STATIC gemma_engine_stub.cpp)
endif()
target_link_libraries(${ENGINE_TARGET} PUBLIC ${TARGET_NAME})
