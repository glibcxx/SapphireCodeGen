cmake_minimum_required(VERSION 3.20)
project(SapphireCodeGen LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

find_package(mimalloc REQUIRED CONFIG)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")
include_directories(${CLANG_INCLUDE_DIRS})

add_executable(SapphireCodeGen
    src/SapphireCodeGen.cpp
    src/codegen/SigDatabase.cpp
    src/codegen/Application.cpp
    src/codegen/CommandLine.cpp
    src/codegen/FileProcessor.cpp
    src/codegen/PCHGenerator.cpp
    src/codegen/ASTParser.cpp
    src/codegen/SignatureGenerator.cpp
    src/codegen/HeaderGenerator.cpp
)

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
    $<IF:$<TARGET_EXISTS:mimalloc-static>,mimalloc-static,mimalloc>
)

if(MSVC)
    target_compile_options(SapphireCodeGen PRIVATE /wd4141 /wd4146 /wd4244 /wd4267 /wd4624)
endif()

include(GNUInstallDirs)

install(TARGETS SapphireCodeGen
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

string(REGEX MATCH "^[0-9]+" CLANG_MAJOR_VERSION "${LLVM_PACKAGE_VERSION}")
get_filename_component(LLVM_INSTALL_PREFIX "${LLVM_LIBRARY_DIRS}" DIRECTORY)
set(CLANG_RESOURCE_DIR "${LLVM_INSTALL_PREFIX}/lib/clang/${CLANG_MAJOR_VERSION}")

if(NOT IS_DIRECTORY "${CLANG_RESOURCE_DIR}")
    get_filename_component(LLVM_INSTALL_PREFIX "${LLVM_INSTALL_PREFIX}" DIRECTORY)
    set(CLANG_RESOURCE_DIR "${LLVM_INSTALL_PREFIX}/lib/clang/${CLANG_MAJOR_VERSION}")
endif()

if(IS_DIRECTORY "${CLANG_RESOURCE_DIR}")
    message(STATUS "Found Clang resource directory: ${CLANG_RESOURCE_DIR}")
    install(DIRECTORY "${CLANG_RESOURCE_DIR}/"
        DESTINATION "lib/clang/${CLANG_MAJOR_VERSION}")
else()
    message(FATAL_ERROR "Could not find Clang resource directory. Please set CLANG_RESOURCE_DIR manually.")
endif()

install(CODE "
    include(BundleUtilities)
    set(BUNDLE_EXECUTABLE \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/SapphireCodeGen.exe\")
    set(BUNDLE_SEARCH_PATHS
        \"${LLVM_LIBRARY_DIRS}\"
        )
    fixup_bundle(\"\${BUNDLE_EXECUTABLE}\" \"\" \"\${BUNDLE_SEARCH_PATHS}\")
"    COMPONENT Runtime)


# Add a target that is equivalent to the install command
add_custom_target(install-SapphireCodeGen
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
    COMMENT "Install SapphireCodeGen"
)
add_dependencies(install-SapphireCodeGen SapphireCodeGen)
