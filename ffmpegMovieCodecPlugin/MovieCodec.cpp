//-----------------------------------------------------------------------------
// Class:	
// Authors:	LiXizhi
// Emails:	LiXizhi@yeah.net
// Company: ParaEngine
// Date:	2015.2.19
//-----------------------------------------------------------------------------
#include "stdafx.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>

extern "C"
{
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
// #include <libavutil/timestamp.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include "AudioCapture.h"
#include "MovieCodec.h"

/** define this to duplicate lagged frames. */
// #define DUPLICATE_TAGGED_FRAMES 

/** define this for debugging purposes **/
// #define LOG_CAPTURE_TIME

/**  whether separate capturing and encoding thread into 2 threads. */
#define ENABLE_ASYNC_ENCODING   true

using namespace ParaEngine;


ParaEngine::MovieCodec::MovieCodec()
: m_bIsBegin(false), m_pCodecContext(NULL), m_pFile(NULL), m_nCurrentFrame(0), m_pAvFrame(0), m_pFormatContext(NULL), m_pOutputFormat(NULL),
m_audio_st(NULL), m_video_st(NULL), m_audio_codec(NULL), m_video_codec(NULL), m_pAudioCapture(NULL), m_bCaptureAudio(false), m_bCaptureMic(false), m_nStereoCaptureMode(MOVIE_CAPTURE_MODE_NORMAL),
m_nLastError(0), m_bStartEvent(false), m_bStopEvent(false), m_CompatibleHDC(NULL), m_BitmapHandle(NULL), m_audio_frame(NULL), m_swr_ctx(NULL), m_bCaptureMouse(false), m_nLastBitmapByteCount(0), m_nCaptureInterval(0), 
m_bEnableAsyncEncoding(ENABLE_ASYNC_ENCODING), m_nLastLostVideoFrame(0),
m_nMaxCacheFrames(30)
{
	CoInitialize(NULL);
	StaticInit();
	//m_nCodec = AV_CODEC_ID_FLV1; // 22
	//m_nCodec = AV_CODEC_ID_GIF; // 98
	// m_nCodec = AV_CODEC_ID_MPEG1VIDEO;

	// m_nCodec = AV_CODEC_ID_MPEG4; // 13
	m_nCodec = AV_CODEC_ID_H264; // 28

	m_nRawCodec = AV_CODEC_ID_MPEG4;
	m_nRecordingFPS = 20;
	m_nWidth = 320;
	m_nHeight = 240;
	m_nMarginTop = 0;

	m_nVideoBitRate = 800000;
	m_nAudioBitRate = 64000;
	m_nAudioSampleRate = 44100;

	m_pAudioCapture = new CAudioCapture();
	m_bCaptureAudio = true;

	OUTPUT_LOG("MovieCodec initialized\n");
}

ParaEngine::MovieCodec::~MovieCodec()
{
	SAFE_DELETE(m_pAudioCapture);
	EndCapture();
	CoUninitialize();
}

void ParaEngine::MovieCodec::StaticInit()
{
	/* Initialize libavcodec, and register all codecs and formats. */
	av_register_all();
	avcodec_register_all();
	// avfilter_register_all();
	// avformat_network_init();
}

void ParaEngine::MovieCodec::Release()
{
	delete this;
}

/*
* Video encoding example
*/
void ParaEngine::MovieCodec::video_encode_example(const char *filename, int codec_id)
{
	int width = 400;
	int height = 300;
	if (BeginCapture(filename, NULL, 0,0, width, height, codec_id)==0)
	{
		std::vector<byte> data;
		data.resize(m_pCodecContext->width* m_pCodecContext->height * 3);

		for (int i = 0; i < 25; i++)
		{
			/* prepare a dummy image */
			for (int y = 0; y < m_pCodecContext->height; y++) {
				for (int x = 0; x < m_pCodecContext->width; x++) {
					data[(y * m_pCodecContext->width + x) * 3] = x + y + m_nCurrentFrame * 3;
					data[(y * m_pCodecContext->width + x) * 3 + 1] = 128 + y + m_nCurrentFrame * 2;
					data[(y * m_pCodecContext->width + x) * 3 + 2] = 64 + x + m_nCurrentFrame * 5;
				}
			}
			encode_video_frame_data(&(data[0]));
		}
		EndCapture();
	}
}


void ParaEngine::MovieCodec::SetCodec(int nCodec)
{
	m_nCodec = nCodec;
}

int ParaEngine::MovieCodec::GetCodec()
{
	return m_nCodec;
}


DWORD ParaEngine::MovieCodec::BeginCaptureInThread()
{
	std::unique_lock<std::recursive_mutex> lock_(m_mutex_io_writer);

	/* allocate the output media context */
	avformat_alloc_output_context2(&m_pFormatContext, NULL, NULL, m_filename.c_str());

	if (!m_pFormatContext) {
		OUTPUT_LOG("Could not deduce output format from file extension: using MPEG.\n");
		avformat_alloc_output_context2(&m_pFormatContext, NULL, "mpeg", m_filename.c_str());
	}

	if (!m_pFormatContext)
	{
		OUTPUT_LOG("Could not allocate format context\n");
		return -1;
	}
	m_pOutputFormat = m_pFormatContext->oformat;

	/* Add the audio and video streams using the default format codecs
	* and initialize the codecs. */
	m_video_st = NULL;
	m_audio_st = NULL;
	m_nLastLostVideoFrame = -1;
	if (m_pOutputFormat->video_codec != AV_CODEC_ID_NONE)
		m_video_st = add_stream(m_pFormatContext, &m_video_codec, m_pOutputFormat->video_codec);
	
	if (IsCaptureAudio() && m_pOutputFormat->audio_codec != AV_CODEC_ID_NONE)
	{
		/* prefer AAC over mp3. mp3 will lead to failure to open the file. i do not know why*/
		if (m_pOutputFormat->audio_codec == AV_CODEC_ID_MP3)
			m_pOutputFormat->audio_codec = AV_CODEC_ID_AAC;

		if (m_pAudioCapture->BeginCaptureInThread() == 0)
		{
			m_nLastLeftDataCount = 0;
			m_audio_st = add_stream(m_pFormatContext, &m_audio_codec, m_pOutputFormat->audio_codec);
			if (m_audio_st)
			{
				if (open_audio(m_pFormatContext, m_audio_codec, m_audio_st) != 0)
				{
					return -1;
				}
			}
		}
	}
		

	/* Now that all the parameters are set, we can open the audio and
	* video codecs and allocate the necessary encode buffers. */
	if (!m_video_st)
		return -1;
	if (open_video(m_pFormatContext, m_video_codec, m_video_st) != 0)
		return -1;
	
	if (!(m_pOutputFormat->flags & AVFMT_NOFILE)) {
		int ret = avio_open(&m_pFormatContext->pb, m_filename.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			OUTPUT_LOG("Could not open '%s': %d\n", m_filename.c_str(), ret);
			return -1;
		}
	}

	/* Write the stream header, if any. */
	int ret = avformat_write_header(m_pFormatContext, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Error occurred when opening output file: %d\n", (ret));
		return -1;
	}

	// get the default device periodicity
	m_nCaptureInterval = 1000 / m_nRecordingFPS / 2;
	if (m_audio_st && m_pAudioCapture->GetTimerInterval() < m_nCaptureInterval)
	{
		m_nCaptureInterval = m_pAudioCapture->GetTimerInterval();
	}
	
	HDC SourceHDC = GetDC(m_hWndCaptured);
	//--Create a compatible device context from the interface context
	m_CompatibleHDC = CreateCompatibleDC(SourceHDC);

	::ReleaseDC(m_hWndCaptured, SourceHDC);

	// start capturing
	m_bStartEvent = true;

	m_nCurrentFrame = 0;
	return 0;
}

DWORD ParaEngine::MovieCodec::EndCaptureInThread()
{
	m_pAudioCapture->EndCaptureInThread();

	if (m_encoding_thread.joinable())
	{
		m_encoding_task_signal.notify_one();
		m_encoding_thread.join();
	}

	std::unique_lock<std::recursive_mutex> lock_(m_mutex_io_writer);


	/* Write the trailer, if any. The trailer must be written before you
	* close the CodecContexts open when you wrote the header; otherwise
	* av_write_trailer() may try to use memory that was freed on
	* av_codec_close(). */
	av_write_trailer(m_pFormatContext);

	/* Close each codec. */
	if (m_video_st)
		close_video(m_pFormatContext, m_video_st);

	if (m_audio_st)
		close_audio(m_pFormatContext, m_audio_st);

	if (!(m_pOutputFormat->flags & AVFMT_NOFILE)){
		/* Close the output file. */
		avio_close(m_pFormatContext->pb);
	}


	/* free the stream */
	avformat_free_context(m_pFormatContext);
	m_pFormatContext = NULL;

	if (m_CompatibleHDC)
	{
		DeleteDC(m_CompatibleHDC);
		m_CompatibleHDC = NULL;
		m_BitmapHandle = NULL;
	}

	return 0;
}

void ParaEngine::MovieCodec::DuplicateLastFrame()
{
	if (m_nLastBitmapByteCount > 0)
	{
		BYTE* pBmpBufferFlip = &(g_buffer[1][0]);
		if (m_bEnableAsyncEncoding)
			encode_video_frame_data_async(pBmpBufferFlip, m_nLastBitmapByteCount);
		else
			encode_video_frame_data(pBmpBufferFlip, m_nLastBitmapByteCount);
	}
	else
	{
		OUTPUT_LOG("warning: can not duplicate frames becaues m_nLastBitmapByteCount is 0 \n");
	}
}

DWORD ParaEngine::MovieCodec::CaptureVideoFrame()
{
	// capture using GDI device.
#ifdef LOG_CAPTURE_TIME
	DWORD nStartTime = timeGetTime();
#endif

	int PixelID = 0;
	BITMAP Bitmap;

	int nMarginLeft = m_nLeft;
	int nMarginTop = m_nTop;
	int nWidth = GetWidth();
	int nHeight = GetHeight();

	//--Get the interface device context
	HDC SourceHDC = GetDC(m_hWndCaptured);

	//--Create a compatible bitmap
	if (m_BitmapHandle == 0)
	{
		m_BitmapHandle = CreateCompatibleBitmap(SourceHDC, nWidth, nHeight);

		//--Validate the data
		if (m_BitmapHandle == 0 || !SelectObject(m_CompatibleHDC, m_BitmapHandle))
		{
			OUTPUT_LOG("error: can not select bitmap in MovieCodec\n");
			return -1;
		}
	}


	// copy the bitmap to the memory device context
	// SRCCOPY | CAPTUREBLT: Includes any windows that are layered on top of your window in the resulting image. By default, the image only contains your window.
	if (!BitBlt(m_CompatibleHDC, 0, 0, nWidth, nHeight, SourceHDC, nMarginLeft, nMarginTop, SRCCOPY))
	{
		OUTPUT_LOG("error: bitblt failed with code %d in MovieCodec\n", GetLastError());
		return -1;
	}

	::ReleaseDC(m_hWndCaptured, SourceHDC);

	// copy the mouse cursor to the bitmap
	if (m_bCaptureMouse) {
		HCURSOR hc = ::GetCursor();
		CURSORINFO cursorinfo;
		ICONINFO iconinfo;
		cursorinfo.cbSize = sizeof(CURSORINFO);
		::GetCursorInfo(&cursorinfo);
		::GetIconInfo(cursorinfo.hCursor, &iconinfo);
		::ScreenToClient(m_hWndCaptured, &cursorinfo.ptScreenPos);
		::DrawIcon(m_CompatibleHDC, cursorinfo.ptScreenPos.x - iconinfo.xHotspot, cursorinfo.ptScreenPos.y - iconinfo.yHotspot, cursorinfo.hCursor);
	}

	//--Get the bitmap
	GetObject(m_BitmapHandle, sizeof(BITMAP), &Bitmap);

	//--Compute the bitmap size
	unsigned long BitmapSize = sizeof(BITMAPINFOHEADER) + (Bitmap.bmWidth  * Bitmap.bmHeight * 3);

	//--Allocate a memory block for the bitmap
	//allocate the output buffer;
	if (g_buffer[0].size() != BitmapSize)
	{
		g_buffer[0].resize(BitmapSize);
		memset(&(g_buffer[0][0]), 0, BitmapSize);
	}
	if (g_buffer[1].size() != BitmapSize)
	{
		g_buffer[1].resize(BitmapSize);
		memset(&(g_buffer[1][0]), 0, BitmapSize);
	}
	BYTE* MemoryHandle = &(g_buffer[0][0]);

	//--Setup the bitmap data
	LPBITMAPINFOHEADER pBmpInfo = (LPBITMAPINFOHEADER)(MemoryHandle);
	pBmpInfo->biSizeImage = 0;//BitmapSize - sizeof(BITMAPINFOHEADER);
	pBmpInfo->biSize = sizeof(BITMAPINFOHEADER);
	pBmpInfo->biHeight = Bitmap.bmHeight;
	pBmpInfo->biWidth = Bitmap.bmWidth;
	pBmpInfo->biCompression = BI_RGB;
	pBmpInfo->biBitCount = 24;
	pBmpInfo->biPlanes = 1;
	pBmpInfo->biXPelsPerMeter = 0;
	pBmpInfo->biYPelsPerMeter = 0;
	pBmpInfo->biClrUsed = 0;
	pBmpInfo->biClrImportant = 0;

	//--Get the bitmap data from memory
	GetDIBits(m_CompatibleHDC, m_BitmapHandle, 0, Bitmap.bmHeight, (unsigned char*)(pBmpInfo + 1), (LPBITMAPINFO)pBmpInfo, DIB_RGB_COLORS);

	//--Get the data
	LPBITMAPINFOHEADER Data = (LPBITMAPINFOHEADER)MemoryHandle;

	//--Allocate memory for the blend data
	unsigned char* pBmpBuffer = (unsigned char*)Data + Data->biSize + Data->biClrUsed * sizeof(RGBQUAD);
	size_t bmpsize = nWidth*nHeight * 3;

	BYTE* pBmpBufferFlip = &(g_buffer[1][0]);

	int nLineByteCount = Bitmap.bmWidth * 3;

	for (int Y = 0; Y < nHeight; Y++)
	{
		memcpy(pBmpBufferFlip + nLineByteCount*Y, pBmpBuffer + nLineByteCount*(nHeight - Y - 1), nLineByteCount);
	}
	m_nLastBitmapByteCount = bmpsize;


	DWORD res = 0;
	if (m_bEnableAsyncEncoding)
		res = encode_video_frame_data_async(pBmpBufferFlip, bmpsize);
	else{
#ifdef LOG_CAPTURE_TIME
		DWORD nGetScreenDataTime = timeGetTime();
#endif
		res = encode_video_frame_data(pBmpBufferFlip, bmpsize);
#ifdef LOG_CAPTURE_TIME
		// frame : 68 captured: from Time: 85315884, fetch data: 10, encode: 116
		OUTPUT_LOG("frame : %d captured: from Time: %d, fetch data: %d, encode: %d\n", m_nCurrentFrame, nStartTime, nGetScreenDataTime - nStartTime, timeGetTime() - nGetScreenDataTime);
#endif
	}

	return res;
}

DWORD ParaEngine::MovieCodec::CaptureThreadFunction()
{
	DWORD res = BeginCaptureInThread();
	if (res != 0)
	{
		SetErrorCode(res);
		EndCaptureInThread();
		return res;
	}

	// capture loop
	bool bDone = false;
	bool bFirstPacket = true;
	
	DWORD nBeginTime = timeGetTime();
	
	for (UINT32 nPasses = 0; !bDone; nPasses++) 
	{
		bool bFlushStream = (m_nCurrentFrame % 200) == 0;
		
		if (m_bStopEvent) {
			OUTPUT_LOG("Received stop event after %d frames\n", m_nCurrentFrame);
			bDone = true;
			continue; // exits loop
		}

		if (m_nLastError != 0) {
			OUTPUT_LOG("Error: Unexpected error code: %d \n", m_nLastError);
			bDone = true;
			continue; // exits loop
		}

		DWORD nCurTime = timeGetTime();

		float nFramesToCapture = (float)(((nCurTime - nBeginTime) / 1000.f * m_nRecordingFPS) - m_nCurrentFrame);
		if (nFramesToCapture > 0.f)
		{
			if (CaptureVideoFrame() != 0)
			{
				OUTPUT_LOG("Error: FrameCaptureInThread return non-zero value \n");
				bDone = true;
				continue; // exits loop
			}
			else
			{
				if (nFramesToCapture > 1.f)
				{
					// just in case the capture thread does not work fast enough. we may at most duplicate 5 frames. 
#ifdef DUPLICATE_TAGGED_FRAMES
					for (int i = 1, j = 1; i < nFramesToCapture && j <= 5; ++i, ++j)
					{
						OUTPUT_LOG("Capture video frame lagging detected: DuplicateLastFrame %d count:%d/%d\n", m_nCurrentFrame, i, (int)nFramesToCapture);
						DuplicateLastFrame();
					}
#else
					OUTPUT_LOG("Capture video frame lagging detected: DuplicateLastFrame %d count:%f\n", m_nCurrentFrame, nFramesToCapture);
					m_nCurrentFrame += (int)(nFramesToCapture - 1 + 0.5f);
#endif
				}
			}
			
		}
		
		if (IsCaptureAudio())
		{
			CaptureAudioFrame();
		}
		if (bFlushStream)
		{
			// write_frame(m_pFormatContext, NULL, NULL, NULL);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(m_nCaptureInterval));
	}

	return EndCaptureInThread();
}

void ParaEngine::MovieCodec::ResetStates()
{
	m_nLastError = 0;
	m_bStartEvent = false;
	m_bStopEvent = false;
	m_video_encode_queue.clear();
}

int ParaEngine::MovieCodec::BeginCapture(const char *filename, HWND nHwnd, int nLeft, int nTop, int width /*= 0*/, int height /*= 0*/, int m_nFPS /*= 0*/, int codec_id /*= 0*/)
{
	if (m_bIsBegin)
		EndCapture();

	// reset states
	ResetStates();

	m_hWndCaptured = nHwnd;
	m_nLeft = nLeft;
	m_nTop = nTop;

	if (m_nFPS > 0)
		m_nRecordingFPS = m_nFPS;
	if (width > 0)
	{
		m_nWidth = ((int)(width / 16)) * 16;
	}
	if (height > 0)
	{
		m_nHeight = ((int)(height / 16)) * 16;
	}

	m_filename = filename;

	m_capture_thread = std::thread(std::bind(&MovieCodec::CaptureThreadFunction, this));

	while (!m_bStartEvent && m_nLastError==0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	if (m_nLastError != 0) {
		OUTPUT_LOG("Unexpected return value %d\n", m_nLastError);
		return -__LINE__;
	}
	else
	{
		m_encoding_thread = std::thread(std::bind(&MovieCodec::EncodingThreadFunction, this));
	}
	
	OUTPUT_LOG("Encode video file %s\n", filename);
	m_bIsBegin = true;
	return 0;
}


DWORD ParaEngine::MovieCodec::EncodingThreadFunction()
{
	int nLastFrameNumber = 0;
	std::unique_lock<std::mutex> lock_(m_mutex);
	while (!m_bStopEvent && m_nLastError == 0)
	{
		m_encoding_task_signal.wait(lock_);
		while (m_video_encode_queue.size() > 0)
		{
			CaptureFrameDataPtr captureData = m_video_encode_queue.front();
			m_video_encode_queue.pop_front();
			lock_.unlock();
#ifdef LOG_CAPTURE_TIME
			DWORD nGetScreenDataTime = timeGetTime();
#endif
			int nFrameNumber = captureData->m_nFrameNumber;
			if((nFrameNumber - nLastFrameNumber) > 1){
				OUTPUT_LOG("warning:inconsistent frame number: last: %d, cur: %d\n", nLastFrameNumber, nFrameNumber);
			}
			nLastFrameNumber = nFrameNumber;
			int res = encode_video_frame_data((const BYTE*)(captureData->GetData()), captureData->GetSize(), &(nFrameNumber));
#ifdef LOG_CAPTURE_TIME
			// frame : 68 captured: from Time: 85315884, fetch data: 10, encode: 116
			OUTPUT_LOG("frame : %d captured. encoding time: %d\n", captureData->m_nFrameNumber, timeGetTime() - nGetScreenDataTime);
#endif
			if (res != 0){
				SetErrorCode(res);
				break;
			}
			lock_.lock();
		}
		while (m_audio_encode_queue.size() > 0)
		{
			CaptureFrameDataPtr captureData = m_audio_encode_queue.front();
			m_audio_encode_queue.pop_front();
			lock_.unlock();
			int nFrameNumber = captureData->m_nFrameNumber;
			encode_audio_frame_data((const BYTE*)(captureData->GetData()), captureData->GetSize(), &(nFrameNumber));
			lock_.lock();
		}
	}
	return 0;
}

int ParaEngine::MovieCodec::encode_video_frame_data_async(const BYTE* pData, int nDataSize, int* pnFrameCount)
{
	{
		std::unique_lock<std::mutex> lock_(m_mutex);
		int nFrameCount = (pnFrameCount) ? *pnFrameCount : m_nCurrentFrame;
		m_video_encode_queue.push_back(CaptureFrameDataPtr(new CaptureFrameData((const char*)pData, nDataSize, nFrameCount)));
		if ((int)m_video_encode_queue.size() > m_nMaxCacheFrames){
			int nFrontFrame = m_video_encode_queue.front()->m_nFrameNumber;
			if (m_nLastLostVideoFrame < 0 || (m_nLastLostVideoFrame + 5) < nFrontFrame){
				// we will drop the front frame if we are not losing frames consecutively. 
				// this ensure that we will smoothly drop FPS, instead of letting the graphics jerks. 
				m_nLastLostVideoFrame = nFrontFrame;
				m_video_encode_queue.pop_front();
			}
			else
			{
				// let us find a smooth frame to drop.  dropping to half of original FPS. 
				bool bFound = false;
				for (auto iter = m_video_encode_queue.begin(); iter != m_video_encode_queue.end(); iter++)
				{
					if ((*iter)->m_nFrameNumber == (m_nLastLostVideoFrame + 2))
					{
						m_nLastLostVideoFrame = (*iter)->m_nFrameNumber;
						m_video_encode_queue.erase(iter);
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					m_nLastLostVideoFrame = nFrontFrame;
					m_video_encode_queue.pop_front();
				}
			}
			OUTPUT_LOG("warning: we are losing video frames: %d\n", m_nLastLostVideoFrame);
		}
		if (pnFrameCount)
			(*pnFrameCount)++;
		else
			m_nCurrentFrame++;
	}
	m_encoding_task_signal.notify_one();
	return 0;
}

int ParaEngine::MovieCodec::encode_video_frame_data(const BYTE* pData, int nDataSize, int* pnFrameCount)
{
	if (!m_video_st || !(m_video_st->codec))
		return -1;
	AVCodecContext *c = m_video_st->codec;

	if (nDataSize != 0 && nDataSize != c->width*c->height * 3)
		return -1;
	
	/* now convert, scale and save each pixel */
	static SwsContext* sws_ctx = NULL;

	sws_ctx = sws_getCachedContext(sws_ctx, c->width, c->height,
		AV_PIX_FMT_BGR24, c->width, c->height,
		c->pix_fmt, SWS_FAST_BILINEAR, 0, 0, 0);
	if (sws_ctx)
	{
		const uint8_t * inData[1] = { (const uint8_t *)pData }; // RGB24 have one plane
		int inLinesize[1] = { 3 * c->width }; // RGB stride
		sws_scale(sws_ctx, inData, inLinesize, 0, c->height, m_pAvFrame->data, m_pAvFrame->linesize);
	}

	int nFrameCount = (pnFrameCount) ? *pnFrameCount : m_nCurrentFrame;
	m_pAvFrame->pts = nFrameCount;
	// increase one frame: what if stereo?
	if (pnFrameCount)
		(*pnFrameCount)++;
	else
		m_nCurrentFrame++;

	/* encode the image */
	
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;    // packet data will be allocated by the encoder
	pkt.size = 0;
	int ret = -1;
	int flush = 0;
	if (m_pFormatContext->oformat->flags & AVFMT_RAWPICTURE && !flush) {
		/* Raw video case - directly store the picture in the packet */
		
		pkt.flags |= AV_PKT_FLAG_KEY;
		pkt.stream_index = m_video_st->index;
		pkt.data = m_pAvFrame->data[0];
		pkt.size = sizeof(AVPicture);
		std::unique_lock<std::recursive_mutex> lock_(m_mutex_io_writer);
		ret = av_write_frame(m_pFormatContext, &pkt);
	}
	else {
		int got_packet;
		
		/* encode the image */
		ret = avcodec_encode_video2(c, &pkt, m_pAvFrame, &got_packet);
		if (ret < 0) {
			OUTPUT_LOG("Error encoding frame\n");
			EndCapture();
			return -1;
		}
		/* If size is zero, it means the image was buffered. */
		if (got_packet) {
			ret = write_frame(m_pFormatContext, &c->time_base, m_video_st, &pkt);
			// av_free_packet(&pkt); // pkts is reference counted no need to free
		}
		else {
			ret = 0;
		}
	}
	
	return 0;
}

int ParaEngine::MovieCodec::EndCapture()
{
	if (!m_bIsBegin)
		return 0;

	if (m_capture_thread.joinable())
	{
		m_bStopEvent = true;
		m_capture_thread.join();
		
		if (m_encoding_thread.joinable())
		{
			// this should never happen, just in case. 
			m_encoding_task_signal.notify_one();
			m_encoding_thread.join();
		}
	}

	m_bIsBegin = false;
	return 0;
}

bool ParaEngine::MovieCodec::IsRecording()
{
	return m_bIsBegin;
}

const char* ParaEngine::MovieCodec::GetFileName()
{
	return m_filename.c_str();
}

int ParaEngine::MovieCodec::GetCurrentFrameNumber()
{
	return m_nCurrentFrame;
}

int ParaEngine::MovieCodec::GetWidth()
{
	return m_nWidth;
}

int ParaEngine::MovieCodec::GetHeight()
{
	return m_nHeight;
}

AVStream * ParaEngine::MovieCodec::add_stream(AVFormatContext *oc, AVCodec **codec, int codec_id_)
{
	AVCodecID codec_id = (AVCodecID)codec_id_;
	AVCodecContext *c;
	AVStream *st;

	/* find the encoder */
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		OUTPUT_LOG("Could not find encoder for '%s'\n",
			avcodec_get_name(codec_id));
		return NULL;
	}

	st = avformat_new_stream(oc, *codec);
	if (!st) {
		OUTPUT_LOG("Could not allocate stream\n");
		return NULL;
	}
	st->id = oc->nb_streams - 1;
	c = st->codec;

	switch ((*codec)->type) {
	case AVMEDIA_TYPE_AUDIO:
		c->sample_fmt = (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		c->bit_rate = m_nAudioBitRate;
		c->sample_rate =  m_pAudioCapture->GetSampleRate();
		c->channels = m_pAudioCapture->GetChannels();
		break;

	case AVMEDIA_TYPE_VIDEO:
		c->codec_id = codec_id;

		c->bit_rate = GetVideoBitRate();
		/* Resolution must be a multiple of two. */
		c->width = GetWidth();
		c->height = GetHeight();
		/* timebase: This is the fundamental unit of time (in seconds) in terms
		* of which frame timestamps are represented. For fixed-fps content,
		* timebase should be 1/framerate and timestamp increments should be
		* identical to 1. */
		c->time_base.den = m_nRecordingFPS;
		c->time_base.num = 1;
		c->gop_size = 12; /* emit one intra frame every twelve frames at most */
		if (c->codec_id == AV_CODEC_ID_GIF)
			c->pix_fmt = AV_PIX_FMT_RGB8;
		else
			c->pix_fmt = AV_PIX_FMT_YUV420P;
		if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
			/* just for testing, we also add B frames */
			c->max_b_frames = 2;
		}
		if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
			/* Needed to avoid using macroblocks in which some coeffs overflow.
			* This does not happen with normal video, it just happens here as
			* the motion of the chroma plane does not match the luma plane. */
			c->mb_decision = 2;
		}
		break;

	default:
		break;
	}

	/* Some formats want stream headers to be separate. */
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}

int ParaEngine::MovieCodec::open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
	AVCodecContext *c = st->codec;

	/* open the codec */
	int ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Could not open video codec: %d\n", ret);
		return -1;
	}

	/* allocate and init a re-usable frame */
	m_pAvFrame = av_frame_alloc();
	if (!m_pAvFrame) {
		fprintf(stderr, "Could not allocate video frame\n");
		return -1;
	}
	m_pAvFrame->format = c->pix_fmt;
	m_pAvFrame->width = c->width;
	m_pAvFrame->height = c->height;

	/* Allocate the encoded raw picture. */
	/* the image can be allocated by any means and av_image_alloc() is
	* just the most convenient way if av_malloc() is to be used */
	ret = av_image_alloc(m_pAvFrame->data, m_pAvFrame->linesize, c->width, c->height,
		c->pix_fmt, 32);
	if (ret < 0) {
		OUTPUT_LOG("Could not allocate raw picture buffer\n");
		return -1;
	}
	return 0;
}

