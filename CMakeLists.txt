cmake_minimum_required( VERSION 2.8.12 )

# we expect LLVM_ROOT to be defined
if( NOT LLVM_ROOT )
  message( FATAL_ERROR "please set LLVM_ROOT to the root of your LLVM build" )
endif()

# get LLVM include and library deps
execute_process( COMMAND ${LLVM_ROOT}/build/bin/llvm-config --cxxflags 
  OUTPUT_VARIABLE LLVM_CXXFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE )
execute_process( COMMAND ${LLVM_ROOT}/build/bin/llvm-config --ldflags
  OUTPUT_VARIABLE LLVM_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE )
execute_process( COMMAND ${LLVM_ROOT}/build/bin/llvm-config --libs
  OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE )
execute_process( COMMAND ${LLVM_ROOT}/build/bin/llvm-config --system-libs
  OUTPUT_VARIABLE LLVM_SYSLIBS OUTPUT_STRIP_TRAILING_WHITESPACE )

include_directories( ${LLVM_ROOT}/tools/clang/include
                     ${LLVM_ROOT}/build/tools/clang/include
                     ${LLVM_ROOT}/include
                   )

add_definitions( -std=c++1y -fno-rtti )    # -fno-rtti inhibits linker error for some reason *shrugs*
if (CMAKE_COMPILER_IS_GNUCC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
  add_definitions(-Wall -Wextra -Werror)  # be conservative about checks
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")

add_executable( rs2 refactor_stage2.cpp )
set_target_properties( rs2 PROPERTIES COMPILE_FLAGS "${LLVM_CXXFLAGS}" )


target_link_libraries( rs2
        -Wl,--start-group
	clangAST
	clangAnalysis
	clangBasic
	clangDriver
	clangEdit
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
        -Wl,--end-group
        ${LLVM_LIBS}
        ${LLVM_SYSLIBS}
)
set_target_properties( rs2 PROPERTIES
                       LINK_FLAGS "${LLVM_LDFLAGS}" )

# test code
add_executable( t test.cpp )
