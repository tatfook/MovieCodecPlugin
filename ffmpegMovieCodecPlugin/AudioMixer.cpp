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
	, m_pSWRctx(nullptr)
	, m_OutputAudioSt(nullptr)
	, m_BuffersinkCtx(nullptr)
	, _movieCodec( movieCodec)
	, m_OutputAudioCodec(nullptr)
	, m_pConvertedDataBuffer(nullptr)
{
	m_Audios.clear();
	InitAudioStream();
}

AudioMixer::~AudioMixer()
{
	CleanUp();
}

bool AudioMixer::Mix()
{
	ProcessInputAudios();
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
	av_opt_set_channel_layout(m_pSWRctx, "out_channel_layout", m_OutputAudioCodecCtx->channel_layout, 0);

	av_opt_set_int(m_pSWRctx, "in_channel_count", pInCedecContext->channels, 0);
	av_opt_set_int(m_pSWRctx, "out_channel_count", m_OutputAudioCodecCtx->channels, 0);

	av_opt_set_int(m_pSWRctx, "in_sample_rate", pInCedecContext->sample_rate, 0);
	av_opt_set_int(m_pSWRctx, "out_sample_rate", m_OutputAudioCodecCtx->sample_rate, 0);

	av_opt_set_sample_fmt(m_pSWRctx, "in_sample_fmt", pInCedecContext->sample_fmt, 0);
	av_opt_set_sample_fmt(m_pSWRctx, "out_sample_fmt", m_OutputAudioCodecCtx->sample_fmt, 0);
	
	
	
	/* initialize the resampling context */
	if ((swr_init(m_pSWRctx)) < 0) {
		OUTPUT_LOG("Failed to initialize the resampling context!\n");
		return;
	}
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
	m_OutputAudioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;//m_OutputAudioCodec->sample_fmts[0];
	m_OutputAudioCodecCtx->bit_rate = 64000;// for test
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

void AudioMixer::InitFilerGraph(int chanels)
{
	if (chanels < 1) return;
	// Create a new filtergraph, which will contain all the filters. 
	AVFilterGraph* graph = avfilter_graph_alloc();
	if (!graph) {
		OUTPUT_LOG("Failed to initialize the filter graph!\n");
		return ;
	}

	// build filter graph description string
	char* labels[32] = {
		"[a]","[b]","[c]","[d]",
		"[e]","[f]","[g]","[h]",
		"[i]","[j]","[k]","[l]",
		"[m]","[n]","[o]","[p]",
		"[q]","[r]","[s]","[t]",
		"[u]","[v]","[w]","[x]",
		"[y]","[z]","[aa]","[bb]",
		"[cc]","[dd]","[ee]","[ff]"
	};
	std::string branches;
	for (int i = 0; i < m_Audios.size(); ++i) {
		int timeLapse = m_Audios[i].m_nStartFrameNum - m_nCaptureStartTime;
		if (timeLapse < 0) timeLapse = 0;
		char chain[128];
		memset(chain, 0, sizeof(chain));
		sprintf(chain, "[%d]adelay=%d|%d,aformat=sample_fmts=%s:sample_rates=%d:channel_layouts=%llu%s;",//apad = whole_len = 11000,
			i, 
			timeLapse,
			timeLapse,
			av_get_sample_fmt_name(m_OutputAudioCodecCtx->sample_fmt),
			m_OutputAudioCodecCtx->sample_rate,
			m_OutputAudioCodecCtx->channel_layout,
			labels[i]);
		branches += chain;
	}
	
	std::string mixConmand;
	mixConmand += branches;
	for (int i = 0; i < m_Audios.size(); ++i) {
		mixConmand += labels[i];
	}
	mixConmand += "amix=inputs=%d,aformat=sample_fmts=%s:sample_rates=%d:channel_layouts=%llu";
	char filter_descr[2048];
	sprintf(filter_descr,  
		mixConmand.c_str(),
		chanels,
		av_get_sample_fmt_name(m_OutputAudioCodecCtx->sample_fmt),
		m_OutputAudioCodecCtx->sample_rate,
		m_OutputAudioCodecCtx->channel_layout 
	);

	AVFilterInOut* inputs = nullptr;
	AVFilterInOut* outputs = nullptr;
	int ret = avfilter_graph_parse2(graph, filter_descr, &inputs, &outputs);

	if (ret < 0) {
		OUTPUT_LOG(filter_descr);
		OUTPUT_LOG("Failed to parse filter description!");
		return;
	}

	AVFilterInOut* currentInput = inputs;
	m_BufferSrcCtx.resize(chanels);
	for (int i = 0; i < chanels; ++i) {
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
		snprintf(name, sizeof(name), "src%d", i);
		snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%llu",
			m_InputCodecCtxs[i]->time_base.num,
			m_InputCodecCtxs[i]->time_base.den,
			m_InputCodecCtxs[i]->sample_rate,
			av_get_sample_fmt_name(m_InputCodecCtxs[i]->sample_fmt),
			m_InputCodecCtxs[i]->channel_layout);
		int ret = avfilter_graph_create_filter(&m_BufferSrcCtx[i], buffer, name, args, NULL, graph);
		if (ret < 0) {
			return;// note: mem leak
		}
		// link to the inputs
		ret = avfilter_link(m_BufferSrcCtx[i], 0, currentInput->filter_ctx, currentInput->pad_idx);
		currentInput = currentInput->next;
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
	m_BuffersinkCtx = avfilter_graph_alloc_filter(graph, pBuffersinkFilter, "sink");
	if (!pBuffersinkFilter) {
		OUTPUT_LOG("Could not find the abuffersink filter!\n");
		return;// note: mem leak
	}
	ret = avfilter_link(outputs->filter_ctx, outputs->pad_idx, m_BuffersinkCtx, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not link the abuffer filter to adelay filter!\n");
		return;// note: mem leak
	}
	if (m_OutputAudioCodecCtx->frame_size > 0)
		av_buffersink_set_frame_size(m_BuffersinkCtx, m_OutputAudioCodecCtx->frame_size);

	// Configure the graph. 
	ret = avfilter_graph_config(graph, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Error while configuring graph!\n"); 
		return;// note: mem leak
	}
}

void AudioMixer::ProcessInputAudios()
{
	this->CollectInputsInfo();
	if (m_Audios.empty()) return;

	if (0) {
		// test call for ffmpeg.exe
		// test start
		std::string inputCommand;
		for (int i = 0; i < m_Audios.size(); ++i) {
			inputCommand += " -i ";
			inputCommand += m_Audios[i].m_strFileName;
		}
		OUTPUT_LOG("Input:%s\n", inputCommand.c_str());

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

		std::string mixConmand(" ");
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
		char commandSet[1024 * 16];
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
		for (int i = 0; i < m_Audios.size(); ++i) {
			std::string fileName = m_Audios[i].m_strFileName;
			std::string::iterator iter = std::find(fileName.begin(), fileName.end(), '.');
			int suffixSize = fileName.end() - iter;
			std::string suffix = fileName.substr(fileName.size() - suffixSize, suffixSize);

			std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
			if (suffix.compare(".mp3") != 0) {
				std::string input(" -i ");
				input += fileName;
				fileName.resize(fileName.size() - suffixSize);
				std::string output = fileName + ".mp3";
				if (!std::experimental::filesystem::exists(output)) {
					std::string commands("ffmpeg.exe ");
					commands += (input + " -f mp3 " + output);
					char commandSet[512];
					snprintf(commandSet, sizeof(commandSet), "%s", commands.c_str());

					OUTPUT_LOG("Call ffmpeg.exe for converting audio files to mp3 format!\n");
					STARTUPINFO si;
					PROCESS_INFORMATION pi;
					ZeroMemory(&si, sizeof(si));
					si.cb = sizeof(si);
					ZeroMemory(&pi, sizeof(pi));
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
					snprintf(filePath, sizeof(commandSet), "%s", m_Audios[i].m_strFileName.c_str());
				}

				m_Audios[i].m_strFileName.resize(m_Audios[i].m_strFileName.size() - suffixSize);
				m_Audios[i].m_strFileName += ".mp3";
			}
		}
	}
	
	this->OpenInputs();
	this->InitFilerGraph(m_Audios.size());
	this->MixAudios();
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
	// free the graph
	//avfilter_graph_free(&m_FilterGraph);
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
		if (*p == sample_fmt)
			return 1;
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

	if (!codec->channel_layouts)
		return AV_CH_LAYOUT_STEREO;

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

void AudioMixer::MixAudios()
{
	int numFailedFrame = 0;
	int numSuccessFrame = 0;
	int numReceived = 0;
	int numAddToGraphFrame = 0;
	
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
				avcodec_send_frame(m_OutputAudioCodecCtx, filteredFrame);
				numReceived++;
				if (avcodec_receive_packet(m_OutputAudioCodecCtx, &outPkt) == 0) {
					outPkt.stream_index = m_OutputAudioSt->index;
					if (av_interleaved_write_frame(m_pOutputFmtCtx, &outPkt) != 0)
						OUTPUT_LOG("Error while writing audio frame");
					numSuccessFrame++;
					av_packet_unref(&outPkt);
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
			err = avcodec_send_frame(m_OutputAudioCodecCtx, nullptr);
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

	// Don't forget to close the audio encoder
	avcodec_close(m_OutputAudioSt->codec);
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
		// Don't forget to close the audio encoder
		avcodec_close(pInputFormatContext->streams[0]->codec);
		avformat_close_input(&pInputFormatContext);
		filteredFiles.push_back(m_Audios[i]);
	}// end for
	OUTPUT_LOG("Total opened input files: %d \n", filteredFiles.size());
	m_Audios = filteredFiles;
}

void AudioMixer::OpenInputs()
{
	m_InputFmtCtxs.resize(m_Audios.size());
	m_InputCodecCtxs.resize(m_Audios.size());
	for (int i = 0; i < m_Audios.size(); ++i) {
		this->OpenAudioInput(m_Audios[i].m_strFileName.c_str(), m_InputFmtCtxs[i], m_InputCodecCtxs[i]);
	}// end for
}

