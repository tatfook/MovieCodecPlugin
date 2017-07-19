# Author: LiXizhi
# Company: ParaEngine.com
# Date: 2014.5.1
# Desc: movie encoder using precompiled version of ffmpeg. See http://ffmpeg.zeranoe.com/builds/
# add environment variable NPLRUNTIME_ROOT that point to git root of NPLRuntime.
# NOTE: modify the root directory path if it is installed at a different location. 

# Note: include the ParaEngine/NPLRuntime include directory
if ("$ENV{NPLRUNTIME_ROOT}" STREQUAL "")
	message("NPLRUNTIME_ROOT environment variable not set")
else()
	set(ParaEngineClient_SOURCE_DIR $ENV{NPLRUNTIME_ROOT}/Client/trunk/ParaEngineClient)
	add_subdirectory (ffmpegMovieCodecPlugin)
endif()
