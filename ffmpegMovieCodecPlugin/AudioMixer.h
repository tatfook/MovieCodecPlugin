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

namespace ParaEngine {
	class MovieCodec;
}


// Merge multi audio file(s) into ine single file.
struct AudioFile
{
	AudioFile(std::string file, float s, float e)
		:m_strFileName(file)
		, m_fStartTime(s)
		, m_fEndTime(e)
	{}
	std::string m_strFileName;
	unsigned int m_fStartTime;
	unsigned int m_fEndTime;
};


class AudioMixer
{
public:
	AudioMixer(AVFormatContext* fmtCtx, ParaEngine::MovieCodec* movieCodec);
	~AudioMixer();

	void AddAudioFile( AudioFile& audio);

	bool Mix();

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
private:
	std::vector<AudioFile> m_Audios;
	std::vector<AVCodecContext*> m_pInputCodecContexts;
	std::vector<AVFormatContext*> m_pInputFormatContexts;
	std::vector<AVFilterContext*> m_pBufferFilterContexts;
	std::vector<AVFilterContext*> m_pDelayFilterContexts;
	AVFilterContext* m_pBuffersinkFilterContext;
	AVFormatContext* m_pOutputFormatContext;
	AVOutputFormat* m_pOutputFormat;
	SwrContext* m_pSWRctx;

	AVStream* m_pAudioStream;
	AVCodecContext* m_pAudioEncoderContext;
	AVCodec* m_pAudioCoder;


	ParaEngine::MovieCodec* _movieCodec;
};


