cmake_minimum_required(VERSION 3.20)
project(autoremesher_houdini CXX)

# The core builds against Houdini's toolkit so it shares the host's runtime and oneTBB.
find_package(Houdini REQUIRED)

set(AUTOREMESHER_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB AUTOREMESHER_CORE_SOURCES
    ${AUTOREMESHER_ROOT}/src/AutoRemesher/*.cpp
    ${AUTOREMESHER_ROOT}/thirdparty/isotropicremesher/*.cpp
    ${AUTOREMESHER_ROOT}/thirdparty/meshoptimizer/src/*.cpp
)

add_library(autoremesher_core STATIC ${AUTOREMESHER_CORE_SOURCES})

target_include_directories(autoremesher_core PUBLIC
    ${AUTOREMESHER_ROOT}/include
    ${AUTOREMESHER_ROOT}/src
    ${AUTOREMESHER_ROOT}/thirdparty/eigen
    ${AUTOREMESHER_ROOT}/thirdparty/isotropicremesher
    ${AUTOREMESHER_ROOT}/thirdparty/meshoptimizer/src
)

target_link_libraries(autoremesher_core PUBLIC Houdini)

set_target_properties(autoremesher_core PROPERTIES POSITION_INDEPENDENT_CODE ON)


add_library(SOP_AutoRemesher SHARED
    ${AUTOREMESHER_ROOT}/src/houdini/SOP_AutoRemesher.C
    ${AUTOREMESHER_ROOT}/src/houdini/SOP_AutoRemesher.h
)

target_link_libraries(SOP_AutoRemesher PRIVATE Houdini autoremesher_core)

houdini_configure_target(SOP_AutoRemesher)
