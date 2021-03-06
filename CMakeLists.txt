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
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} /Od /MTd")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${PROJECT_NAME} main.cpp
    module.cpp module.h
    xm.cpp xm.h
    mixer.cpp mixer.h
    mod_player.cpp mod_player.h
    base/stream_util.h base/stream_util.cpp
    base/event.h
    base/job_queue.cpp base/job_queue.h
    base/sample.cpp base/sample.h
    base/voice.h
    base/sample_voice.cpp base/sample_voice.h
    base/note.cpp base/note.h
    base/virtual_grid.h
    win32/base.cpp win32/base.h
    win32/gdi.cpp win32/gdi.h
    win32/sample_window.cpp win32/sample_window.h
    win32/text_grid.cpp win32/text_grid.h
    win32/pattern_edit.cpp win32/pattern_edit.h
    win32/main_window.cpp win32/main_window.h
    win32/info_window.cpp win32/info_window.h
    win32/wavedev.cpp win32/wavedev.h
    )
target_link_libraries(${PROJECT_NAME} comctl32.lib)
