cmake_minimum_required(VERSION 3.10)
project(mdk-video)
include_directories(include)
link_directories(lib/x86 lib/x64 lib)

if(${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
	find_package(LibObs QUIET)
endif()

set(mdk-video_SOURCES
	plugin.c
	mdkvideo.cpp
	)

add_library(mdk-video MODULE
	${mdk-video_SOURCES}
	)
target_link_libraries(mdk-video
	libobs
	mdk
	)

if(NOT ${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
    install_obs_plugin_with_data(mdk-video data)
endif()
