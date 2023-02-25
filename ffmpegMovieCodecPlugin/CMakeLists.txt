# Author: LiXizhi
# Company: ParaEngine.com
# Date: 2014.5.1
# Desc: movie encoder using precompiled version of ffmpeg. See http://ffmpeg.zeranoe.com/builds/
# add environment variable NPLRUNTIME_ROOT that point to git root of NPLRuntime.

# NOTE: modify the root directory path if it is installed at a different location. 
# this is ffmpeg v3.X in 2018, there are breaking API changes since mmpeg v5

if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
	set (ffmpeg_Root ${CMAKE_CURRENT_SOURCE_DIR}/../x64-windows-static)
else()
	#set (ffmpeg_Root ${CMAKE_CURRENT_SOURCE_DIR}/../x86)
	set (ffmpeg_Root ${CMAKE_CURRENT_SOURCE_DIR}/../x86-windows-static)
endif()


# Note: include the ParaEngine/NPLRuntime include directory
if ("$ENV{NPLRUNTIME_ROOT}" STREQUAL "")
	set(ParaEngineClient_SOURCE_DIR ${CLIENT_SOURCE_DIR}/trunk/ParaEngineClient)
else()
	set(ParaEngineClient_SOURCE_DIR $ENV{NPLRUNTIME_ROOT}/Client/trunk/ParaEngineClient)
endif()


IF(EXISTS "${ffmpeg_Root}")

cmake_minimum_required(VERSION 2.6)

project (MovieCodecPlugin)

set (ffmpeg_INCLUDE_DIR ${ffmpeg_Root}/include)

SET(MovieCodecPlugin_SRCS
	stdafx.h
	stdafx.cpp
	MovieCodecPlugin.h
	MovieCodecPlugin.cpp
	MovieCodecPlugin.def
	MovieCodec.h
	MovieCodec.cpp
	AudioCapture.h
	AudioCapture.cpp
	CaptureFrameData.h
	CaptureFrameData.cpp
	AudioMixer.h
	AudioMixer.cpp
	AudioRecordAudioFromMicrophone.h
	AudioRecordAudioFromMicrophone.cpp
	readme.txt
	../README.md
	AddCompileLinkFlags.cmake
)

# add the binary tree to the search path for include files
include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${ParaEngineClient_SOURCE_DIR}")
include_directories("${ParaEngineClient_SOURCE_DIR}/Core")
include_directories("${ParaEngineClient_SOURCE_DIR}/util")
include_directories("${ffmpeg_INCLUDE_DIR}")

IF(MSVC)
	# statically link MSVC to reduce dependancies
	foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
	 	if(${flag_var} MATCHES "/MD")
			string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
	 	endif(${flag_var} MATCHES "/MD")
	 	if(${flag_var} MATCHES "/MDd")
	 		string(REGEX REPLACE "/MDd" "/MTd" ${flag_var} "${${flag_var}}")
		endif(${flag_var} MATCHES "/MDd")
	endforeach(flag_var)
	
	# /GR- Remove RTTI to miminize the executable size
	SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /GR-")
	SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")
	# add a stripped version of symbol files during release build
	#SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LARGEADDRESSAWARE /opt:ref /DEBUG /PDBSTRIPPED:Release/MovieCodecPlugin.stripped.pdb")
ENDIF()


IF(MSVC)
	# generate program database (PDB symbol files even in release build)
	SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
	SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")
ENDIF(MSVC)

# SET(CMAKE_DEBUG_POSTFIX "_d")

link_directories("${ffmpeg_Root}/lib/")

link_libraries(MovieCodecPlugin 
	avfilter 
	avcodec 
	avutil 
	avformat 
	swscale 
	swresample 

	libpostproc 
	
	libx264 
	
	#mp3lame
	libmp3lame-static
	libmpghip-static

	advapi32 
	Winmm 
	Avrt
	bcrypt

	fdk-aac

	dxva2
	evr
	mf
	mfplat
	mfplay
	mfreadwrite
	mfuuid
	ws2_32
	Secur32
	Strmiids
)


# add the executable
add_library(MovieCodecPlugin SHARED ${MovieCodecPlugin_SRCS})




IF(WIN32)
	ADD_DEFINITIONS(-DWNT)
ENDIF(WIN32)

if(MSVC)
	# disable SAFESEH - to avoid "LNK2026: module unsafe" on "ffmpeg lib"
    SET(CMAKE_CXX_FLAGS           "${CMAKE_CXX_FLAGS} /D \"SAFESEH:NO\"")
    SET(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
    SET(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /SAFESEH:NO")


	get_target_property(DEBUG_EXE_PATH MovieCodecPlugin DEBUG_LOCATION)
	STRING(REGEX REPLACE "^.*[/\\]([^/\\]+)$" "\\1" DEBUG_EXE_NAME "${DEBUG_EXE_PATH}" )

	get_target_property(RELEASE_EXE_PATH MovieCodecPlugin RELEASE_LOCATION)
	STRING(REGEX REPLACE "^.*[/\\]([^/\\]+)$" "\\1" RELEASE_EXE_NAME "${RELEASE_EXE_PATH}" )

	if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
		set( OUTPUT_BIN_DIR "${PROJECT_SOURCE_DIR}/../Mod/MovieCodecPlugin/bin64"  )
	else()
		set( OUTPUT_BIN_DIR "${PROJECT_SOURCE_DIR}/../Mod/MovieCodecPlugin/" )
	endif()

	ADD_CUSTOM_COMMAND(
		TARGET MovieCodecPlugin
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_BIN_DIR}
		COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:MovieCodecPlugin> ${OUTPUT_BIN_DIR}
	 )
endif(MSVC)

ENDIF(EXISTS "${ffmpeg_Root}")