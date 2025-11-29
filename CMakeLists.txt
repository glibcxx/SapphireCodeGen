cmake_minimum_required(VERSION 3.20)
project(SapphireCodeGen LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")
include_directories(${CLANG_INCLUDE_DIRS})

add_executable(SapphireCodeGen src/SapphireCodeGen.cpp src/SigDatabase.cpp)

llvm_map_components_to_libnames(llvm_libs
    Support Core Option Demangle
)

target_link_libraries(SapphireCodeGen PRIVATE
    clangTooling
    clangFrontend
    clangSerialization
    clangDriver
    clangParse
    clangSema
    clangAnalysis
    clangAST
    clangASTMatchers
    clangLex
    clangBasic
    clangEdit
    ${llvm_libs}
)

if(MSVC)
    target_compile_options(SapphireCodeGen PRIVATE /wd4141 /wd4146 /wd4244 /wd4267 /wd4624)
endif()
