# LadybugDB (Kuzu fork) for the rag-core index store. Prebuilt liblbug plus
# the fts/vector extension binaries live in one prefix; without it the index
# store is omitted and the rest of rag-core is unaffected.
set(SESSIO_LADYBUG_DIR "" CACHE PATH "LadybugDB prefix: liblbug.so, lbug.hpp, extension/{fts,vector}")
set(SESSIO_LADYBUG_FOUND OFF)

if(NOT SESSIO_LADYBUG_DIR)
  message(STATUS "RAG index store: not configured (set SESSIO_LADYBUG_DIR)")
  return()
endif()

find_library(SESSIO_LADYBUG_LIBRARY NAMES lbug PATHS "${SESSIO_LADYBUG_DIR}" NO_DEFAULT_PATH)
if(NOT SESSIO_LADYBUG_LIBRARY)
  message(FATAL_ERROR "SESSIO_LADYBUG_DIR=${SESSIO_LADYBUG_DIR} has no liblbug; download the LadybugDB release tarball")
endif()
if(NOT EXISTS "${SESSIO_LADYBUG_DIR}/extension/fts/libfts.lbug_extension" OR
   NOT EXISTS "${SESSIO_LADYBUG_DIR}/extension/vector/libvector.lbug_extension")
  message(FATAL_ERROR "SESSIO_LADYBUG_DIR is missing extension/fts or extension/vector binaries")
endif()

add_library(lbug::lbug SHARED IMPORTED)
set_target_properties(lbug::lbug PROPERTIES
  IMPORTED_LOCATION "${SESSIO_LADYBUG_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${SESSIO_LADYBUG_DIR}")
set(SESSIO_LADYBUG_FOUND ON)
message(STATUS "RAG index store: ${SESSIO_LADYBUG_LIBRARY}")
