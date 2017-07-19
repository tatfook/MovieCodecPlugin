#pragma once
#include <memory>

namespace ParaEngine
{
	/** a cache of video data of a single frame. */
	class CaptureFrameData
	{
	public:
		CaptureFrameData(const char* pData, int nDataSize, int nFrameNumber);
		CaptureFrameData();
		~CaptureFrameData();

		inline const char* GetData(){
			return &(m_data[0]);
		};
		inline int GetSize(){
			return (int)(m_data.size());
		};

		void SetData(const char* pData, int nDataSize, int nFrameNumber);
	public:
		std::vector<char> m_data;
		int m_nFrameNumber;
	};

	typedef std::shared_ptr<CaptureFrameData> CaptureFrameDataPtr;
}