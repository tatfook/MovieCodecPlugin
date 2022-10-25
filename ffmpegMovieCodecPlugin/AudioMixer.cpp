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
#include<filesystem>
#include<fstream>

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
	: m_pOutputFmtCtx(fmtCtx)
	, m_pOutputFmt(fmtCtx->oformat)
	, m_OutputAudioSt(nullptr)
	, m_BuffersinkCtx(nullptr)
	, _movieCodec( movieCodec)
	, m_OutputAudioCodec(nullptr)
	, m_pConvertedDataBuffer(nullptr)
	, m_FilterGraph(nullptr)
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
	return true;
}

void AudioMixer::InitAudioStream( )
{
	/* find the encoder */
	m_OutputAudioCodec = avcodec_find_encoder(m_pOutputFmt->audio_codec);
	if (!m_OutputAudioCodec) {
		OUTPUT_LOG("Could not find audio encoder !\n");
		return;
	}
	m_OutputAudioSt = avformat_new_stream(m_pOutputFmtCtx, m_OutputAudioCodec);
	if (!m_OutputAudioSt) {
		OUTPUT_LOG("Could not allocate stream\n");
		return;
	}else {
		// keep codec pointer for later use
		m_OutputAudioCodecCtx = m_OutputAudioSt->codec;
		m_OutputAudioSt->id = m_pOutputFmtCtx->nb_streams - 1;
	}

	m_OutputAudioCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
	m_OutputAudioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;//m_OutputAudioCodec->sample_fmts[0]
	m_OutputAudioCodecCtx->bit_rate = 1280000;// for a  better audio quality
	m_OutputAudioCodecCtx->sample_rate = this->SelectSampleRate(m_OutputAudioCodec);
	m_OutputAudioCodecCtx->channel_layout = this->SelectChannelLayout(m_OutputAudioCodec);
	m_OutputAudioCodecCtx->channels = av_get_channel_layout_nb_channels(m_OutputAudioCodecCtx->channel_layout); 

	/* Some formats want stream headers to be separate. */
	if (m_pOutputFmt->flags & AVFMT_GLOBALHEADER)
		m_OutputAudioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// open it
	int ret = avcodec_open2(m_OutputAudioCodecCtx, m_OutputAudioCodec, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Could not open audio codec: %d\n", ret);
		return;
	}
}

void AudioMixer::CreateMixFilterGraph(std::vector<AudioRecord>& inputAudios)
{
	int chanels = inputAudios.size();
	if (chanels < 1) return;
	// Create a new filtergraph, which will contain all the filters. 
	m_FilterGraph = avfilter_graph_alloc();
	if (!m_FilterGraph) {
		OUTPUT_LOG("Failed to initialize the filter graph!\n");
		return;
	}

	char filter_descr[512];
	if (chanels == 1) {
		snprintf(filter_descr, sizeof(filter_descr), "volume=2.0,amix=inputs=%d:duration=longest:dropout_transition=2", chanels);
	}else if (chanels == 2) {
		snprintf(filter_descr, sizeof(filter_descr), "[0]volume=1.2[a];[1]volume=1.2[b];[a][b]amix=inputs=%d:duration=longest:dropout_transition=2", chanels);
	}else {
		snprintf(filter_descr, sizeof(filter_descr), "amix=inputs=%d:duration=longest:dropout_transition=0", chanels);
	}
	

	AVFilterInOut* inputs = nullptr;
	AVFilterInOut* outputs = nullptr;
	int ret = avfilter_graph_parse_ptr(m_FilterGraph, filter_descr, &inputs, &outputs, nullptr);

	if (ret < 0) {
		OUTPUT_LOG(filter_descr);
		OUTPUT_LOG("Failed to parse filter description!");
		return;
	}

	AVFilterInOut* currentInput = inputs;
	m_BufferSrcCtx.resize(chanels);
	for (int i = 0; i < chanels; ++i, currentInput = currentInput->next) {
		if (!m_InputFmtCtxs[i])continue;
		// Create the abuffer filter to buffer audio frames, and make them available to the filter chain.
		const AVFilter* buffer = avfilter_get_by_name("abuffer");
		if (!buffer) {
			OUTPUT_LOG("Could not find the abuffer filter!\n");
			return;// note: mem leak
		}
		if (!m_InputCodecCtxs[i]->channel_layout)
			m_InputCodecCtxs[i]->channel_layout = av_get_default_channel_layout(m_InputCodecCtxs[i]->channels);
		char name[16], args[512];
		snprintf(name, sizeof(name), "buffer%d", i);
		snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%llu",
			m_InputCodecCtxs[i]->time_base.num,
			m_InputCodecCtxs[i]->time_base.den,
			m_InputCodecCtxs[i]->sample_rate,
			av_get_sample_fmt_name(m_InputCodecCtxs[i]->sample_fmt),
			m_InputCodecCtxs[i]->channel_layout);
		int ret = avfilter_graph_create_filter(&m_BufferSrcCtx[i], buffer, name, args, nullptr, m_FilterGraph);
		if (ret < 0) {
			OUTPUT_LOG("Failed to create abuffer filter!\n");
			return;// note: mem leak
		}

		// link abuffer and amix
		ret = avfilter_link(m_BufferSrcCtx[i], 0, currentInput->filter_ctx, currentInput->pad_idx);
		if (ret < 0) {
			OUTPUT_LOG("Could not link the abuffer filter to adelay filter!\n");
			return;// note: mem leak
		}
	}

	// Finally create the abuffersink filter to buffer audio frames, and make them available to the end of filter chain.
	const AVFilter* pBuffersinkFilter = avfilter_get_by_name("abuffersink");
	if (!pBuffersinkFilter) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		return;// note: mem leak
	}
	m_BuffersinkCtx = avfilter_graph_alloc_filter(m_FilterGraph, pBuffersinkFilter, "sink");
	if (!pBuffersinkFilter) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		return;// note: mem leak
	}
	ret = avfilter_link(outputs->filter_ctx, outputs->pad_idx, m_BuffersinkCtx, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not link the amix filter to sink filter!\n");
		return;// note: mem leak
	}
	if (m_OutputAudioCodecCtx->frame_size > 0)
		av_buffersink_set_frame_size(m_BuffersinkCtx, m_OutputAudioCodecCtx->frame_size);

	// Configure the graph. 
	ret = avfilter_graph_config(m_FilterGraph, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Error while configuring graph!\n"); 
		return;// note: mem leak
	}
}

