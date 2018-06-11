#include"AudioMixer.h"
#include"stdafx.h"

extern "C"
{

#include "libavcodec/avcodec.h"

#include "libavutil/channel_layout.h"
#include "libavutil/md5.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

#include "libavformat/avformat.h"
//#include "libavfilter/avfiltergraph.h"

#include "libavformat/avio.h"

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"

#include "libswresample\swresample.h"
}

#include"MovieCodec.h"

#include <stdio.h>
#include<regex>
#include<algorithm>
#include<locale>
#include<codecvt>

using namespace ParaEngine;

void UTF8_to_GB2312(const char* utf8, string& gb2312_str)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	wchar_t* wstr = new wchar_t[len + 1];
	memset(wstr, 0, len + 1);
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wstr, len);
	len = WideCharToMultiByte(CP_ACP, 0, wstr, -1, NULL, 0, NULL, NULL);
	char* str = new char[len + 1];
	memset(str, 0, len + 1);
	WideCharToMultiByte(CP_ACP, 0, wstr, -1, str, len, NULL, NULL);
	delete[] wstr;
	gb2312_str = str;
	delete[] str;
	return;
}

void GB2312_to_UTF8(const char* gb2312, wstring& utf8_str)
{
	int len = MultiByteToWideChar(CP_ACP, 0, gb2312, -1, NULL, 0);
	wchar_t* wstr = new wchar_t[len + 1];
	memset(wstr, 0, len + 1);
	MultiByteToWideChar(CP_ACP, 0, gb2312, -1, wstr, len);
	utf8_str = wstr;
	//len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	//char* str = new char[len + 1];
	//memset(str, 0, len + 1);
	//WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
	delete[] wstr;
	//utf8_str = str;
	//delete[] str;
	return;
}

AudioMixer::AudioMixer(AVFormatContext* fmtCtx, ParaEngine::MovieCodec* movieCodec)
	: m_pOutputFormatContext(fmtCtx)
	, m_pOutputFormat(fmtCtx->oformat)
	, m_pSWRctx(nullptr)
	, m_pAudioStream(nullptr)
	, m_pBuffersinkFilterContext(nullptr)
	, _movieCodec( movieCodec)
	, m_pAudioCoder(nullptr)
	, m_pConvertedDataBuffer(nullptr)
{
	m_Audios.clear();
	InitAudioStream();
}

AudioMixer::~AudioMixer()
{
	
}

bool AudioMixer::Mix()
{
	ProcessInputAudios();
	CleanUp();
	return true;
}

void AudioMixer::InitResampleSettings(AVFormatContext* pInputFormatContext)
{
	AVCodecContext* pInCedecContext = pInputFormatContext->streams[0]->codec;
	/* create resampler context */

	if (m_pSWRctx != nullptr) swr_free(&m_pSWRctx);
	m_pSWRctx = swr_alloc();
	if (!m_pSWRctx) {
		OUTPUT_LOG("Could not allocate resampler context! \n");
		return ;
	}

	/* set options */
	av_opt_set_channel_layout(m_pSWRctx, "in_channel_layout", pInCedecContext->channel_layout, 0);
	av_opt_set_channel_layout(m_pSWRctx, "out_channel_layout", m_pAudioEncoderContext->channel_layout, 0);

	av_opt_set_int(m_pSWRctx, "in_channel_count", pInCedecContext->channels, 0);
	av_opt_set_int(m_pSWRctx, "out_channel_count", m_pAudioEncoderContext->channels, 0);

	av_opt_set_int(m_pSWRctx, "in_sample_rate", pInCedecContext->sample_rate, 0);
	av_opt_set_int(m_pSWRctx, "out_sample_rate", m_pAudioEncoderContext->sample_rate, 0);

	av_opt_set_sample_fmt(m_pSWRctx, "in_sample_fmt", pInCedecContext->sample_fmt, 0);
	av_opt_set_sample_fmt(m_pSWRctx, "out_sample_fmt", m_pAudioEncoderContext->sample_fmt, 0);
	
	
	
	/* initialize the resampling context */
	if ((swr_init(m_pSWRctx)) < 0) {
		OUTPUT_LOG("Failed to initialize the resampling context!\n");
		return;
	}
}