void ParaEngine::MovieCodec::close_video(AVFormatContext *oc, AVStream *st)
{
	avcodec_close(st->codec);
	if (m_pAvFrame)
	{
		av_freep(&m_pAvFrame->data[0]);
		av_frame_free(&m_pAvFrame);
		m_pAvFrame = NULL;
	}
}

int ParaEngine::MovieCodec::write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
	/* rescale output packet timestamp values from codec to stream timebase */
	if (pkt)
	{
		pkt->pts = av_rescale_q_rnd(pkt->pts, *time_base, st->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt->dts = av_rescale_q_rnd(pkt->dts, *time_base, st->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt->duration = (int)av_rescale_q(pkt->duration, *time_base, st->time_base);
		pkt->stream_index = st->index;
	}

	/* Write the compressed frame to the media file. */
	std::unique_lock<std::recursive_mutex> lock_(m_mutex_io_writer);
	// since all frames are in order, we use av_write_frame() instead of av_interleaved_write_frame();
	// return av_interleaved_write_frame(fmt_ctx, pkt);
	return av_write_frame(fmt_ctx, pkt);
}

void ParaEngine::MovieCodec::SetVideoBitRate(int nRate)
{
	m_nVideoBitRate = nRate;
}

int ParaEngine::MovieCodec::GetVideoBitRate()
{
	return m_nVideoBitRate;
}

void ParaEngine::MovieCodec::SetCaptureAudio(bool bEnable)
{
	m_bCaptureAudio = bEnable;
}

bool ParaEngine::MovieCodec::IsCaptureAudio()
{
	return m_bCaptureAudio;
}

void ParaEngine::MovieCodec::SetCaptureMic(bool bEnable)
{
	m_bCaptureMic = bEnable;
}

bool ParaEngine::MovieCodec::IsCaptureMic()
{
	return m_bCaptureMic;
}

int ParaEngine::MovieCodec::open_audio(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
	AVCodecContext *c;
	int ret;

	c = st->codec;

	/* allocate and init a re-usable frame */
	m_audio_frame = av_frame_alloc();
	if (!m_audio_frame) {
		OUTPUT_LOG("Could not allocate audio frame\n");
		exit(1);
	}

	/* open it */
	ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Could not open audio codec: %d\n", ret);
		return -1;
	}

	m_samples_count = 0;
	
	m_src_nb_samples = c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE ?
		10000 : c->frame_size;

	AVSampleFormat avInputFormat = (m_pAudioCapture->GetBitsPerSample() == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT);
	ret = av_samples_alloc_array_and_samples(&m_src_samples_data, &m_src_samples_linesize, c->channels,	m_src_nb_samples, avInputFormat, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not allocate source samples\n");
		return -1;
	}

	/* compute the number of converted samples: buffering is avoided
	* ensuring that the output buffer will contain at least all the
	* converted input samples */
	m_max_dst_nb_samples = m_src_nb_samples;

	/* create resampler context */
	if (c->sample_fmt != avInputFormat) {
		m_swr_ctx = swr_alloc();
		if (!m_swr_ctx) {
			OUTPUT_LOG("Could not allocate resampler context\n");
			return -1;
		}

		/* set options */
		av_opt_set_int(m_swr_ctx, "in_channel_count", c->channels, 0);
		av_opt_set_int(m_swr_ctx, "in_sample_rate", c->sample_rate, 0);
		av_opt_set_sample_fmt(m_swr_ctx, "in_sample_fmt", avInputFormat, 0);
		av_opt_set_int(m_swr_ctx, "out_channel_count", c->channels, 0);
		av_opt_set_int(m_swr_ctx, "out_sample_rate", c->sample_rate, 0);
		av_opt_set_sample_fmt(m_swr_ctx, "out_sample_fmt", c->sample_fmt, 0);

		/* initialize the resampling context */
		if ((ret = swr_init(m_swr_ctx)) < 0) {
			OUTPUT_LOG("Failed to initialize the resampling context\n");
			return -1;
		}

		ret = av_samples_alloc_array_and_samples(&m_dst_samples_data, &m_dst_samples_linesize, c->channels,
			m_max_dst_nb_samples, c->sample_fmt, 0);
		if (ret < 0) {
			OUTPUT_LOG("Could not allocate destination samples\n");
			return -1;
		}
	}
	else {
		m_dst_samples_data = m_src_samples_data;
	}
	m_dst_samples_size = av_samples_get_buffer_size(NULL, c->channels, m_max_dst_nb_samples,
		c->sample_fmt, 0);
	return 0;
}

