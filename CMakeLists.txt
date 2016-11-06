cmake_minimum_required(VERSION 3.3)
project(sampedit)

if (NOT MSVC)
    message(FATAL_ERROR "MSVC required")
endif()


set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported build configurations" FORCE)

add_definitions("/W4")
#add_definitions("/wd4267") # C4267: 'argument': conversion from 'X' to 'Y', possible loss of data
#add_definitions("/wd4244") # C4244: 'initializing': conversion from 'X' to 'Y', possible loss of data
#add_definitions("/wd4319") # C4319: '~': zero extending 'X' to 'Y' of greater size
#add_definitions("/wd4193") # C4193: #pragma warning(pop): no matching '#pragma warning(push)'

add_definitions("-D_SCL_SECURE_NO_WARNINGS")
add_definitions("-DUNICODE -D_UNICODE")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /MT /DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} /MTd")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${PROJECT_NAME} main.cpp
    win32/base.cpp win32/base.h
    win32/gdi.cpp win32/gdi.h
    )
