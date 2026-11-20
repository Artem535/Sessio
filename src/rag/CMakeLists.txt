set(TARGET_NAME ${PROJECT_NAME}_rag)

# Self-contained rag-core: no Qt and no Sessio module dependencies, so the
# module can move to CppWiki as a plugin (Artem535/CppWiki#242) unchanged.
add_library(${TARGET_NAME} STATIC
  chunk.h
  entities.cpp
  entities.h
  markdown_chunker.cpp
  markdown_chunker.h
  rewrite.cpp
  rewrite.h
  router.cpp
  router.h
  router_head.cpp
  router_head.h
)
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(${TARGET_NAME} PUBLIC cxx_std_20)

# Outline connector: the only Qt-dependent part of rag-core so far. Kept as a
# separate target so the chunker/router core stays Qt-free for the CppWiki
# plugin port (Artem535/CppWiki#242).
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_library(${TARGET_NAME}_outline STATIC
  outline_connector.cpp
  outline_connector.h
)
target_link_libraries(${TARGET_NAME}_outline PUBLIC ${TARGET_NAME} Qt6::Core Qt6::Network)

# LadybugDB index store: optional, needs the prebuilt liblbug prefix.
if(SESSIO_LADYBUG_FOUND)
  add_library(${TARGET_NAME}_index STATIC
    index_store.cpp
    index_store.h
    search.cpp
    search.h
  )
  target_link_libraries(${TARGET_NAME}_index PUBLIC ${TARGET_NAME} lbug::lbug)

  # RagWorker: integration layer. Uses the sessio_litert Embedder interface
  # via injected factories, so it does not link the runtime library itself.
  add_library(${TARGET_NAME}_worker STATIC
    rag_worker.cpp
    rag_worker.h
  )
  target_link_libraries(${TARGET_NAME}_worker PUBLIC
    ${TARGET_NAME}_index ${TARGET_NAME}_outline Qt6::Core Qt6::Network)
  target_include_directories(${TARGET_NAME}_worker PUBLIC
    ${CMAKE_SOURCE_DIR}/src/call_prompter/litert)
endif()