void ParaEngine::MovieCodec::close_audio(AVFormatContext *oc, AVStream *st)
{
	avcodec_close(st->codec);

	if (m_src_samples_data)
	{
		if (m_dst_samples_data != m_src_samples_data) {
			av_free(m_dst_samples_data[0]);
			av_free(m_dst_samples_data);
		}
		av_free(m_src_samples_data[0]);
		av_free(m_src_samples_data);

		m_src_samples_data = NULL;
		m_dst_samples_data = NULL;
	}

	av_frame_free(&m_audio_frame);
	m_audio_frame = NULL;
}


bool ParaEngine::MovieCodec::CaptureAudioFrame()
{
	if (!m_audio_st)
		return false;

	for (int i = 0; i < 10; ++i)
	{
		const BYTE* pData = NULL;
		int nByteCount = 0;
		HRESULT hr = m_pAudioCapture->FrameCaptureInThread(&pData, &nByteCount);
		if (hr != 0 || nByteCount == 0)
		{
			break;
		}
		if (m_bEnableAsyncEncoding)
		{
			{
				std::unique_lock<std::mutex> lock_(m_mutex);
				m_audio_encode_queue.push_back(CaptureFrameDataPtr(new CaptureFrameData((const char*)pData, nByteCount, m_nCurrentFrame)));
				if ((int)m_audio_encode_queue.size() > m_nMaxCacheFrames * 100){
					auto data = m_audio_encode_queue.front();
					OUTPUT_LOG("warning: we are losing audio frames: %d queue size: %d\n", data->m_nFrameNumber, m_audio_encode_queue.size());
					m_audio_encode_queue.pop_front();
				}
			}
			m_encoding_task_signal.notify_one();
		}
		else
		{
			encode_audio_frame_data(pData, nByteCount);
		}
	}
	return true;
}