void AudioMixer::InitAudioStream( )
{
	/* find the encoder */
	m_pAudioCoder = avcodec_find_encoder(m_pOutputFormat->audio_codec);
	if (!m_pAudioCoder) {
		OUTPUT_LOG("Could not find audio encoder !\n");
		return;
	}
	m_pAudioStream = avformat_new_stream(m_pOutputFormatContext, m_pAudioCoder);
	if (!m_pAudioStream) {
		OUTPUT_LOG("Could not allocate stream\n");
		return;
	}
	else {
		// keep codec pointer for later use
		m_pAudioEncoderContext = m_pAudioStream->codec;
		m_pAudioStream->id = m_pOutputFormatContext->nb_streams - 1;
	}

	m_pAudioEncoderContext->codec_type = AVMEDIA_TYPE_AUDIO;
	
	m_pAudioEncoderContext->sample_fmt = m_pAudioCoder->sample_fmts[0];
	m_pAudioEncoderContext->bit_rate = 64000;// for test
	m_pAudioEncoderContext->sample_rate = 44100;
	m_pAudioEncoderContext->channels = 2;
	m_pAudioEncoderContext->channel_layout = AV_CH_LAYOUT_STEREO;

	/* Some formats want stream headers to be separate. */
	if (m_pOutputFormat->flags & AVFMT_GLOBALHEADER)
		m_pAudioEncoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// open it
	int ret = avcodec_open2(m_pAudioEncoderContext, m_pAudioCoder, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Could not open audio codec: %d\n", ret);
		return;
	}
}