void AudioMixer::DelayAllInputs(std::vector<AudioRecord>& delayedAudios)
{
	delayedAudios.clear();
	std::vector<AVCodecContext*> inputCodecCtxs;
	std::vector<AVFormatContext*> inputFmtCtxs;
	
	inputFmtCtxs.resize(m_Audios.size());
	inputCodecCtxs.resize(m_Audios.size());
	for (int i = 0; i < m_Audios.size(); ++i) {
		if (m_Audios[i].m_nStartTime < m_nCaptureStartTime)continue;// skip the audio playing before capture
		this->OpenAudioInput(m_Audios[i].m_FileName.c_str(), inputFmtCtxs[i], inputCodecCtxs[i]);
		if (!inputFmtCtxs[i])continue;

#pragma region CREATE FILTER GRAPH
		// create delayed filter graph 
		AVFilterGraph* graph = avfilter_graph_alloc();
		if (graph == nullptr) {
			OUTPUT_LOG("Failed to initialize the delayed filter graph!\n");
			continue;
		}

		// set delay 
		int timeLapse = m_Audios[i].m_nStartTime - m_nCaptureStartTime;
		char descrpt[128];
		memset(descrpt, 0, sizeof(descrpt));
		std::sprintf(descrpt, "aresample=icl=%llu:ocl=%llu:isr=%d:osr=%d:isf=%s:osf=%s,adelay=%d|%d,aformat=sample_fmts=%s:sample_rates=%d:channel_layouts=%llu",//apad = whole_len = 11000,
			inputCodecCtxs[i]->channel_layout,
			m_OutputAudioCodecCtx->channel_layout,
			inputCodecCtxs[i]->sample_rate,
			m_OutputAudioCodecCtx->sample_rate,
			av_get_sample_fmt_name(inputCodecCtxs[i]->sample_fmt),
			av_get_sample_fmt_name(m_OutputAudioCodecCtx->sample_fmt),
			timeLapse,
			timeLapse,
			av_get_sample_fmt_name(m_OutputAudioCodecCtx->sample_fmt),
			m_OutputAudioCodecCtx->sample_rate,
			m_OutputAudioCodecCtx->channel_layout
		);

		AVFilterInOut* inputs = nullptr;
		AVFilterInOut* outputs = nullptr;
		int ret = avfilter_graph_parse2(graph, descrpt, &inputs, &outputs);
		if (ret < 0) {
			OUTPUT_LOG(descrpt);
			OUTPUT_LOG("Failed to parse filter description: %s!", descrpt);
			continue;
		}

		// Create the abuffer filter to buffer audio frames, and make them available to the filter chain.
		const AVFilter* buffer = avfilter_get_by_name("abuffer");
		if (buffer == nullptr) {
			OUTPUT_LOG("Could not find the abuffer filter!\n");
			continue;
		}

		char name[16], args[512];

		snprintf(name, sizeof(name), "buffer%d", i);
		snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%llu",
			inputCodecCtxs[i]->time_base.num,
			inputCodecCtxs[i]->time_base.den,
			inputCodecCtxs[i]->sample_rate,
			av_get_sample_fmt_name(inputCodecCtxs[i]->sample_fmt),
			inputCodecCtxs[i]->channel_layout);
		AVFilterContext* abufferCtx = nullptr;
		ret = avfilter_graph_create_filter(&abufferCtx, buffer, name, args, nullptr, graph);
		if (ret < 0) {
			OUTPUT_LOG("Could not create the abuffer filter context!\n");
			continue;
		}
		// link to the inputs
		ret = avfilter_link(abufferCtx, 0, inputs->filter_ctx, inputs->pad_idx);
		if (ret < 0) {
			OUTPUT_LOG("Could not link the abuffer filter to adelay filter!\n");
			continue;// note: mem leak
		}

		// Finally create the abuffersink filter to buffer audio frames, and make them available to the end of filter chain.
		const AVFilter* sinkbuffer = avfilter_get_by_name("abuffersink");
		if (!sinkbuffer) {
			OUTPUT_LOG("Could not find the abuffersink filter!\n");
			continue;// note: mem leak
		}
		AVFilterContext* sinkbufferCtx = avfilter_graph_alloc_filter(graph, sinkbuffer, "sink");
		if (!sinkbuffer) {
			OUTPUT_LOG("Could not find the abuffersink filter!\n");
			continue;// note: mem leak
		}
		ret = avfilter_link(outputs->filter_ctx, outputs->pad_idx, sinkbufferCtx, 0);
		if (ret < 0) {
			OUTPUT_LOG("Could not link the abuffer filter to adelay filter!\n");
			continue;// note: mem leak
		}
		if (m_OutputAudioCodecCtx->frame_size > 0)
			av_buffersink_set_frame_size(sinkbufferCtx, m_OutputAudioCodecCtx->frame_size);

		// Configure the delay filter  graph. 
		ret = avfilter_graph_config(graph, NULL);
		if (ret < 0) {
			OUTPUT_LOG("Error while configuring delay filter graph!\n");
			continue;
		}
#pragma endregion

#pragma region SETUP OUTPUT

		// set output file name
		char filePath[512];
		char suffix[32];
		snprintf(filePath, sizeof(filePath), "%s", m_Audios[i].m_FileName.c_str());
		snprintf(suffix, sizeof(suffix), "_delayed_%d.mp3", i);
		for (int i = 0; i < 512; ++i) {
			if (filePath[i] == '.') {
				memset(filePath + i, 0, sizeof(filePath) - i - 1);
				memcpy(filePath + i, suffix, sizeof(suffix));
				break;
			}
		}
		AVIOContext* ioContext = NULL;
		ret = avio_open(&ioContext, filePath, AVIO_FLAG_WRITE);
		if (ret < 0) {
			OUTPUT_LOG("Failed to open %s: %d\n", filePath);
			continue;
		}

		AVFormatContext* outputFormat = avformat_alloc_context();
		outputFormat->pb = ioContext;
		outputFormat->oformat = av_guess_format(nullptr, filePath, nullptr);
		av_strlcpy(outputFormat->filename, filePath, sizeof((outputFormat)->filename));
		AVCodec* codec = avcodec_find_encoder(outputFormat->oformat->audio_codec);
		AVStream* stream = avformat_new_stream(outputFormat, codec);

		AVCodecContext* outputCodec = stream->codec;
		outputCodec->channels = m_OutputAudioCodecCtx->channels;
		outputCodec->channel_layout = m_OutputAudioCodecCtx->channel_layout;
		outputCodec->sample_rate = m_OutputAudioCodecCtx->sample_rate;
		outputCodec->sample_fmt = m_OutputAudioCodecCtx->sample_fmt;
		outputCodec->bit_rate = m_OutputAudioCodecCtx->bit_rate;

		/** Allow the use of the experimental AAC encoder */
		outputCodec->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

		/** Set the sample rate for the container. */
		stream->time_base.den = outputCodec->sample_rate;
		stream->time_base.num = 1;

		/**
		* Some container formats (like MP4) require global headers to be present
		* Mark the encoder so that it behaves accordingly.
		*/
		if (outputFormat->oformat->flags & AVFMT_GLOBALHEADER)
			outputCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		/** Open the encoder for the audio stream to use it later. */
		ret = avcodec_open2(outputCodec, codec, nullptr);
		if (ret < 0) {
			OUTPUT_LOG("Failed to open the encoder for the audio stream %s .\n", filePath);
			continue;
		}

		// write output file header
		ret = avformat_write_header(outputFormat, nullptr);
		if (ret < 0) {
			OUTPUT_LOG("Failed to write header!\n");
			continue;
		}

#pragma endregion

#pragma region FILTER THE FRAMES
		/* pull filtered audio from the filtergraph */
		AVFrame* inputFrame = av_frame_alloc();
		AVFrame* filteredFrame = av_frame_alloc();
		AVPacket outPkt, inPkt;
		av_init_packet(&outPkt);
		av_init_packet(&inPkt);
		int64_t framePts = 0;

		while (true) {
			// encode filterd frames
			int err = -1;
			do {
				err = av_buffersink_get_frame(sinkbufferCtx, filteredFrame);
				if (err >= 0) {
					filteredFrame->pts = framePts;
					framePts += filteredFrame->nb_samples;
					avcodec_send_frame(outputCodec, filteredFrame);
					if (avcodec_receive_packet(outputCodec, &outPkt) == 0) {
						if (av_interleaved_write_frame(outputFormat, &outPkt) != 0)OUTPUT_LOG("Error while writing audio frame");
						av_packet_unref(&outPkt);
					}
				}
				av_frame_unref(filteredFrame);
			} while (err >= 0);

			// need more input
			if (AVERROR(EAGAIN) == err) {
				if (av_buffersrc_get_nb_failed_requests(abufferCtx) > 0) {
					// decode frames and fill into input filter
					bool finished = false;
					for (int j = 0; j < 128 && !finished; ++j) {
						int rEror = -1;
						while ((rEror = av_read_frame(inputFmtCtxs[i], &inPkt)) >= 0) {
							if (inPkt.stream_index == 0) {// for later opt
								int ret = avcodec_send_packet(inputCodecCtxs[i], &inPkt);
								if (ret < 0) {
									OUTPUT_LOG("Error while sending a packet to the decoder!\n");
									break;
								}
								while (ret >= 0) {
									ret = avcodec_receive_frame(inputCodecCtxs[i], inputFrame);
									if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
										break;
									}
									else if (ret < 0) {
										OUTPUT_LOG("Error while receiving a frame from the decoder!\n");
										break;// go to end
									}
									else {
										/* push the audio data from decoded frame into the filtergraph */
										if (av_buffersrc_add_frame_flags(abufferCtx, inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
											OUTPUT_LOG("Error while feeding the audio filtergraph!\n");
										}
										av_frame_unref(inputFrame);
									}
								}// while
								av_packet_unref(&inPkt);
							}
						}// end while
						if (rEror == AVERROR_EOF) finished = true;
					}
					// end input
					if (finished) av_buffersrc_add_frame(abufferCtx, nullptr);
				}
			}

			// flush encoder
			if ((AVERROR(ENOMEM) == err) || AVERROR_EOF == err) {
				err = avcodec_send_frame(outputCodec, nullptr);
				break;
			}

		}// end while

		av_free_packet(&inPkt);
		av_free_packet(&outPkt);
		av_frame_free(&filteredFrame);
		av_frame_free(&inputFrame);
#pragma endregion

		// write trailer
		ret = av_write_trailer(outputFormat);
		if (ret < 0) {
			OUTPUT_LOG("Failed to write trailer!\n");
		}
		avio_close(outputFormat->pb);
		avcodec_close(inputFmtCtxs[i]->streams[0]->codec);

		avfilter_free(abufferCtx);
		avfilter_free(sinkbufferCtx);
		avformat_close_input(&inputFmtCtxs[i]);
		
		avfilter_graph_free(&graph);
		 
		AudioRecord delayedAudio(filePath, m_nCaptureStartTime, 0);
		delayedAudios.emplace_back(delayedAudio);
	}// end big for
}

