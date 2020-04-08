cmake_minimum_required(VERSION 3.10)
project(mdk-video)
if(NOT EXISTS ${MDK_SDK_DIR})
	set(MDK_SDK_DIR ${CMAKE_CURRENT_LIST_DIR}/mdk-sdk)
endif()
include(${MDK_SDK_DIR}/lib/cmake/FindMDK.cmake)

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