void AudioMixer::InitFilerGraph(int chanels)
{
	if (chanels < 1) return;
	// Create a new filtergraph, which will contain all the filters. 
	AVFilterGraph* filterGraph = avfilter_graph_alloc();
	if (!filterGraph) {
		OUTPUT_LOG("Failed to initialize the filter graph!\n");
		return ;
	}

	// Create the abuffer filter to buffer audio frames, and make them available to the filter chain.
	
	m_pBufferFilterContexts.resize(chanels);
	m_pDelayFilterContexts.resize(chanels);
	char args[512];
	for (int i = 0; i < chanels; ++i) {
		if (!m_pInputFormatContexts[i])continue;
		const AVFilter* buffer = avfilter_get_by_name("abuffer");
		if (!buffer) {
			OUTPUT_LOG("Could not find the abuffer filter!\n");
			return ;// note: mem leak
		}
		// buffer audio source: the decoded and converted frames from the decoder and decoder will be inserted here.
		if (!m_pAudioEncoderContext->channel_layout)
			m_pInputCodecContexts[i]->channel_layout = av_get_default_channel_layout(m_pInputCodecContexts[i]->channels);
		char name[16];
		snprintf(name, sizeof(name), "src%d", i);
		snprintf(args, sizeof(args),
			"sample_rate=%d:sample_fmt=%s:channel_layout=0x%d",
			m_pAudioEncoderContext->sample_rate,
			av_get_sample_fmt_name(m_pAudioEncoderContext->sample_fmt),
			m_pAudioEncoderContext->channel_layout );
		int ret = avfilter_graph_create_filter(&m_pBufferFilterContexts[i], buffer, name, args, NULL, filterGraph);
		if (ret < 0) {
			return;// note: mem leak
		}

		// Create delay filter to delay one or more audio channels.
		// When mixing the tracks, we need to consider that they might(and probably have) started at different times.
		// If we were to merge tracks without taking this into account, we would end up with synchronization issues.
		const AVFilter* pDelayFilter = avfilter_get_by_name("adelay");
		if (!pDelayFilter) {
			OUTPUT_LOG("Could not find the adelay filter!\n");
			return;// note: mem leak
		}
		// Beware of this filter's args
		int timeLapse = m_Audios[i].m_nStartFrameNum - m_nCaptureStartTime;
		if (timeLapse < 0) {
			std::string msg = m_Audios[i].m_strFileName + " has played before caputre!\n";
			OUTPUT_LOG(msg.c_str());
			timeLapse = 0;
		}

		// Remove the last "|" symbol.
		snprintf(args, sizeof(args),"%d|%d", timeLapse, timeLapse);
		avfilter_graph_create_filter(&m_pDelayFilterContexts[i], pDelayFilter, "adelay",
			args, NULL, filterGraph);
	}

	// Create mix filter.
	const AVFilter* pMixFilter = avfilter_get_by_name("amix");
	if (!pMixFilter) {
		OUTPUT_LOG("Could not find the mix filter!\n");
		return;// note: mem leak
	}

	snprintf(args, sizeof(args), "inputs=%d:duration=longest:dropout_transition=0", chanels);
	avfilter_graph_create_filter(&m_pMixFilterContext, pMixFilter, "amix", args, NULL, filterGraph);
	OUTPUT_LOG("Total amerge input : %d \n", chanels);

	// Create afomat filter
	const AVFilter* pAformatFilter = avfilter_get_by_name("aformat");
	if (!pAformatFilter) {
		OUTPUT_LOG("Could not find the aformat filter!\n");
		return;// note: mem leak
	}
	AVFilterContext* pAformatFilterContext = nullptr;
	snprintf(args, sizeof(args), "sample_fmts=%s:sample_rates=%d:channel_layouts=%d",
		av_get_sample_fmt_name(m_pAudioEncoderContext->sample_fmt),
		m_pAudioEncoderContext->sample_rate, m_pAudioEncoderContext->channel_layout);
	avfilter_graph_create_filter(&pAformatFilterContext, pAformatFilter, "aformat", args, NULL, filterGraph);
	if (!pAformatFilterContext) {
		OUTPUT_LOG("Could not create the aformat filter context!\n");
		return;// note: mem leak
	}

	// Finally create the abuffersink filter to buffer audio frames, and make them available to the end of filter chain.
	const AVFilter* pBuffersinkFilter = avfilter_get_by_name("abuffersink");
	if (!pBuffersinkFilter) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		return;// note: mem leak
	}
	m_pBuffersinkFilterContext = avfilter_graph_alloc_filter(filterGraph, pBuffersinkFilter, "sink");
	if (!pBuffersinkFilter) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		return;// note: mem leak
	}


	//// Same sample fmts as the output file. */
	//AVSampleFormat FMT[2] = { m_pAudioCoder->sample_fmts[0], AV_SAMPLE_FMT_NONE };
	//int ret = av_opt_set_int_list(m_pBuffersinkFilterContext,
	//	"sample_fmts",
	//	FMT,
	//	AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	//if (ret < 0) {
	//	OUTPUT_LOG("Could set options to the abuffersink instance!\n");
	//	return;// note: mem leak
	//}
	//
	//char ch_layout[64];
	//av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, m_Audios.size());
	//av_opt_set(m_pBuffersinkFilterContext, "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);

	int ret = avfilter_init_str(m_pBuffersinkFilterContext, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Could not initialize the abuffersink instance!\n");
		return;// note: mem leak
	}

	// Link the filters.
	for (int i = 0; i < m_pBufferFilterContexts.size(); ++i) {
		if (!m_pInputFormatContexts[i])continue;
		ret = avfilter_link(m_pBufferFilterContexts[i], 0, m_pDelayFilterContexts[i], 0);
		if (ret < 0) {
			OUTPUT_LOG("Could not link the buffer filter to mix filter!\n");
			return;// note: mem leak
		}
		ret = avfilter_link(m_pDelayFilterContexts[i], 0, m_pMixFilterContext, i);
		if (ret < 0) {
			OUTPUT_LOG("Could not link the buffer filter to mix filter!\n");
			return;// note: mem leak
		}
	}

	ret = avfilter_link(m_pMixFilterContext, 0, pAformatFilterContext, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not link the mix filter to aformat filter!\n");
		return;// note: mem leak
	}

	ret = avfilter_link(pAformatFilterContext, 0, m_pBuffersinkFilterContext, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not link the aformat filter to buffersink filter!\n");
		return;// note: mem leak
	}

	// Configure the graph. 
	ret = avfilter_graph_config(filterGraph, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Error while configuring graph!\n"); 
		return;// note: mem leak
	}
}

