cmake_minimum_required(VERSION 3.21)
project(webengine_audio_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineCore WebEngineWidgets WebChannel)

qt_add_executable(audio_bridge_spike
  main.cpp
  audio_stats.cpp audio_stats.h
  ws_server.cpp ws_server.h
  scheme_handler.cpp scheme_handler.h
)

qt_add_resources(audio_bridge_spike web
  PREFIX /
  FILES web/index.html web/app.js web/pcm-tap.js
)

target_link_libraries(audio_bridge_spike PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
  Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::WebChannel)
