//-----------------------------------------------------------------------------
// Class:	
// Authors:	LiXizhi
// Emails:	LiXizhi@yeah.net
// Company: ParaEngine
// Date:	2015.2.19
//-----------------------------------------------------------------------------
#include "stdafx.h"
#include "CaptureFrameData.h"
using namespace ParaEngine;

ParaEngine::CaptureFrameData::CaptureFrameData(const char* pData, int nDataSize, int nFrameNumber)
{
	SetData(pData, nDataSize, nFrameNumber);
}

ParaEngine::CaptureFrameData::CaptureFrameData()
	:m_nFrameNumber(0)
{

}

ParaEngine::CaptureFrameData::~CaptureFrameData()
{

}

void ParaEngine::CaptureFrameData::SetData(const char* pData, int nDataSize, int nFrameNumber)
{
	if (pData)
	{
		m_data.resize(nDataSize);
		memcpy(&(m_data[0]), pData, nDataSize);
	}
	m_nFrameNumber = nFrameNumber;
}
