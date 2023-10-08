cmake_minimum_required(VERSION 3.10)
project(obs-mdk)


if(${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
	find_package(libobs)
	if(NOT libobs_FOUND)
	  find_package(LibObs)
  endif()
endif()
if(NOT TARGET OBS::libobs AND TARGET libobs)
  add_library(OBS::libobs ALIAS libobs)
endif()

set(MDKSDK "${CMAKE_CURRENT_LIST_DIR}/mdk-sdk" CACHE STRING "libmdk SDK dir")

if(NOT CMAKE_PROJECT_NAME STREQUAL mdk) # not build in source tree
    list(APPEND CMAKE_MODULE_PATH ${MDKSDK}/lib/cmake)
endif()
find_package(MDK)

add_library(${PROJECT_NAME} MODULE
  plugin.c
  mdkvideo.cpp
	)
add_library(OBS::mdk ALIAS ${PROJECT_NAME})

if(APPLE)
  target_compile_options(${PROJECT_NAME} PRIVATE -Wno-quoted-include-in-framework-header -Wno-newline-eof)
endif()
target_link_libraries(${PROJECT_NAME}
	OBS::libobs
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
    set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER plugins/${PROJECT_NAME} PREFIX "")
  endif()
# cmake-format: on
elseif(EXISTS ${LIBOBS_PLUGIN_DESTINATION}) # linux
  install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${LIBOBS_PLUGIN_DESTINATION}
    LIBRARY DESTINATION ${LIBOBS_PLUGIN_DESTINATION}
  )
  install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/
    DESTINATION ${LIBOBS_PLUGIN_DATA_DESTINATION}/${PROJECT_NAME}
  )
  if(MDK_RUNTIMES)
    install(
      FILES ${MDK_RUNTIMES} # libmdk.so.0 can be loaded by MDK_PLUGINS from RUNPATH
      DESTINATION ${LIBOBS_PLUGIN_DESTINATION}
      COMPONENT ${PROJECT_NAME}_Runtime
      OPTIONAL
    )
  endif()
endif()
