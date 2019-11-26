cmake_minimum_required(VERSION 3.10)
project(mdk-video)

find_package(LibObs QUIET)

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

#install_obs_plugin_with_data(mdk-video data)