void AudioMixer::MergeInputs(const std::vector<AudioRecord>& todo, AudioRecord& result)
{
#pragma region PREPARATIONS
	if (todo.size() < 1) return;

	std::vector<AVCodecContext*> inputCodecCtxs;
	std::vector<AVFormatContext*> inputFmtCtxs;

	// open the todo list
	inputFmtCtxs.resize(todo.size());
	inputCodecCtxs.resize(todo.size());
	for (int i = 0; i < todo.size(); ++i) {
		this->OpenAudioInput(todo[i].m_FileName.c_str(), inputFmtCtxs[i], inputCodecCtxs[i]);
	}
#pragma endregion

#pragma region BUILD MIX FILTER GRAPH
	// create delayed filter graph 
	AVFilterGraph* graph = avfilter_graph_alloc();
	if (graph == nullptr) {
		OUTPUT_LOG("Failed to initialize the delayed filter graph!\n");
		return;
	}
	
	char filter_descr[2048*8];
	std::sprintf(filter_descr, "amix=inputs=%d", todo.size());//,dynaudnorm

	AVFilterInOut* inputs = nullptr;
	AVFilterInOut* outputs = nullptr;
	int ret = avfilter_graph_parse2(graph, filter_descr, &inputs, &outputs);

	if (ret < 0) {
		OUTPUT_LOG("Failed to parse filter description!");
		return;
	}

	// create src buffers
	AVFilterInOut* currentInput = inputs;
	std::vector<AVFilterContext*> srcbufferCtxs(todo.size(), nullptr);
	std::vector<AVFilterContext*> delayCtxs(todo.size(), nullptr);
	for (int i = 0; i < todo.size(); ++i, currentInput = currentInput->next) {
		// Create the abuffer filter to buffer audio frames, and make them available to the filter chain.
		const AVFilter* bufferFilter = avfilter_get_by_name("abuffer");
		if (!bufferFilter) {
			OUTPUT_LOG("Could not find the abuffer filter!\n");
			return;// note: mem leak
		}
		if (!inputCodecCtxs[i]->channel_layout)
			inputCodecCtxs[i]->channel_layout = av_get_default_channel_layout(inputCodecCtxs[i]->channels);
		char name[16], args[512];
		std::sprintf(name, "buffer%d", i);
		std::sprintf(args, "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%llu",
			inputCodecCtxs[i]->time_base.num,
			inputCodecCtxs[i]->time_base.den,
			inputCodecCtxs[i]->sample_rate,
			av_get_sample_fmt_name(inputCodecCtxs[i]->sample_fmt),
			inputCodecCtxs[i]->channel_layout);
		int ret = avfilter_graph_create_filter(&srcbufferCtxs[i], bufferFilter, name, args, NULL, graph);
		if (ret < 0) {
			OUTPUT_LOG("Could not create abuffer filter!\n");
			return;// note: mem leak
		}

		// link abuffer and adelay 
		ret = avfilter_link(srcbufferCtxs[i], 0, currentInput->filter_ctx, currentInput->pad_idx);
		if (ret < 0) {
			OUTPUT_LOG("Could not link the abuffer filter to adelay filter!\n");
			return;// note: mem leak
		}

	}

	// Finally create the abuffersink filter to buffer audio frames, and make them available to the end of filter chain.
	const AVFilter* sinkbufferFilter = avfilter_get_by_name("abuffersink");
	if (!sinkbufferFilter) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		return;// note: mem leak
	}
	AVFilterContext* sinkbufferCtx = avfilter_graph_alloc_filter(graph, sinkbufferFilter, "sink");
	if (!sinkbufferFilter) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		return;// note: mem leak
	}
	ret = avfilter_link(outputs->filter_ctx, outputs->pad_idx, sinkbufferCtx, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not link the abuffer filter to adelay filter!\n");
		return;// note: mem leak
	}
	if (m_OutputAudioCodecCtx->frame_size > 0)
		av_buffersink_set_frame_size(sinkbufferCtx, m_OutputAudioCodecCtx->frame_size);

	// Configure the graph. 
	ret = avfilter_graph_config(graph, NULL);
	if (ret < 0) {
		char erros[256];
		av_make_error_string(erros, 256, ret);
		OUTPUT_LOG("Error while configuring graph!\n");
		return;// note: mem leak
	}
