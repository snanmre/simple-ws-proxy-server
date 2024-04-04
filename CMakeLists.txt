cmake_minimum_required(VERSION 3.0.0)
project(
  ws-proxy
  VERSION 0.1.0
  LANGUAGES C CXX)

include(CTest)
enable_testing()

add_library(mongoose mongoose.h mongoose.c)
# target_compile_definitions(mongoose PUBLIC -DMG_TLS=MG_TLS_BUILTIN)
target_compile_definitions(mongoose PUBLIC -DMG_ENABLE_OPENSSL -DMG_TLS=MG_TLS_OPENSSL)
target_link_libraries(mongoose PUBLIC -lssl -lcrypto)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE mongoose)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
