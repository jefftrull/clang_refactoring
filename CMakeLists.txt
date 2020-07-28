#
#    Copyright (C) 2015 Jeff Trull <edaskel@att.net>
# 
#    Distributed under the Boost Software License, Version 1.0. (See accompanying
#    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
# 
# 
#

cmake_minimum_required( VERSION 3.12 )

project( clang_refactoring )

set( CMAKE_CXX_STANDARD 14 )
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

# We will pick up LLVM via the somewhat more modern "Config" route
# so set CMAKE_PREFIX_PATH accordingly

find_package( Clang )

find_package( Boost 1.53 REQUIRED COMPONENTS system wave thread filesystem )

include_directories( SYSTEM ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS} )

add_definitions( -fno-rtti )    # -fno-rtti inhibits linker error for some reason *shrugs*
if (CMAKE_COMPILER_IS_GNUCC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
  add_definitions(-Wall -Wextra -Werror)  # be conservative about checks
  if (CMAKE_COMPILER_IS_GNUCC)
    add_definitions(-Wno-error=strict-aliasing) # this one is triggered by Clang AST
  endif()
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")

# stage 1 via Boost.wave

# Stage 1 needs to know what include paths to use.  We will use those of the build compile
# by asking it what it uses, and creating a cpp file containing them:
execute_process( COMMAND ${CMAKE_CXX_COMPILER} -E -x c++ - -v
  INPUT_FILE /dev/null
  ERROR_VARIABLE CXXERR
  OUTPUT_QUIET
  ERROR_STRIP_TRAILING_WHITESPACE
  )

# remove initial spaces in path list
string( REPLACE "\n " "\n" CXXLIST "${CXXERR}" )
# turn embedded newlines into semicolons, and thus into a CMake list
string( REPLACE "\n" ";" CXXLIST "${CXXLIST}" )

# treating the output like a string, find the start and end of the include paths
set( INCLSTARTSTR "\#include <...> search starts here:;" )   # preamble
string( LENGTH "${INCLSTARTSTR}" INCLSTARTLEN )
string( FIND "${CXXLIST}" ${INCLSTARTSTR} INCLSTART )        # locate preamble
math( EXPR INCLSTART ${INCLSTART}+${INCLSTARTLEN})           # move past it
string( FIND "${CXXLIST}" "End of search list." INCLEND )    # find postscript
math( EXPR INCLLEN ${INCLEND}-${INCLSTART}-1 )               # truncate final semicolon
string( SUBSTRING "${CXXLIST}" ${INCLSTART} ${INCLLEN} INCLPATHLIST )

# use "generator expressions" to join the paths as an initializer list
file( GENERATE OUTPUT ${CMAKE_BINARY_DIR}/default_include_paths.cpp
  CONTENT "\#include <vector>\n\#include <string>\nstd::vector<std::string>\nipaths={\n\"$<JOIN:${INCLPATHLIST},\",\n\">\"\n};\n" )

# Stage 1 also needs a list of any predefined macros the compiler uses
execute_process( COMMAND ${CMAKE_CXX_COMPILER} -dM -E -
  INPUT_FILE /dev/null
  OUTPUT_VARIABLE PREDEFS
  )
# turn those definitions (of the form #define X Y) into X=Y form for supplying to Wave
string( REPLACE "\#define " "" PREDEFS "${PREDEFS}" )  # remove define, leaving "X Y"
string( REPLACE " " "=" PREDEFS "${PREDEFS}" )         # produces "X=Y"
string( REPLACE "\"" "\\\"" PREDEFS "${PREDEFS}" )     # escapes double quotes in string macros
string( REPLACE "\n" ";" PREDEFS "${PREDEFS}" )        # create CMake list X1=Y1;X2=Y2;...
file( GENERATE OUTPUT ${CMAKE_BINARY_DIR}/predefined_macros.cpp
  CONTENT "\#include <vector>\n\#include <string>\nstd::vector<std::string>\npredefs={\n\"$<JOIN:${PREDEFS},\",\n\">\"\n};\n" )

add_library( compiler_info ${CMAKE_BINARY_DIR}/default_include_paths.cpp ${CMAKE_BINARY_DIR}/predefined_macros.cpp )

add_executable( rs1 refactor_stage1.cpp )
target_link_libraries( rs1 Boost::system Boost::filesystem Boost::wave compiler_info )

add_executable( rs2 refactor_stage2.cpp )
set_target_properties( rs2 PROPERTIES COMPILE_FLAGS "${LLVM_CXXFLAGS}" )


set( CLANG_AST_LIBS
        -Wl,--start-group
	clangAST
	clangAnalysis
	clangBasic
	clangDriver
	clangEdit
        clangFormat
	clangFrontend
	clangFrontendTool
	clangLex
	clangParse
	clangSema
	clangEdit
	clangASTMatchers
	clangRewrite
	clangRewriteFrontend
	clangStaticAnalyzerFrontend
	clangStaticAnalyzerCheckers
	clangStaticAnalyzerCore
	clangSerialization
	clangToolingCore
	clangTooling
	clangToolingInclusions
        -Wl,--end-group
        ${LLVM_LIBS}
        ${LLVM_SYSLIBS}
)

target_link_libraries( rs2 ${CLANG_AST_LIBS} )


# test code
add_executable( t test.cpp )