#pragma endregion

#pragma region SET UP OUTPUT 
	// set output file name
	char filePath[512];
	char suffix[32];
	std::sprintf(filePath, "%s", todo[0].m_FileName.c_str());
	std::sprintf(suffix,  "_merged_.mp3");
	for (int i = 0; i < 512; ++i) {
		if (filePath[i] == '.') {
			memset(filePath + i, 0, sizeof(filePath) - i - 1);
			memcpy(filePath + i, suffix, sizeof(suffix));
			break;
		}
	}

	AVIOContext* ioContext = nullptr;
	ret = avio_open(&ioContext, filePath, AVIO_FLAG_WRITE);
	if (ret < 0) {
		OUTPUT_LOG("Failed to open %s: %d\n", filePath);
		return;
	}

	AVFormatContext* outputFormat = avformat_alloc_context();
	outputFormat->pb = ioContext;
	outputFormat->oformat = av_guess_format(nullptr, filePath, nullptr);
	av_strlcpy(outputFormat->filename, filePath, sizeof((outputFormat)->filename));
	AVCodec* codec = avcodec_find_encoder(outputFormat->oformat->audio_codec);
	AVStream* stream = avformat_new_stream(outputFormat, codec);

	AVCodecContext* outputCodec = stream->codec;
	outputCodec->channels = m_OutputAudioCodecCtx->channels;
	outputCodec->channel_layout = m_OutputAudioCodecCtx->channel_layout;
	outputCodec->sample_rate = m_OutputAudioCodecCtx->sample_rate;
	outputCodec->sample_fmt = m_OutputAudioCodecCtx->sample_fmt;
	outputCodec->bit_rate = m_OutputAudioCodecCtx->bit_rate;


	/** Allow the use of the experimental AAC encoder */
	outputCodec->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

	/** Set the sample rate for the container. */
	stream->time_base.den = outputCodec->sample_rate;
	stream->time_base.num = 1;

	/**
	* Some container formats (like MP4) require global headers to be present
	* Mark the encoder so that it behaves accordingly.
	*/
	if (outputFormat->oformat->flags & AVFMT_GLOBALHEADER)
		outputCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	/** Open the encoder for the audio stream to use it later. */
	ret = avcodec_open2(outputCodec, codec, nullptr);
	if (ret < 0) {
		OUTPUT_LOG("Failed to open the encoder for the audio stream %s .\n", filePath);
		return;
	}

	// write output file header
	ret = avformat_write_header(outputFormat, nullptr);
	if (ret < 0) {
		OUTPUT_LOG("Failed to write header!\n");
		return;
	}
#pragma endregion
	
#pragma region FILTERED THE INPUTS

	/* pull filtered audio from the filtergraph */
	AVFrame* inputFrame = av_frame_alloc();
	AVFrame* filteredFrame = av_frame_alloc();
	AVPacket outPkt, inPkt;
	av_init_packet(&outPkt);
	av_init_packet(&inPkt);
	int64_t framePts = 0;

	int numFailedFrame = 0;
	int numSuccessFrame = 0;
	int numReceived = 0;
	int numAddToGraphFrame = 0;

	while (true) {
		// encode filterd frames
		int err = -1;
		do {
			err = av_buffersink_get_frame(sinkbufferCtx, filteredFrame);
			if (err >= 0) {
				filteredFrame->pts = framePts;
				framePts += filteredFrame->nb_samples;
				avcodec_send_frame(outputCodec, filteredFrame);
				numReceived++;
				if (avcodec_receive_packet(outputCodec, &outPkt) == 0) {
					if (av_interleaved_write_frame(outputFormat, &outPkt) != 0) {
						OUTPUT_LOG("Error while writing audio frame");
					}
					numSuccessFrame++;
					av_packet_unref(&outPkt);
				}
			}
			av_frame_unref(filteredFrame);
		} while (err >= 0);

		// need more input
		if (AVERROR(EAGAIN) == err) {
			// find lack source inputs
			std::vector<int> lackSourceInputs;
			for (int i = 0; i < inputFmtCtxs.size(); ++i) {
				if (av_buffersrc_get_nb_failed_requests(srcbufferCtxs[i]) > 0)lackSourceInputs.push_back(i);
			}

			if (lackSourceInputs.size() == 0) break;

			// decode frames and fill into input filter
			for (int i : lackSourceInputs) {
				bool finished = false;
				for (int j = 0; j < 128 && !finished; ++j) {
					int rEror = -1;
					while ((rEror = av_read_frame(inputFmtCtxs[i], &inPkt)) >= 0) {
						if (inPkt.stream_index == 0) {// for later opt
							int ret = avcodec_send_packet(inputCodecCtxs[i], &inPkt);
							if (ret < 0) {
								OUTPUT_LOG("Error while sending a packet to the decoder!\n");
								break;
							}
							
							while (ret >= 0) {
								ret = avcodec_receive_frame(inputCodecCtxs[i], inputFrame);
								if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
									break;
								}
								else if (ret < 0) {
									OUTPUT_LOG("Error while receiving a frame from the decoder!\n");
									break;// go to end
								}
								else {
									/* push the audio data from decoded frame into the filtergraph */
									if (av_buffersrc_add_frame_flags(srcbufferCtxs[i], inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
										OUTPUT_LOG("Error while feeding the audio filtergraph!\n");
									}
									av_frame_unref(inputFrame);
									numAddToGraphFrame++;
								}
							}// while
							av_packet_unref(&inPkt);
						}
					}// end while
					if (rEror == AVERROR_EOF) finished = true;
				}
				// end input
				if (finished) av_buffersrc_add_frame(srcbufferCtxs[i], NULL);
			}
		}

		// flush encoder
		if ((AVERROR(ENOMEM) == err) || AVERROR_EOF == err) {
			err = avcodec_send_frame(outputCodec, nullptr);
			break;
		}
	}// end while

	av_free_packet(&inPkt);
	av_free_packet(&outPkt);
	av_frame_free(&filteredFrame);
	av_frame_free(&inputFrame);

	OUTPUT_LOG("numAddToGraphFrame, %d!\n", numAddToGraphFrame);
	OUTPUT_LOG("Failed to get audio frame number, %d!\n", numFailedFrame);
	OUTPUT_LOG("Success to get audio frame number, %d!\n", numSuccessFrame);
	OUTPUT_LOG("numReceived, %d!\n", numReceived);

