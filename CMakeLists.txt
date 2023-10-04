cmake_minimum_required(VERSION 3.10)
project(obs-mdk)


if(${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
	find_package(LibObs QUIET)
endif()


set(MDKSDK "${CMAKE_CURRENT_LIST_DIR}/mdk-sdk" CACHE STRING "libmdk SDK dir")

if(NOT CMAKE_PROJECT_NAME STREQUAL mdk) # not build in source tree
    list(APPEND CMAKE_MODULE_PATH ${MDKSDK}/lib/cmake)
endif()
find_package(MDK)

set(mdk-video_SOURCES
	plugin.c
	mdkvideo.cpp
	)

add_library(obs-mdk MODULE
	${mdk-video_SOURCES}
	)
add_library(OBS::mdk ALIAS obs-mdk)

if(APPLE)
  target_compile_options(obs-mdk PRIVATE -Wno-quoted-include-in-framework-header -Wno-newline-eof)
endif()
target_link_libraries(obs-mdk
	libobs
	mdk
	)

if(EXISTS ${MDK_FRAMEWORK})
    set_property(GLOBAL APPEND PROPERTY _OBS_FRAMEWORKS ${MDK_FRAMEWORK})
endif()

if(NOT ${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
  if(COMMAND legacy_check)
    legacy_check()
  endif()

# cmake-format: off
  if(COMMAND set_target_properties_obs)
    set_target_properties_obs(obs-mdk PROPERTIES FOLDER plugins/obs-mdk PREFIX "")
  endif()
# cmake-format: on
endif()
