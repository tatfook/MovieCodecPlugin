#pragma once

#ifdef WIN32
#	ifndef _CRT_SECURE_NO_WARNINGS
#		define _CRT_SECURE_NO_WARNINGS
#	endif
#endif


#include "PluginAPI.h"
#include "IParaEngineApp.h"


#ifndef OUTPUT_LOG
#define OUTPUT_LOG	ParaEngine::CParaEngineCore::GetParaEngineCOREInterface()->GetAppInterface()->WriteToLog
//#define OUTPUT_LOG	printf
#endif

#pragma warning( disable : 4819)