#pragma endregion

	// write trailer
	ret = av_write_trailer(outputFormat);
	if (ret < 0) {
		OUTPUT_LOG("Failed to write trailer!\n");
	}
	// free the filters and filter graph
	for (int i = 0; i < todo.size(); ++i) {
		avfilter_free(srcbufferCtxs[i]);
		avfilter_free(delayCtxs[i]);
	}
	avfilter_free(sinkbufferCtx);
	avfilter_graph_free(&graph);

	avio_close(outputFormat->pb);
	for (int i = 0; i < inputFmtCtxs.size(); ++i) {
		avcodec_close(inputFmtCtxs[i]->streams[0]->codec);
		avformat_close_input(&inputFmtCtxs[i]);
	}
	
	result.m_FileName = filePath;
	result.m_nStartTime = m_nCaptureStartTime;
}

void AudioMixer::ProcessInputAudios()
{
	this->CollectInputsInfo();
	if (m_Audios.empty()) return;

	// temporary files that must be removed after this process
	std::vector<AudioRecord> tempRecords;

	// insert loop audios
	std::vector<AudioRecord> loopRecords;
	for (int i = 0; i < m_Audios.size(); ++i) {
		if (m_Audios[i].m_bIsLoop) {
			if (m_Audios[i].m_nEndTime < 0) {
				m_Audios[i].m_nEndTime = m_nCaptureEndTime;
			}

			float dur = (m_Audios[i].m_nEndTime - m_Audios[i].m_nStartTime) / 1000.0 / m_Audios[i].m_Duration;
			int numLoop = (int)dur;
			int duration = m_Audios[i].m_Duration * 1000;// in milliseconds
			for (int j = 0; j < numLoop; ++j) {
				int start = m_Audios[i].m_nEndTime + j * duration;
				int end = -1;
				AudioRecord loopRecord(m_Audios[i].m_FileName, start, end);
				loopRecords.emplace_back(loopRecord);
			}

			m_Audios[i].m_nStartTime + numLoop * duration;
			m_Audios[i].m_nEndTime = (dur - numLoop)*duration;
		}
	}
	m_Audios.insert(m_Audios.end(),loopRecords.begin(), loopRecords.end());

	// clip audios that were stopped when playing
	for (int i = 0; i < m_Audios.size(); ++i) {
		if (m_Audios[i].m_nEndTime > 0 || m_Audios[i].m_nSeekPos > 0) {
			std::string fileName = this->ClipAudio(m_Audios[i]);
			if (fileName != m_Audios[i].m_FileName) {
				m_Audios[i].m_FileName = fileName;
				// new clipped audio file generated, must be removed at the end 
				tempRecords.emplace_back(m_Audios[i]);
			}
			 		
		}
	}

	std::sort(m_Audios.begin(), m_Audios.end(), [](const AudioRecord& a1, const AudioRecord& a2) {
		return a1.m_nStartTime < a2.m_nStartTime; });

	std::vector<AudioRecord> delayedAudios;
	this->DelayAllInputs(delayedAudios);

	std::list<AudioRecord> todos;
	for (int i = 0; i < delayedAudios.size(); ++i) {
		todos.push_back(delayedAudios[i]);
		// all delayed files are temporary must be removed at the end
		tempRecords.emplace_back(delayedAudios[i]);
	}

	std::vector<AudioRecord> mergedAudios;
	while (todos.size() > 1) {
		std::vector<AudioRecord> audios;
		audios.push_back(todos.front()); todos.pop_front();
		audios.push_back(todos.front()); todos.pop_front();

		char filePath[512];
		char suffix[32];
		std::sprintf(filePath, "%s", audios.front().m_FileName.c_str());
		std::sprintf(suffix, "_merged_.mp3");
		for (int i = 0; i < 512; ++i) {
			if (filePath[i] == '.') {
				memset(filePath + i, 0, sizeof(filePath) - i - 1);
				memcpy(filePath + i, suffix, sizeof(suffix));
				break;
			}
		}

		AudioRecord merged(filePath, 0, 0);
		this->OpenInputs(audios);
		this->CreateMixFilterGraph(audios);
		this->MixAudios(merged);
		this->CleanUp();

		//this->MergeInputs(audios, merged);
		todos.push_back(merged);
		mergedAudios.push_back(merged);
		// all merged files are temporary must be removed at the end
		tempRecords.emplace_back(merged);
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}


	AudioRecord emptyFile("", 0, 0);
	std::vector<AudioRecord> finalAudio;
	if (todos.size() > 0) {
		finalAudio.push_back(todos.front());
		this->OpenInputs(finalAudio);
		this->CreateMixFilterGraph(finalAudio);
		this->MixAudios(emptyFile);
	}
	this->CleanUp();

	// remove the temporary file
	for (int i = 0; i < tempRecords.size(); ++i) {
		std::remove(tempRecords[i].m_FileName.c_str());
	}
}

void AudioMixer::ParseAudioFiles(const std::string& rawdata)
{
	m_Audios.clear();
	std::regex re(",");
	std::sregex_token_iterator
		first{ rawdata.begin(), rawdata.end(), re, -1 }, last;
	std::vector<std::string> audioFiles = { first, last };
	for (int i = 0; i < audioFiles.size()-1; i+=5 ) {
		std::string name = audioFiles[i];
		int start, end, seek, isloop;
		std::sscanf(audioFiles[i + 1].c_str(), "%d", &start);
		std::sscanf(audioFiles[i + 2].c_str(), "%d", &end);
		std::sscanf(audioFiles[i + 3].c_str(), "%d", &seek);
		std::sscanf(audioFiles[i + 4].c_str(), "%d", &isloop);
		m_Audios.emplace_back(AudioRecord(name, start, end, seek, isloop));
	}
}

void AudioMixer::SetCaptureStartTime(unsigned int startTime)
{
	m_nCaptureStartTime = startTime;
}

void AudioMixer::SetCaptureEndTime(unsigned int endTime)
{
	m_nCaptureEndTime = endTime;
}

void AudioMixer::CleanUp()
{
	// close the inputs
	for (int i = 0; i < m_InputFmtCtxs.size(); ++i) {
		avcodec_close(m_InputFmtCtxs[i]->streams[0]->codec);
		avformat_close_input(&m_InputFmtCtxs[i]);
	}
	m_InputFmtCtxs.clear();
	m_InputCodecCtxs.clear();
	
	// free the filter graph
	for (int i = 0; i < m_BufferSrcCtx.size(); ++i) {
		avfilter_free(m_BufferSrcCtx[i]);
	}
	avfilter_free(m_BuffersinkCtx);
	m_BuffersinkCtx = nullptr;
	
	avfilter_graph_free(&m_FilterGraph);
}

/* just pick the highest supported samplerate */
int AudioMixer::SelectSampleRate(const AVCodec *codec)
{
	const int *p;
	int best_samplerate = 0;

	if (!codec->supported_samplerates)
		return 44100;

	p = codec->supported_samplerates;
	while (*p) {
		if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
			best_samplerate = *p;
		p++;
	}
	return best_samplerate;
}

/* check that a given sample format is supported by the encoder */
int AudioMixer::CheckSampleFmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
	const enum AVSampleFormat *p = codec->sample_fmts;

	while (*p != AV_SAMPLE_FMT_NONE) {
		if (*p == sample_fmt)return 1;	
		p++;
	}
	return 0;
}

