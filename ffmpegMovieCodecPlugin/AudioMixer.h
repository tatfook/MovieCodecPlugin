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

struct AudioRecord
{
	AudioRecord(std::string file, int s, int e, int seek = 0, bool isLoop = false)
		: m_strFileName(file)
		, m_nStartTime(s)
		, m_nEndTime(e)
		, m_nSeekPos(isLoop)
		, m_bIsLoop()
	{}
	std::string m_strFileName;
	int m_nStartTime;
	int m_nEndTime;
	int m_nSeekPos; // the seek positon where it starts when the audio engines play the audio file with name m_WaveFileName
	bool m_bIsLoop;
};
typedef std::vector<AudioRecord> AudioFiles;

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
	void SetCaptureEndTime(unsigned int);

private:
	void InitAudioStream();
	void CreateMixFilterGraph(std::vector<AudioRecord>& inputs);
	void ProcessInputAudios();
	void CleanUp();

	int SelectSampleRate(const AVCodec *codec);
	int CheckSampleFmt(const AVCodec *codec, enum AVSampleFormat sample_fmt);
	int SelectChannelLayout(const AVCodec *codec);

	int OpenAudioInput(const char *filename, AVFormatContext*& fmt_ctx, AVCodecContext*& dec_ctx);
	void OpenInputs(const std::vector<AudioRecord>& inputs);
	void CollectInputsInfo();
	void MixAudios(AudioRecord& merged);
	void DelayAllInputs(std::vector<AudioRecord>& delayedAudios);
	std::string ClipAudio(AudioRecord);

	void MergeInputs(const std::vector<AudioRecord>& todo, AudioRecord& result);

private:
	// about input audios 
	std::vector<AudioRecord> m_Audios;
	std::vector<AVCodecContext*> m_InputCodecCtxs;
	std::vector<AVFormatContext*> m_InputFmtCtxs;
	AVFormatContext* m_pOutputFmtCtx;
	AVOutputFormat* m_pOutputFmt;
	AVStream* m_OutputAudioSt;
	AVCodecContext* m_OutputAudioCodecCtx;
	AVCodec* m_OutputAudioCodec;

	// filters
	std::vector<AVFilterContext*> m_BufferSrcCtx;
	AVFilterContext* m_BuffersinkCtx;
	AVFilterGraph* m_FilterGraph;

	// record the frame number when capture starts 
	unsigned int m_nCaptureStartTime;
	unsigned int m_nCaptureEndTime;

	uint8_t* m_pConvertedDataBuffer;

	// a reference to MovieCodec instance for current FPS
	ParaEngine::MovieCodec* _movieCodec;
};


