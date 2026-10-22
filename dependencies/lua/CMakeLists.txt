# Lua as a static C library for the White Bird Engine. The standalone interpreter (lua.c) and compiler (luac.c) are not
# built.
cmake_minimum_required(VERSION 3.16)
project(lua C)

file(GLOB lua_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM lua_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lua.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/luac.c
)

add_library(lua STATIC ${lua_sources})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The engine may be built as a shared library that links this archive.
set_target_properties(lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
endif()
if (UNIX AND NOT APPLE)
    target_link_libraries(lua PRIVATE m)
endif()