/* select layout with the highest channel count */
int AudioMixer::SelectChannelLayout(const AVCodec *codec)
{
	const uint64_t *p;
	uint64_t best_ch_layout = 0;
	int best_nb_channels = 0;

	if (!codec->channel_layouts)return AV_CH_LAYOUT_STEREO;
		
	p = codec->channel_layouts;
	while (*p) {
		int nb_channels = av_get_channel_layout_nb_channels(*p);

		if (nb_channels > best_nb_channels) {
			best_ch_layout = *p;
			best_nb_channels = nb_channels;
		}
		p++;
	}
	return best_ch_layout;
}

void AudioMixer::MixAudios(AudioRecord& merged)
{
	int numFailedFrame = 0;
	int numSuccessFrame = 0;
	int numReceived = 0;
	int numAddToGraphFrame = 0;

	bool muxWithVideo = merged.m_FileName.empty();
	AVFormatContext* outputFormat = nullptr;
	AVCodecContext* outputCodec = nullptr;
	if (!muxWithVideo) {
		AVIOContext* ioContext = NULL;
		int ret = avio_open(&ioContext, merged.m_FileName.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			OUTPUT_LOG("Failed to open %s: %d\n", merged.m_FileName.c_str());
			return;
		}

		outputFormat = avformat_alloc_context();
		outputFormat->pb = ioContext;
		outputFormat->oformat = av_guess_format(NULL, merged.m_FileName.c_str(), NULL);

		av_strlcpy(outputFormat->filename, merged.m_FileName.c_str(), sizeof((outputFormat)->filename));

		AVCodec* codec = avcodec_find_encoder(outputFormat->oformat->audio_codec);

		AVStream *stream = avformat_new_stream(outputFormat, codec);

		outputCodec = stream->codec;
		outputCodec->channels = m_OutputAudioCodecCtx->channels;
		outputCodec->channel_layout = m_OutputAudioCodecCtx->channel_layout;
		outputCodec->sample_rate = m_OutputAudioCodecCtx->sample_rate;
		outputCodec->sample_fmt = m_OutputAudioCodecCtx->sample_fmt;
		outputCodec->bit_rate = m_OutputAudioCodecCtx->bit_rate;

		/** Allow the use of the experimental AAC encoder */
		outputCodec->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

		/** Set the sample rate for the container. */
		stream->time_base.den = outputCodec->sample_rate;
		stream->time_base.num = 1;

		/**
		* Some container formats (like MP4) require global headers to be present
		* Mark the encoder so that it behaves accordingly.
		*/
		if (outputFormat->oformat->flags & AVFMT_GLOBALHEADER)
			outputCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		/** Open the encoder for the audio stream to use it later. */
		ret = avcodec_open2(outputCodec, codec, nullptr);
		if (ret < 0) {
			OUTPUT_LOG("Failed to open the encoder for the audio stream %s .\n", merged.m_FileName.c_str());
			return;
		}

		// write output file header
		ret = avformat_write_header(outputFormat, nullptr);
		if (ret < 0) {
			OUTPUT_LOG("Failed to write header!\n");
			return;
		}
	}
	

	int64_t framePts = 0;
	/* pull filtered audio from the filtergraph */
	AVFrame* inputFrame = av_frame_alloc();
	AVFrame* filteredFrame = av_frame_alloc();
	AVPacket outPkt, inPkt;
	av_init_packet(&outPkt);
	av_init_packet(&inPkt);
	
	while (true) {
		// encode filterd frames
		int err = -1;
		do{
			err = av_buffersink_get_frame(m_BuffersinkCtx, filteredFrame);
			if (err >= 0){
				filteredFrame->pts = framePts;
				framePts += filteredFrame->nb_samples;
				if (muxWithVideo) {
					avcodec_send_frame(m_OutputAudioCodecCtx, filteredFrame);
					numReceived++;
					if (avcodec_receive_packet(m_OutputAudioCodecCtx, &outPkt) == 0) {
						outPkt.stream_index = m_OutputAudioSt->index;
						if (av_interleaved_write_frame(m_pOutputFmtCtx, &outPkt) != 0)
							OUTPUT_LOG("Error while writing audio frame");
						numSuccessFrame++;
						av_packet_unref(&outPkt);
					}
				}else {
					avcodec_send_frame(outputCodec, filteredFrame);
					numReceived++;
					if (avcodec_receive_packet(outputCodec, &outPkt) == 0) {	
						if (av_interleaved_write_frame(outputFormat, &outPkt) != 0)OUTPUT_LOG("Error while writing audio frame");
						numSuccessFrame++;
						av_packet_unref(&outPkt);
					}
				}
				
			}
			av_frame_unref(filteredFrame);
		} while (err >= 0);

		// need more input
		if (AVERROR(EAGAIN) == err){
			// find lack source inputs
			std::vector<int> lackSourceInputs;
			for (int i = 0; i < m_InputFmtCtxs.size(); ++i){
				if (av_buffersrc_get_nb_failed_requests(m_BufferSrcCtx[i]) > 0)lackSourceInputs.push_back(i);		
			}

			if (lackSourceInputs.size() == 0) break;

			// decode frames and fill into input filter
			for (int i : lackSourceInputs){
				bool finished = false;
				for (int j = 0; j < 128 && !finished; ++j){
					int rEror = -1;
					while ((rEror = av_read_frame(m_InputFmtCtxs[i], &inPkt)) >= 0) {
						if (inPkt.stream_index == 0) {// for later opt
							int ret = avcodec_send_packet(m_InputCodecCtxs[i], &inPkt);
							if (ret < 0) {
								char erros[256];
								av_make_error_string(erros, 256, ret);
								OUTPUT_LOG("Error while sending a packet to the decoder!\n");
								break;
							}
							while (ret >= 0) {
								ret = avcodec_receive_frame(m_InputCodecCtxs[i], inputFrame);
								if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
									break;
								}else if (ret < 0) {
									OUTPUT_LOG("Error while receiving a frame from the decoder!\n");
									break;// go to end
								}else {
									/* push the audio data from decoded frame into the filtergraph */
									if (av_buffersrc_add_frame_flags(m_BufferSrcCtx[i], inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
										OUTPUT_LOG("Error while feeding the audio filtergraph!\n");	
									}
									numAddToGraphFrame++;
									av_frame_unref(inputFrame);
								}
							}// while
							av_packet_unref(&inPkt);
						}
					}// end while
					if (rEror == AVERROR_EOF) finished = true;
				}
				// end input
				if (finished) av_buffersrc_add_frame(m_BufferSrcCtx[i], NULL);
			}
		}

		// flush encoder
		if ((AVERROR(ENOMEM) == err) || AVERROR_EOF == err){
			if (muxWithVideo) {
				err = avcodec_send_frame(m_OutputAudioCodecCtx, nullptr);
			}else {
				err = avcodec_send_frame(outputCodec, nullptr);
			}
			
			break;
		}

	}// end while

	av_free_packet(&inPkt);
	av_free_packet(&outPkt);
	av_frame_free(&filteredFrame);
	av_frame_free(&inputFrame);

	OUTPUT_LOG("numAddToGraphFrame, %d!\n", numAddToGraphFrame);
	OUTPUT_LOG("Failed to get audio frame number, %d!\n", numFailedFrame);
	OUTPUT_LOG("Success to get audio frame number, %d!\n", numSuccessFrame);
	OUTPUT_LOG("numReceived, %d!\n", numReceived);

	if (!muxWithVideo) {
		// write trailer
		int ret = av_write_trailer(outputFormat);
		if (ret < 0) {
			OUTPUT_LOG("Failed to write trailer!\n");
		}
		avio_close(outputFormat->pb);
	}else {
		// Don't forget to close the audio encoder
		avcodec_close(m_OutputAudioSt->codec);
	}
	
}

