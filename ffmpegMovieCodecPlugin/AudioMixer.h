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
	AudioFile(std::string file, unsigned int s, unsigned int e)
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

	void SetCaptureStartTime(unsigned int );

private:
	void InitResampleSettings(AVFormatContext* pInputFormatContext);
	void InitAudioStream();
	void InitFilerGraph(int chanels);
	void ProcessInputAudios();
	void CleanUp();

	int SelectSampleRate(const AVCodec *codec);
	int CheckSampleFmt(const AVCodec *codec, enum AVSampleFormat sample_fmt);
	int SelectChannelLayout(const AVCodec *codec);

	int OpenAudioInput(const char *filename, AVFormatContext*& fmt_ctx, AVCodecContext*& dec_ctx);
	void OpenInputs();
	void CollectInputsInfo();
	void MixAudios();

private:
	// about input audios 
	std::vector<AudioFile> m_Audios;
	std::vector<AVCodecContext*> m_InputCodecCtxs;
	std::vector<AVFormatContext*> m_InputFmtCtxs;
	AVFormatContext* m_pOutputFmtCtx;
	AVOutputFormat* m_pOutputFmt;
	SwrContext* m_pSWRctx;
	AVStream* m_OutputAudioSt;
	AVCodecContext* m_OutputAudioCodecCtx;
	AVCodec* m_OutputAudioCodec;

	// filters
	std::vector<AVFilterContext*> m_BufferSrcCtx;
	std::vector<AVFilterContext*> m_DelayCtx;
	AVFilterContext* m_BuffersinkCtx;
	AVFilterContext* m_MixCtx;
	AVFilterGraph* m_FilterGraph;

	// record the frame number when capture starts 
	unsigned long m_nCaptureStartTime;

	uint8_t* m_pConvertedDataBuffer;

	// a reference to MovieCodec instance for current FPS
	ParaEngine::MovieCodec* _movieCodec;
};