DWORD ParaEngine::MovieCodec::encode_audio_frame_data(const BYTE* pData, int nByteCount, int* pnFrameCount)
{
	if (!m_audio_st)
		return 0;

	AVCodecContext *c = m_audio_st->codec;

	if (pData && nByteCount>0)
	{
		int nFrameSize = (m_src_nb_samples * c->channels * m_pAudioCapture->GetBitsPerSample() / 8);
		int nFramesToWrite = (nByteCount + m_nLastLeftDataCount) / nFrameSize;
		int nRemainingBytes = (nByteCount + m_nLastLeftDataCount) - (nFramesToWrite * nFrameSize);

		const byte * pSrc = (const byte *)pData;

		for (int nFrame = 0; nFrame < nFramesToWrite; nFrame++)
		{
			AVPacket pkt = { 0 }; // data and size must be 0;
			int got_packet, ret, dst_nb_samples;

			av_init_packet(&pkt);

			// copy data for the given frame. 
			byte * pDest = (byte *)m_src_samples_data[0];
			{
				memcpy(pDest + m_nLastLeftDataCount, pSrc, nFrameSize - m_nLastLeftDataCount);
				pDest += (nFrameSize - m_nLastLeftDataCount);
				pSrc += (nFrameSize - m_nLastLeftDataCount);
				m_nLastLeftDataCount = 0;
			}

			/* convert samples from native format to destination codec format, using the resampler */
			if (m_swr_ctx) {
				/* compute destination number of samples */
				dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(m_swr_ctx, c->sample_rate) + m_src_nb_samples,
					c->sample_rate, c->sample_rate, AV_ROUND_UP);
				if (dst_nb_samples > m_max_dst_nb_samples) {
					av_free(m_dst_samples_data[0]);
					ret = av_samples_alloc(m_dst_samples_data, &m_dst_samples_linesize, c->channels,
						dst_nb_samples, c->sample_fmt, 0);
					if (ret < 0)
						return -1;
					m_max_dst_nb_samples = dst_nb_samples;
					m_dst_samples_size = av_samples_get_buffer_size(NULL, c->channels, dst_nb_samples,
						c->sample_fmt, 0);
				}

				/* convert to destination format */
				ret = swr_convert(m_swr_ctx,
					m_dst_samples_data, dst_nb_samples,
					(const uint8_t **)m_src_samples_data, m_src_nb_samples);
				if (ret < 0) {
					OUTPUT_LOG("Error while converting\n");
					return -1;
				}
			}
			else {
				dst_nb_samples = m_src_nb_samples;
			}

#ifdef DEBUG_PULSE_CODE
			if (c->sample_fmt == AV_SAMPLE_FMT_S16)
			{
				int16_t * pDest1 = (int16_t *)m_dst_samples_data[0];
				float *   pSrc1 = (float *)m_src_samples_data[0];
				int channels = c->channels;
				int16_t nSample;
				float fSample;
				for (int i = 0; i < m_src_nb_samples; i++)
				{
					for (int nc = 0; nc < channels; nc++)
					{
						nSample = (pDest1[i * channels + nc]);
						fSample = (pSrc1[i * channels + nc]);
						if (fSample >= 1.f || fSample <= -1.f)
						{
							nSample = 0;
						}
					}
				}
			}
#endif

			m_audio_frame->nb_samples = dst_nb_samples;
			AVRational avr = { 1, c->sample_rate };

			m_audio_frame->pts = av_rescale_q(m_samples_count, avr, c->time_base);
			avcodec_fill_audio_frame(m_audio_frame, c->channels, c->sample_fmt,
				m_dst_samples_data[0], m_dst_samples_size, 0);
			m_samples_count += dst_nb_samples;


			ret = avcodec_encode_audio2(c, &pkt, m_audio_frame, &got_packet);
			if (ret < 0) {
				OUTPUT_LOG("Error encoding audio frame: %d\n", ret);
				return -1;
			}

			if (!got_packet) {
				continue;
			}

			ret = write_frame(m_pFormatContext, &c->time_base, m_audio_st, &pkt);

			if (ret < 0) {
				OUTPUT_LOG("Error while writing audio frame: %d\n", ret);
				return -1;
			}
		}

		if (nRemainingBytes > 0)
		{
			byte * pDest = (byte *)(m_src_samples_data[0] + m_nLastLeftDataCount);
			memcpy(pDest, pSrc, nRemainingBytes - m_nLastLeftDataCount);
			pDest += nRemainingBytes - m_nLastLeftDataCount;
			pSrc += nRemainingBytes - m_nLastLeftDataCount;
			m_nLastLeftDataCount = nRemainingBytes;
		}
	}
	return 0;
}

ParaEngine::MOVIE_CAPTURE_MODE ParaEngine::MovieCodec::GetStereoCaptureMode()
{
	return m_nStereoCaptureMode;
}

void ParaEngine::MovieCodec::SetStereoCaptureMode(MOVIE_CAPTURE_MODE nMode /*= MOVIE_CAPTURE_MODE_NORMAL*/)
{
	m_nStereoCaptureMode = nMode;
}