int AudioMixer::OpenAudioInput(const char *filename, AVFormatContext*& fmt_ctx, AVCodecContext*& dec_ctx)
{
	int ret = -1;
	// Open the input file to read from it.
	if (ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
		OUTPUT_LOG("Could not open input file '%s' (error '%s'), m_Audios[i].m_strFileName.c_str()\n");
		return ret;
	}

	// Get information on the input file (number of streams etc.).
	if (ret = avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		OUTPUT_LOG("Could not open find stream info (error '%s')\n");
		avformat_close_input(&fmt_ctx);
		return ret;
	}

	// Find a decoder for the audio stream.
	AVCodec* codec = avcodec_find_decoder(fmt_ctx->streams[0]->codec->codec_id);
	if (!codec) {
		OUTPUT_LOG("Could not find input codec!\n");
		avformat_close_input(&fmt_ctx);
		return -1;
	}

	/** Open the decoder for the audio stream to use it later. */
	if (ret = avcodec_open2(fmt_ctx->streams[0]->codec, codec, NULL) < 0) {
		OUTPUT_LOG("Could not open input codec!\n");
		avformat_close_input(&fmt_ctx);
		return ret;// NOTE: what if if it failed?
	}

	// Save the decoder context for easier access later. 
	dec_ctx = fmt_ctx->streams[0]->codec;

	if (dec_ctx->channel_layout == 0) {
		dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
	}

	return ret;
}

void AudioMixer::CollectInputsInfo()
{
	AudioFiles filteredFiles;
	OUTPUT_LOG("Total input files: %d \n", m_Audios.size());
	// open all the input 
	for (int i = 0; i < m_Audios.size(); ++i) {
		AVFormatContext* pInputFormatContext = nullptr;
		// Open the input file to read from it.

		int ret = avformat_open_input(&pInputFormatContext, m_Audios[i].m_FileName.c_str(), NULL, NULL);
		if (ret < 0) {
			OUTPUT_LOG("Could not open input file %s \n", m_Audios[i].m_FileName.c_str());
			continue;// NOTE: what if if it failed?
		}
		OUTPUT_LOG("Open input file %s, startFrame: %d  \n", m_Audios[i].m_FileName.c_str(), m_Audios[i].m_nStartTime);

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

		// quick method
		long long duration = 0;
		AVStream* stream = pInputFormatContext->streams[0];
		m_Audios[i].m_Duration = (float)(stream->duration * stream->time_base.num) / (float)stream->time_base.den;

		// Don't forget to close the audio encoder
		avcodec_close(pInputFormatContext->streams[0]->codec);
		avformat_close_input(&pInputFormatContext);
		filteredFiles.push_back(m_Audios[i]);
	}// end for
	OUTPUT_LOG("Total opened input files: %d \n", filteredFiles.size());
	m_Audios = filteredFiles;
}

void AudioMixer::OpenInputs(const std::vector<AudioRecord>& inputs)
{
	m_InputFmtCtxs.clear();
	m_InputCodecCtxs.clear();
	m_InputFmtCtxs.resize(inputs.size());
	m_InputCodecCtxs.resize(inputs.size());
	for (int i = 0; i < inputs.size(); ++i) {
		this->OpenAudioInput(inputs[i].m_FileName.c_str(), m_InputFmtCtxs[i], m_InputCodecCtxs[i]);
	}// end for
}

std::string AudioMixer::ClipAudio(AudioRecord audio)
{
	AVCodecContext* inputCodecCtx = nullptr;
	AVFormatContext* inputFmtCtx = nullptr;
	std::string trimmedFile = audio.m_FileName;
	this->OpenAudioInput(audio.m_FileName.c_str(), inputFmtCtx, inputCodecCtx);
	if (inputFmtCtx == nullptr || audio.m_nStartTime < m_nCaptureStartTime) {
		goto end;
	}

#pragma region CREATE FILTER GRAPH
	// create atrim filter graph 
	AVFilterGraph* graph = avfilter_graph_alloc();
	if (graph == nullptr) {
		OUTPUT_LOG("Failed to create the atrim filter graph!\n");
		goto end;
	}

	// creat atrim filter 
	AVFilterContext* trim = avfilter_graph_alloc_filter(graph, avfilter_get_by_name("atrim"), nullptr);
	if (trim == nullptr) {
		OUTPUT_LOG("Could not find the atrim filter!\n");
		goto end;
	}

	// set atrim options
	int64_t start = 0;
	int64_t end = -1;
	start = (audio.m_nSeekPos > 0 ? audio.m_nSeekPos : 0.0);
	if (audio.m_nEndTime > 0) {
		end = (audio.m_nEndTime - audio.m_nStartTime) / 1000.0 + 1;
	}
	start *= inputCodecCtx->sample_rate;
	end *= inputCodecCtx->sample_rate;

	char options[512];
	// at least one of start and end is bigger than zero
	if (start > 0 && end > start) {
		snprintf(options, sizeof(options), "start_sample=%lld:end_sample=%lld", start, end);
	}else if (start > 0) {
		snprintf(options, sizeof(options), "start_sample=%lld", start);
	}else if (end > 0) {
		snprintf(options, sizeof(options), "end_sample=%lld", end);
	}else {
		OUTPUT_LOG("Invalid interval!\n");
		goto end;
	}	
	int ret = avfilter_init_str(trim, options);

	// Create the abuffer filter to buffer audio frames, and make them available to the filter chain.
	const AVFilter* buffer = avfilter_get_by_name("abuffer");
	if (buffer == nullptr) {
		OUTPUT_LOG("Could not find the abuffer filter!\n");
		goto end;
	}
	char name[16], args[512];
	snprintf(name, sizeof(name), "buffer");
	snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%llu",
		inputCodecCtx->time_base.num,
		inputCodecCtx->time_base.den,
		inputCodecCtx->sample_rate,
		av_get_sample_fmt_name(inputCodecCtx->sample_fmt),
		inputCodecCtx->channel_layout);
	AVFilterContext* abufferCtx = nullptr;
	ret = avfilter_graph_create_filter(&abufferCtx, buffer, name, args, nullptr, graph);
	if (ret < 0) {
		OUTPUT_LOG("Could not create the abuffer filter context!\n");
		goto end;
	}
	// link to the trim filter
	ret = avfilter_link(abufferCtx, 0, trim, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not link the abuffer filter to atrim filter!\n");
		goto end;
	}

	// Finally create the abuffersink filter to buffer audio frames, and make them available to the end of filter chain.
	const AVFilter* sinkbuffer = avfilter_get_by_name("abuffersink");
	if (!sinkbuffer) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		goto end;
	}
	AVFilterContext* sinkbufferCtx = avfilter_graph_alloc_filter(graph, sinkbuffer, "sink");
	if (!sinkbuffer) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		goto end;
	}
	ret = avfilter_link(trim, 0, sinkbufferCtx, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not link the abuffer filter to adelay filter!\n");
		goto end;
	}
	if (inputCodecCtx->frame_size > 0)
		av_buffersink_set_frame_size(sinkbufferCtx, inputCodecCtx->frame_size);

	// Configure the delay filter  graph. 
	ret = avfilter_graph_config(graph, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Error while configuring delay filter graph!\n");
		goto end;
	}
