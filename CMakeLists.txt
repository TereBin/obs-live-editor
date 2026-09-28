cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
if(DEFINED ENV{CHZZK_BROKER_URL} AND NOT "$ENV{CHZZK_BROKER_URL}" STREQUAL "")
  set(_default_broker_url "$ENV{CHZZK_BROKER_URL}")
else()
  set(_default_broker_url "https://YOUR-WORKER.workers.dev")
endif()
set(CHZZK_BROKER_URL "${_default_broker_url}" CACHE STRING "OAuth broker base URL")
unset(_default_broker_url)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CHZZK_BROKER_URL="${CHZZK_BROKER_URL}")

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/chzzk-api-client.cpp
    src/chzzk-api-client.hpp
    src/chzzk-dock.cpp
    src/chzzk-dock.hpp
    src/oauth-callback-server.cpp
    src/oauth-callback-server.hpp
    src/plugin-main.cpp
    src/token-store.cpp
    src/token-store.hpp
)

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Crypt32)
  if(TARGET Qt6::QSchannelBackendPlugin)
    install(FILES "$<TARGET_FILE:Qt6::QSchannelBackendPlugin>" DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit/tls")
  else()
    message(FATAL_ERROR "Qt Schannel TLS backend was not found")
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
