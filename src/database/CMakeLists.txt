set(TARGET_NAME ${PROJECT_NAME}_database)

find_package(Poco REQUIRED COMPONENTS Foundation)
find_package(plog CONFIG REQUIRED)
find_package(DuckDB CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)

add_library(${TARGET_NAME} STATIC
        database.cpp
        prompter_database.cpp
        call_material_database.cpp
        constants.hpp
        datetime.hpp
        schema.hpp
        db_utils.hpp
)

# Include directories
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_dependencies(${TARGET_NAME} ${PROJECT_NAME}_config)

target_link_libraries(${TARGET_NAME}
  PUBLIC
  ${PROJECT_NAME}_config
  Poco::Foundation
  plog::plog
  ${DuckDB_LIBRARIES}
  PRIVATE reflectcpp::reflectcpp
)