#pragma endregion

#pragma region SETUP OUTPUT

	// set output file name
	size_t pos = trimmedFile.find_last_of(".");
	trimmedFile.resize(pos+1);
	trimmedFile += "mp3";
	trimmedFile.insert(pos, "_trimmed_");

	struct FileExist
	{
		bool operator()(const std::string& fileName)
		{
			std::ifstream ifile(fileName);
			return (bool)ifile;
		}
	};
	FileExist fileExist;

	while (fileExist(trimmedFile)) {
		size_t pos = trimmedFile.find_last_of(".");
		trimmedFile.insert(pos, "_trimmed_");
	}

	AVIOContext* ioContext = NULL;
	ret = avio_open(&ioContext, trimmedFile.c_str(), AVIO_FLAG_WRITE);
	if (ret < 0) {
		OUTPUT_LOG("Failed to open %s !\n", trimmedFile.c_str());
		goto end;
	}

	AVFormatContext* outputFormat = avformat_alloc_context();
	outputFormat->pb = ioContext;
	outputFormat->oformat = av_guess_format(nullptr, trimmedFile.c_str(), nullptr);
	av_strlcpy(outputFormat->filename, trimmedFile.c_str(), sizeof((outputFormat)->filename));
	AVCodec* codec = avcodec_find_encoder(outputFormat->oformat->audio_codec);
	AVStream* stream = avformat_new_stream(outputFormat, codec);

	AVCodecContext* outputCodecCtx = stream->codec; 
	outputCodecCtx->channels = inputCodecCtx->channels;
	outputCodecCtx->channel_layout = inputCodecCtx->channel_layout;
	outputCodecCtx->sample_rate = inputCodecCtx->sample_rate;
	outputCodecCtx->sample_fmt = inputCodecCtx->sample_fmt;
	outputCodecCtx->bit_rate = inputCodecCtx->bit_rate;

	/** Allow the use of the experimental AAC encoder */
	//outputCodecCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

	/** Set the sample rate for the container. */
	stream->time_base.den = outputCodecCtx->sample_rate;
	stream->time_base.num = 1;

	/**
	* Some container formats (like MP4) require global headers to be present
	* Mark the encoder so that it behaves accordingly.
	*/
	if (outputFormat->oformat->flags & AVFMT_GLOBALHEADER)
		outputCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	/** Open the encoder for the audio stream to use it later. */
	ret = avcodec_open2(outputCodecCtx, codec, nullptr);
	if (ret < 0) {
		OUTPUT_LOG("Failed to open the encoder for the audio stream %s !\n", trimmedFile.c_str());
		goto end;
	}

	// write output file header
	ret = avformat_write_header(outputFormat, nullptr);
	if (ret < 0) {
		OUTPUT_LOG("Failed to write header!\n");
		goto end;
	}
#pragma endregion

#pragma region FILTER THE FRAMES
	/* pull filtered audio from the filtergraph */
	AVFrame* inputFrame = av_frame_alloc();
	AVFrame* filteredFrame = av_frame_alloc();
	AVPacket outPkt, inPkt;
	av_init_packet(&outPkt);
	av_init_packet(&inPkt);
	int64_t framePts = 0;

	while (true) {
		// encode filterd frames
		int err = -1;
		do {
			err = av_buffersink_get_frame(sinkbufferCtx, filteredFrame);
			if (err >= 0) {
				filteredFrame->pts = framePts;
				framePts += filteredFrame->nb_samples;
				avcodec_send_frame(outputCodecCtx, filteredFrame);
				if (avcodec_receive_packet(outputCodecCtx, &outPkt) == 0) {
					if (av_interleaved_write_frame(outputFormat, &outPkt) != 0) {
						OUTPUT_LOG("Error while writing audio frame");
					}
					av_packet_unref(&outPkt);
				}
			}
			av_frame_unref(filteredFrame);
		} while (err >= 0);

		// need more input
		if (AVERROR(EAGAIN) == err) {
			if (av_buffersrc_get_nb_failed_requests(abufferCtx) > 0) {
				// decode frames and fill into input filter
				bool finished = false;
				for (int j = 0; j < 128 && !finished; ++j) {
					int rEror = -1;
					while ((rEror = av_read_frame(inputFmtCtx, &inPkt)) >= 0) {
						if (inPkt.stream_index == 0) {// for later opt
							int ret = avcodec_send_packet(inputCodecCtx, &inPkt);
							if (ret < 0) {
								OUTPUT_LOG("Error while sending a packet to the decoder!\n");
								break;
							}
							while (ret >= 0) {
								ret = avcodec_receive_frame(inputCodecCtx, inputFrame);
								if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
									break;
								}
								else if (ret < 0) {
									OUTPUT_LOG("Error while receiving a frame from the decoder!\n");
									break;// go to end
								}
								else {
									/* push the audio data from decoded frame into the filtergraph */
									if (av_buffersrc_add_frame_flags(abufferCtx, inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
										OUTPUT_LOG("Error while feeding the audio filtergraph!\n");
									}
									av_frame_unref(inputFrame);
								}
							}// while
							av_packet_unref(&inPkt);
						}
					}// end while
					if (rEror == AVERROR_EOF) finished = true;
				}
				// end input
				if (finished) av_buffersrc_add_frame(abufferCtx, nullptr);
			}
		}

		// flush encoder
		if ((AVERROR(ENOMEM) == err) || AVERROR_EOF == err) {
			err = avcodec_send_frame(outputCodecCtx, nullptr);
			break;
		}

	}// end while

	av_free_packet(&inPkt);
	av_free_packet(&outPkt);
	av_frame_free(&filteredFrame);
	av_frame_free(&inputFrame);
#pragma endregion

	// write trailer
	ret = av_write_trailer(outputFormat);
	if (ret < 0) {
		OUTPUT_LOG("Failed to write trailer!\n");
	}
	avio_close(outputFormat->pb);
	avcodec_close(inputFmtCtx->streams[0]->codec);

	avfilter_free(abufferCtx);
	avfilter_free(sinkbufferCtx);
	avformat_close_input(&inputFmtCtx);

	avfilter_graph_free(&graph);

	end:
	   return trimmedFile;
}

