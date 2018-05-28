#pragma once

#include<string>
#include<vector>

struct AVFormatContext;
struct AVOutputFormat;
struct AVCodec;
struct SwrContext;
struct AVCodecContext;
struct AVFilterContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct AVFilterGraph;

namespace ParaEngine {
	class MovieCodec;
}

struct AudioFile
{
	AudioFile(std::string file, float s, float e)
		:m_strFileName(file)
		, m_nStartFrameNum(s)
		, m_nEndFrameNum(e)
	{}
	std::string m_strFileName;
	unsigned int m_nStartFrameNum;
	unsigned int m_nEndFrameNum;
};
typedef std::vector<AudioFile> AudioFiles;

/** 
This class merges multi audio streams (.wav, .au etc.) inputs into a single one with proper delay(s).
NOTE:If your audio stream is for example longer than the video stream, you have to cut it or otherwise you will have the last 
video frame as a still image and audio running.
*/
class AudioMixer
{
public:
	AudioMixer(AVFormatContext* fmtCtx, ParaEngine::MovieCodec* movieCodec);
	~AudioMixer();

	void ParseAudioFiles(const std::string& rawdata);

	bool Mix();

	void SetCaptureStartFrameNum(unsigned int );

private:
	void InitResampleSettings(AVFormatContext* pInputFormatContext);
	void InitAudioStream();
	void InitFilerGraph(int chanels);
	void ProcessInputAudios();
	int ReadAndDecodeAudioFrame(AVFrame* frame, AVFormatContext* inputFormatContext,
		AVCodecContext* inputCodecContext, int& hasData, bool& finished);
	int EncodeAndWriteAudioFrame(AVFrame* frame, AVFormatContext* outputFormatContext,
		AVCodecContext* outputCodecContext);
	int InitInputFrame(AVFrame **frame);
	void InitPacket(AVPacket *packet);
	void CleanUp();

private:
	// about input audios 
	std::vector<AudioFile> m_Audios;
	std::vector<AVCodecContext*> m_pInputCodecContexts;
	std::vector<AVFormatContext*> m_pInputFormatContexts;
	AVFormatContext* m_pOutputFormatContext;
	AVOutputFormat* m_pOutputFormat;
	SwrContext* m_pSWRctx;
	AVStream* m_pAudioStream;
	AVCodecContext* m_pAudioEncoderContext;
	AVCodec* m_pAudioCoder;

	// filters
	std::vector<AVFilterContext*> m_pBufferFilterContexts;
	std::vector<AVFilterContext*> m_pDelayFilterContexts;
	AVFilterContext* m_pBuffersinkFilterContext;
	AVFilterContext* m_pMixFilterContext;
	AVFilterGraph* m_pFilterGraph;

	// record the frame number when capture starts 
	unsigned long m_nCaptureStartFrameNum;

	uint8_t* m_pConvertedDataBuffer;

	// a reference to MovieCodec instance for current FPS
	ParaEngine::MovieCodec* _movieCodec;
};


