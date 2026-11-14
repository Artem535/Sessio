cmake_minimum_required(VERSION 3.28)

# Set the C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_config)

find_package(reflectcpp CONFIG REQUIRED)
find_package(Poco REQUIRED COMPONENTS Foundation)
find_package(Qt6 REQUIRED COMPONENTS Core)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Poco DEFAULT_MSG Poco_FOUND)
find_package_handle_standard_args(reflectcpp DEFAULT_MSG reflectcpp_FOUND)


# Add the library for the config module
qt_add_library(${TARGET_NAME} STATIC
        app_role.h
        config.cpp
        rfl_path.hpp
)

# Include directories
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link necessary libraries
target_link_libraries(${TARGET_NAME} PRIVATE reflectcpp::reflectcpp Poco::Foundation)
target_link_libraries(${TARGET_NAME} PUBLIC Qt6::Core)