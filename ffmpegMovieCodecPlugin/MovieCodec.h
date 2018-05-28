#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include "IMovieCodec.h"
#include "CaptureFrameData.h"

struct AVCodecContext;
struct AVFrame;
struct AVFormatContext;
struct AVStream;
struct AVCodec;
struct AVPacket;
struct AVRational;
struct AVOutputFormat;
struct SwrContext;

class AudioMixer;

namespace ParaEngine
{
	class CAudioCapture;

	class MovieCodec :public IMovieCodec
	{
	public:
		MovieCodec();
		~MovieCodec();
		static void StaticInit();
	public:
		virtual void Release();

		/**encode a random dummy video to the given filename with given codec. */
		virtual void video_encode_example(const char *filename, int codec_id);

		/** set preferred codec: one of the AVCodecID */
		virtual void SetCodec(int nCodec);

		/** get preferred codec: one of the AVCodecID */
		virtual int GetCodec();

		/** begin recording to a given file. */
		virtual int BeginCapture(const char *filename, HWND nHwnd, int nLeft=0, int nTop=0, int width = 0, int height = 0, int nFPS = 0, int codec_id = 0);

		void ResetStates();

		/** capture the current frame.
		* @param pData: raw RGB array. 3 bytes per pixels
		* @param nDataSize: data size in bytes. it must be width*height*3
		*/
		virtual int encode_video_frame_data(const BYTE* pData, int nDataSize = 0, int* pnFrameCount = 0);
		virtual int FrameCapture(const BYTE* pData, int nDataSize = 0, int* pnFrameCount = 0){
			return encode_video_frame_data(pData, nDataSize, pnFrameCount);
		};
		virtual int encode_video_frame_data_async(const BYTE* pData, int nDataSize = 0, int* pnFrameCount = 0);

		/** end recording for the current file. */
		virtual int EndCapture(std::string auidoMap = "");

		/** is recording */
		virtual bool IsRecording();

		/** get current filename */
		virtual const char* GetFileName();

		/** get current frame number */
		virtual int GetCurrentFrameNumber();

		virtual int GetWidth();

		virtual int GetHeight();

		virtual void SetVideoBitRate(int nRate);

		virtual int GetVideoBitRate();

		virtual void SetCaptureAudio(bool bEnable);
		virtual bool IsCaptureAudio();

		virtual void SetCaptureMic(bool bEnable);
		virtual bool IsCaptureMic();

		/**
		* set the stereo capture mode. This is used to generate video files that can be viewed by 3d eye glasses and stereo video player.
		*  - 0 for disable stereo capture(default);
		*  - 1 for line interlaced stereo.
		*  - 2 for left right stereo;
		*  - 3 for above below stereo;
		*  - 4 for frame interlaved mode, where the odd frame is the left eye and even frame is the right image;
		*/
		virtual void SetStereoCaptureMode(MOVIE_CAPTURE_MODE nMode = MOVIE_CAPTURE_MODE_NORMAL);

		/**
		* Get the stereo capture mode. This is used to generate video files that can be viewed by 3d eye glasses and stereo video player.
		*  - 0 for disable stereo capture(default);
		*  - 1 for line interlaced stereo.
		*  - 2 for left right stereo;
		*  - 3 for above below stereo;
		*  - 4 for frame interlaved mode, where the odd frame is the left eye and even frame is the right image;
		*/
		virtual MOVIE_CAPTURE_MODE GetStereoCaptureMode();
	public:
		DWORD CaptureThreadFunction();
		DWORD EncodingThreadFunction();

		DWORD BeginCaptureInThread();
		DWORD CaptureVideoFrame();
		DWORD EndCaptureInThread();
		
		DWORD encode_audio_frame_data(const BYTE* pData, int nDataSize = 0, int* pnFrameCount = 0);

		bool CaptureAudioFrame();

		void DuplicateLastFrame();

		int GetRecordingFPS();

		void SetErrorCode(int nErrorCode = 1){ m_nLastError = nErrorCode; };

	private:
		AVStream *add_stream(AVFormatContext *oc, AVCodec **codec, int codec_id);
		int open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st);
		void close_video(AVFormatContext *oc, AVStream *st);
		int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt);
		int open_audio(AVFormatContext *oc, AVCodec *codec, AVStream *st);
		void close_audio(AVFormatContext *oc, AVStream *st);

		void CaptureThreadFunctionCaptureLoopOriginal();
		void CaptureThreadFunctionCaptureLoop1080P();
	
	private:
		/** Video capture mode	*/
		MOVIE_CAPTURE_MODE m_nStereoCaptureMode;
		/** current preferred codec. default to mp4 */
		int m_nCodec;
		/** raw codec for storing packets of RGB values. default to MPEG4 */
		int m_nRawCodec;
		/** current filename that is being recorded. */
		std::string m_filename;
		FILE * m_pFile;
		AVCodecContext * m_pCodecContext;
		AVFrame * m_pAvFrame;
		SwrContext* m_swr_ctx;
		AVFrame * m_audio_frame;
		

		AVFormatContext * m_pFormatContext;
		AVOutputFormat * m_pOutputFormat;
		AVStream * m_audio_st;
		AVStream * m_video_st;
		AVCodec * m_audio_codec;
		AVCodec * m_video_codec;

		CAudioCapture* m_pAudioCapture;
		bool m_bCaptureAudio;
		bool m_bCaptureMic;

		/** current frame number*/
		int m_nCurrentFrame;

		int m_nVideoBitRate;
		int m_nAudioBitRate;
		int m_nAudioSampleRate;

		/** preferred recording FPS. */
		int m_nRecordingFPS;

		HWND m_hWndCaptured;
		int m_nLeft;
		int m_nTop;
		/** output width */
		int m_nWidth;
		/** output height */
		int m_nHeight;
		/** margin top from the back buffer. */
		int m_nMarginTop;
		/** if BeginCapture is called. */
		bool m_bIsBegin;
		bool m_bEnableAsyncEncoding;
		std::thread m_capture_thread;
		std::thread m_encoding_thread;
		std::mutex m_mutex;
		// this is actually not quite useful now, as both video/audio encoding are done in a single encoding thread. 
		std::recursive_mutex m_mutex_io_writer;
		std::condition_variable m_encoding_task_signal;
		int m_nLastError;
		bool m_bStartEvent;
		bool m_bStopEvent;
		/** in ms seconds*/
		int64_t m_nCaptureInterval;
		/** default to 15, which is usually half a second. */
		int m_nMaxCacheFrames;
		std::list<CaptureFrameDataPtr> m_video_encode_queue;
		std::deque<CaptureFrameDataPtr> m_audio_encode_queue;


		HDC m_CompatibleHDC;
		HBITMAP m_BitmapHandle;


		/* audio related params*/
		uint8 ** m_src_samples_data;
		int       m_src_samples_linesize;
		int       m_src_nb_samples;
		int		m_max_dst_nb_samples;
		uint8 **m_dst_samples_data;
		int       m_dst_samples_linesize;
		int       m_dst_samples_size;
		int m_samples_count;
		int m_nLastLeftDataCount;
		int m_nLastLostVideoFrame;
		bool m_bCaptureMouse;
		std::vector<BYTE> g_buffer[2];
		int m_nLastBitmapByteCount;

		// add by Cheng Yuanchu in May 8th, 2018 
		std::string m_strAudioMap;
		AudioMixer* m_AudioMixer;
		unsigned int m_nFrameIndex;
	};
}