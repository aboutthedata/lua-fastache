cmake_minimum_required(VERSION 3.19)
project(lua_fastache C)

find_package(Lua 5.3 REQUIRED)
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

BISON_TARGET(parser_tab src/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.c
             DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.h
             COMPILE_FLAGS "-p mustache_p_")
FLEX_TARGET(parser_lex src/parser.l ${CMAKE_CURRENT_BINARY_DIR}/parser.lex.c
            DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.lex.h
		    COMPILE_FLAGS "-Pmustache_p_ --bison-bridge -pp")
ADD_FLEX_BISON_DEPENDENCY(parser_lex parser_tab)

add_library(lua_fastache MODULE 
            src/lua-fastache.c
            ${BISON_parser_tab_OUTPUTS}
            ${FLEX_parser_lex_OUTPUTS}
           )

if(MSVC)
	target_compile_options(lua_fastache PRIVATE /W4)
else()
	target_compile_options(lua_fastache PRIVATE -Wall -Wextra)
endif()

target_compile_definitions(lua_fastache PRIVATE YYERROR_VERBOSE)
target_include_directories(lua_fastache PRIVATE src/ "${LUA_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(lua_fastache PROPERTIES
                      C_STANDARD 11
                      C_STANDARD_REQUIRED on
                      PREFIX "")
                      
function(FindLuaModulePath outvar)
	execute_process(COMMAND echo LUA_CPATH_DEFAULT
					COMMAND ${CMAKE_C_COMPILER} -imacros ${LUA_INCLUDE_DIR}/lua.h -imacros ${LUA_INCLUDE_DIR}/luaconf.h -E -P -
					OUTPUT_VARIABLE _gcc_output
					COMMAND_ERROR_IS_FATAL ANY)
	string(REGEX REPLACE "[ \t\r\n\"]+" "" _all_search_paths "${_gcc_output}")
	
	set(_endpattern "[/\\]\\?\.[A-Za-z]+$")
	list(FILTER _all_search_paths INCLUDE REGEX "${_endpattern}")
	list(GET _all_search_paths 0 _path)
	string(REGEX REPLACE "${_endpattern}" "" _path "${_path}")
	set(${outvar} "${_path}" PARENT_SCOPE)
endfunction()

FindLuaModulePath(DESTPATH)
install(TARGETS lua_fastache DESTINATION ${DESTPATH})