void AudioMixer::ProcessInputAudios()
{
	if (m_Audios.empty()) return;
	AudioFiles filteredFiles;
	OUTPUT_LOG("Total input files: %d \n", m_Audios.size());
	// open all the input 
	for (int i = 0; i < m_Audios.size(); ++i) {
		AVFormatContext* pInputFormatContext = nullptr;
		// Open the input file to read from it.

		int ret = avformat_open_input(&pInputFormatContext, m_Audios[i].m_strFileName.c_str(), NULL, NULL);
		if (ret < 0) {
			OUTPUT_LOG("Could not open input file %s \n", m_Audios[i].m_strFileName.c_str());
			continue;// NOTE: what if if it failed?
		}
		OUTPUT_LOG("Open input file %s, startFrame: %d  \n", m_Audios[i].m_strFileName.c_str(), m_Audios[i].m_nStartFrameNum);

		// Get information on the input file (number of streams etc.).
		if (avformat_find_stream_info(pInputFormatContext, NULL) < 0) {
			OUTPUT_LOG("Could not open find stream info (error '%s')\n");
			avformat_close_input(&pInputFormatContext);
			continue;// NOTE: what if if it failed?
		}

		// Find a decoder for the audio stream.
		AVCodec* codec = avcodec_find_decoder(pInputFormatContext->streams[0]->codec->codec_id);
		if (!codec) {
			OUTPUT_LOG("Could not find input codec!\n");
			avformat_close_input(&pInputFormatContext);
			continue;// NOTE: what if if it failed?
		}

		/** Open the decoder for the audio stream to use it later. */
		if (avcodec_open2(pInputFormatContext->streams[0]->codec, codec, NULL) < 0) {
			OUTPUT_LOG("Could not open input codec!\n");
			avformat_close_input(&pInputFormatContext);
			continue;// NOTE: what if if it failed?
		}
		avformat_close_input(&pInputFormatContext);
		filteredFiles.push_back(m_Audios[i]);
	}// end for
	OUTPUT_LOG("Total opened input files: %d \n", filteredFiles.size());
	m_Audios = filteredFiles;
	filteredFiles.clear();
	if (m_Audios.size() < 1) return;

	// test call for ffmpeg.exe
	// test start
	std::string inputCommand;
	for (int i = 0; i < m_Audios.size(); ++i) {
		inputCommand += " -i ";
		inputCommand += m_Audios[i].m_strFileName;
	}
	OUTPUT_LOG("Input:%s\n",inputCommand.c_str());
	
	char* labels[32] = {
		"[a]","[b]","[c]","[d]",
		"[e]","[f]","[g]","[h]",
		"[i]","[j]","[k]","[l]",
		"[m]","[n]","[o]","[p]",
		"[q]","[r]","[s]","[t]",
		"[u]","[v]","[w]","[x]",
		"[y]","[z]","[aa]","[bb]",
		"[cc]","[dd]","[ff]","[ee]"
	};
	std::string filterChains;
	for (int i = 0; i < m_Audios.size(); ++i) {
		int timeLapse = m_Audios[i].m_nStartFrameNum - m_nCaptureStartTime;
		if (timeLapse < 0) timeLapse = 0;
		char chain[32];
		snprintf(chain, sizeof(chain), "[%d]adelay=%d|%d%s;", i, timeLapse, timeLapse, labels[i]);
		filterChains += chain;
	}
	OUTPUT_LOG("filterChains:%s\n", filterChains.c_str());
	
	std::string mixConmand (" ");
	for (int i = 0; i < m_Audios.size(); ++i) {
		mixConmand += labels[i];
	}
	char amix[16];
	snprintf(amix, sizeof(amix), "amix=%d\" ", m_Audios.size());
	mixConmand += amix;
	OUTPUT_LOG("mixConmand:%s\n", mixConmand.c_str());
	
	// output file name
	char filePath[256];
	int nNum = snprintf(filePath, sizeof(filePath), "%s", m_Audios[0].m_strFileName.c_str());
	for (int i = 0; i < 256; ++i) {
		if (filePath[i] == '.') {
			char temt[] = "temp.mp3";
			memcpy(filePath + i, temt, sizeof(temt));
			break;
		}
	}
	//std::string tempOutputFileName = std::filesystem::path(filePath).replace_filename("temp.mp3").string();
	OUTPUT_LOG("tempOutputFileName:%s\n", filePath);
	
	// Assemble commands
	std::string commands("ffmpeg.exe ");
	commands += inputCommand;
	commands += " -filter_complex \"";
	commands += filterChains;
	commands += mixConmand;
	commands += filePath;
	OUTPUT_LOG(commands.c_str());
	char commandSet[1024*16];
	snprintf(commandSet, sizeof(commandSet), "%s", commands.c_str());
	
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	// Start the child process. 
	if (!CreateProcess(NULL,   // No module name (use command line)
		commandSet,        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		CREATE_NO_WINDOW,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		) {
		OUTPUT_LOG("Failed to call ffmpeg.exe!\n");
		return;
	}
	// Wait until child process exits.
	WaitForSingleObject(pi.hProcess, INFINITE);
	// Close process and thread handles. 
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	m_Audios.clear();
	m_Audios.push_back(AudioFile(filePath, 0, 0));
	// test end

	m_pInputFormatContexts.resize(m_Audios.size());
	m_pInputCodecContexts.resize(m_Audios.size());
	for (int i = 0; i < m_Audios.size(); ++i) {
		AVFormatContext* pInputFormatContext = nullptr;
		// Open the input file to read from it.
		if (avformat_open_input(&m_pInputFormatContexts[i], m_Audios[i].m_strFileName.c_str(), NULL, NULL) < 0) {
			OUTPUT_LOG("Could not open input file '%s' (error '%s'), m_Audios[i].m_strFileName.c_str()\n");
			continue;// NOTE: what if if it failed?
		}

		// Get information on the input file (number of streams etc.).
		if (avformat_find_stream_info(m_pInputFormatContexts[i], NULL) < 0) {
			OUTPUT_LOG("Could not open find stream info (error '%s')\n");
			avformat_close_input(&m_pInputFormatContexts[i]);
			continue;// NOTE: what if if it failed?
		}

		// Find a decoder for the audio stream.
		AVCodec* codec = avcodec_find_decoder(m_pInputFormatContexts[i]->streams[0]->codec->codec_id);
		if (!codec) {
			OUTPUT_LOG("Could not find input codec!\n");
			avformat_close_input(&m_pInputFormatContexts[i]);
			continue;// NOTE: what if if it failed?
		}

		/** Open the decoder for the audio stream to use it later. */
		if (avcodec_open2(m_pInputFormatContexts[i]->streams[0]->codec, codec, NULL) < 0) {
			OUTPUT_LOG("Could not open input codec!\n");
			avformat_close_input(&m_pInputFormatContexts[i]);
			continue;// NOTE: what if if it failed?
		}

		// Save the decoder context for easier access later. 
		m_pInputCodecContexts[i] = m_pInputFormatContexts[i]->streams[0]->codec;
	}// end for

	InitFilerGraph(m_Audios.size());

	// Decode the frames and send all of them to the buffer
	for (int i = 0; i < m_Audios.size(); ++i) {
		if (!m_pInputFormatContexts[i])continue;
		InitResampleSettings(m_pInputFormatContexts[i]);
		// Allocate and init re-usable frames
		AVFrame* audioFrameDecoded = av_frame_alloc();
		audioFrameDecoded->format = m_pInputCodecContexts[i]->sample_fmt;
		audioFrameDecoded->channel_layout = m_pInputCodecContexts[i]->channel_layout;
		audioFrameDecoded->channels = m_pInputCodecContexts[i]->channels;
		audioFrameDecoded->sample_rate = m_pInputCodecContexts[i]->sample_rate;

		AVFrame* audioFrameConverted = av_frame_alloc();
		audioFrameConverted->nb_samples = m_pAudioEncoderContext->frame_size;
		audioFrameConverted->format = m_pAudioEncoderContext->sample_fmt;
		audioFrameConverted->channel_layout = m_pAudioEncoderContext->channel_layout;
		audioFrameConverted->channels = m_pAudioEncoderContext->channels;
		audioFrameConverted->sample_rate = m_pAudioEncoderContext->sample_rate;

		AVPacket inPacket;
		av_init_packet(&inPacket);
		inPacket.data = NULL;
		inPacket.size = 0;

		int frameFinished = 0;

		while (av_read_frame(m_pInputFormatContexts[i], &inPacket) >= 0) {

			if (inPacket.stream_index == 0) {// for later opt

				int len = avcodec_decode_audio4(m_pInputCodecContexts[i], audioFrameDecoded, &frameFinished, &inPacket);

				if (frameFinished) {

					{
#pragma region	// Convert

						if (!m_pConvertedDataBuffer) {
							if (av_samples_alloc(&m_pConvertedDataBuffer,
								NULL,
								m_pAudioEncoderContext->channels,
								audioFrameConverted->nb_samples,
								m_pAudioEncoderContext->sample_fmt, 0) < 0)
								OUTPUT_LOG("Could not allocate samples");
						}

						int outSamples = swr_convert(m_pSWRctx,
							&m_pConvertedDataBuffer,
							audioFrameConverted->nb_samples,
							(const uint8_t **)audioFrameDecoded->data,
							audioFrameDecoded->nb_samples);
						if (outSamples < 0)
							OUTPUT_LOG("Could not convert");

						size_t buffer_size = av_samples_get_buffer_size(NULL,
							m_pAudioEncoderContext->channels,
							audioFrameConverted->nb_samples,
							m_pAudioEncoderContext->sample_fmt,
							0);
						if (buffer_size < 0)
							OUTPUT_LOG("Invalid buffer size");

						if (avcodec_fill_audio_frame(audioFrameConverted,
							m_pAudioEncoderContext->channels,
							m_pAudioEncoderContext->sample_fmt,
							m_pConvertedDataBuffer,
							buffer_size,
							0) < 0)
							OUTPUT_LOG("Could not fill frame");
						
#pragma endregion

							// Push the audio data from decoded frame into the filtergraph./
						int ret = av_buffersrc_write_frame(m_pBufferFilterContexts[i], audioFrameConverted);
						if( ret < 0)OUTPUT_LOG("Failed to wirte buffer src frame");
					}
				}
			}

		}// end while

		av_frame_free(&audioFrameConverted);
		av_frame_free(&audioFrameDecoded);
		av_free_packet(&inPacket);
		avformat_close_input(&m_pInputFormatContexts[i]);
	}// end for

	{
		// Pull filtered audio frames from the filtergraph 
		AVFrame* audioFrameFiltered = av_frame_alloc();
		audioFrameFiltered->nb_samples = m_pAudioEncoderContext->frame_size;
		audioFrameFiltered->format = m_pAudioEncoderContext->sample_fmt;
		audioFrameFiltered->channel_layout = m_pAudioEncoderContext->channel_layout;
		audioFrameFiltered->channels = m_pAudioEncoderContext->channels;
		audioFrameFiltered->sample_rate = m_pAudioEncoderContext->sample_rate;
		int frameFinished = 0;
		int numFailedFrame = 0;
		int numSuccessFrame = 0;
		while (true) {
			int ret = av_buffersink_get_frame(m_pBuffersinkFilterContext, audioFrameFiltered);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
			if (ret < 0) {
				numFailedFrame++;
				continue;
			}
		
			AVPacket outPacket;
			av_init_packet(&outPacket);
			outPacket.data = NULL;
			outPacket.size = 0;

			if (avcodec_encode_audio2(m_pAudioEncoderContext, &outPacket, audioFrameFiltered, &frameFinished) < 0)
				OUTPUT_LOG("Error encoding audio frame");

			if (frameFinished) {
				outPacket.stream_index = m_pAudioStream->index;

				if (av_interleaved_write_frame(m_pOutputFormatContext, &outPacket) != 0)
					OUTPUT_LOG("Error while writing audio frame");
				numSuccessFrame++;
				av_free_packet(&outPacket);
			}
			av_frame_unref(audioFrameFiltered);
		}// end while
		OUTPUT_LOG("Failed to get audio frame number, %d!\n", numFailedFrame);
		OUTPUT_LOG("Success to get audio frame number, %d!\n", numSuccessFrame);
	}

	// Don't forget to close the audio encoder
	avcodec_close(m_pAudioStream->codec); 

	std::remove(filePath);
}

int AudioMixer::ReadAndDecodeAudioFrame(AVFrame *frame, AVFormatContext *inputFormatContext,
	AVCodecContext* inputCodecContext, int& hasData, bool& finished)
{
	/** Packet used for temporary storage. */
	AVPacket input_packet;
	InitPacket(&input_packet);
	int error = av_read_frame(inputFormatContext, &input_packet);
	/** Read one audio frame from the input file into a temporary packet. */
	if (error  < 0) {
		/** If we are the the end of the file, flush the decoder below. */
		if (error == AVERROR_EOF)
			finished = true;
		else {
			OUTPUT_LOG("Could not read frame!\n");
			return error;
		}
	}

	/**
	* Decode the audio frame stored in the temporary packet.
	* The input audio stream decoder is used to do this.
	* If we are at the end of the file, pass an empty packet to the decoder
	* to flush it.
	*/
	error = avcodec_decode_audio4(inputCodecContext, frame, &hasData, &input_packet);
	if (error < 0) {
		OUTPUT_LOG("Could not decode frame!\n"); 
		av_free_packet(&input_packet);
		return error;
	}

	/**
	* If the decoder has not been flushed completely, we are not finished,
	* so that this function has to be called again.
	*/
	if (finished && hasData)
		finished = 0;
	av_free_packet(&input_packet);
	return 0;
}

int AudioMixer::EncodeAndWriteAudioFrame(AVFrame* frame, AVFormatContext* outputFormatContext,
	AVCodecContext* outputCodecContext)
{
	/** Packet used for temporary storage. */
	AVPacket* output_packet = av_packet_alloc();

	/**
	* Encode the audio frame and store it in the temporary packet.
	* The output audio stream encoder is used to do this.
	*/
	int hasData = -1;
	int error = avcodec_encode_audio2(outputCodecContext, output_packet, frame, &hasData);
	if (error < 0) {
		OUTPUT_LOG("Could not encode frame !\n");
		av_free_packet(output_packet);
		return error;
	}

	/** Write one audio frame from the temporary packet to the output file. */
	if (hasData) {
		int error = av_write_frame(outputFormatContext, output_packet);
		if (error < 0) {
			OUTPUT_LOG("Could not write frame !\n"); 
			av_free_packet(output_packet);
			return error;
		}
		av_free_packet(output_packet); 
	}

	return 0; 
}

/** Initialize one audio frame for reading from the input file */
int AudioMixer::InitInputFrame(AVFrame **frame)
{
	if (!(*frame = av_frame_alloc())) {
		av_log(NULL, AV_LOG_ERROR, "Could not allocate input frame\n");
		return AVERROR(ENOMEM);
	}
	return 0;
}

/** Initialize one data packet for reading or writing. */
void AudioMixer::InitPacket(AVPacket *packet)
{
	av_init_packet(packet);
	/** Set the packet data and size so that it is recognized as being empty. */
	packet->data = NULL;
	packet->size = 0;
}

void AudioMixer::ParseAudioFiles(const std::string& rawdata)
{
	m_Audios.clear();
	std::regex re(",");
	std::sregex_token_iterator
		first{ rawdata.begin(), rawdata.end(), re, -1 },
		last;
	std::vector<std::string> audioFiles = { first, last };
	for (int i = 0; i < audioFiles.size()-1; i+=2 ) {
		std::string name = audioFiles[i];
		std::string::iterator iter = std::find(name.begin(), name.end(), '.');
		if (iter == name.end())continue;
		unsigned long startTIme = std::stoi(audioFiles[i + 1], std::string::size_type());
		m_Audios.emplace_back(AudioFile(name, startTIme, 0));
	}
}

void AudioMixer::SetCaptureStartTime(unsigned int startTime)
{
	m_nCaptureStartTime = startTime;
}

void AudioMixer::CleanUp()
{
	if (m_pSWRctx != nullptr) swr_free(&m_pSWRctx);
	
	// free the abuffersink filter
	//avfilter_free(m_pBuffersinkFilterContext);

	// free the amix filter
	// already null
	//avfilter_free(m_pMixFilterContext);

	// free the adelay filter
	//for (int i = 0; i < m_pDelayFilterContexts.size(); ++i) {
	//	avfilter_free(m_pDelayFilterContexts[i]);
	//}

	// free the abuffer filter
	//for (int i = 0; i < m_pBufferFilterContexts.size(); ++i) {
	//	avfilter_free(m_pBufferFilterContexts[i]);
	//}

	// free the graph
	//avfilter_graph_free(&m_pFilterGraph);

	// free all the input format
	//for (int i = 0; i < m_pInputFormatContexts.size(); ++i) {
	//	avformat_free_context(m_pInputFormatContexts[i]);
	//}

	av_freep(&((&m_pConvertedDataBuffer)[0]));
}
