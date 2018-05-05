#include"AudioMixer.h"
#include"stdafx.h"

#include <stdio.h>

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

AudioMixer::AudioMixer(AVFormatContext* fmtCtx, ParaEngine::MovieCodec* movieCodec)
	: m_pOutputFormatContext(fmtCtx)
	, m_pOutputFormat(fmtCtx->oformat)
	, m_pSWRctx(nullptr)
	, m_pAudioStream(nullptr)
	, m_pBuffersinkFilterContext(nullptr)
	, _movieCodec( movieCodec)
	, m_pAudioCoder(nullptr)
{
	m_Audios.clear();
	InitAudioStream();
}

AudioMixer::~AudioMixer()
{
	avcodec_close(m_pAudioStream->codec);
}

void AudioMixer::AddAudioFile( AudioFile& audio)
{
	m_Audios.emplace_back(audio);
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
	// Create a new filtergraph, which will contain all the filters. 
	AVFilterGraph* filterGraph = avfilter_graph_alloc();
	if (!filterGraph) {
		OUTPUT_LOG("Failed to initialize the filter graph!\n");
		return ;
	}

	// Create the abuffer filter to buffer audio frames, and make them available to the filter chain.
	
	m_pBufferFilterContexts.resize(chanels);
	m_pDelayFilterContexts.resize(chanels);
	for (int i = 0; i < chanels; ++i) {
		const AVFilter* buffer = avfilter_get_by_name("abuffer");
		if (!buffer) {
			OUTPUT_LOG("Could not find the abuffer filter!\n");
			return ;// note: mem leak
		}
		// buffer audio source: the decoded and converted frames from the decoder and decoder will be inserted here.
		if (!m_pAudioEncoderContext->channel_layout)
			m_pInputCodecContexts[i]->channel_layout = av_get_default_channel_layout(m_pInputCodecContexts[i]->channels);
		char name[8] = {'s','r','c'};
		itoa(i, &name[3], 10);
		char args[512];
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
		std::string argsstr;
		char digits[16];
		memset(digits, 0, 16);
		itoa(i*100000, digits, 10);
		argsstr.append(digits);
		argsstr += "|";
		argsstr.append(digits);

		// Remove the last "|" symbol.
		argsstr.resize(argsstr.size() - 1);
		avfilter_graph_create_filter(&m_pDelayFilterContexts[i], pDelayFilter, "adelay",
			argsstr.c_str(), NULL, filterGraph);
	}

	// Create mix filter.
	const AVFilter* pMixFilter = avfilter_get_by_name("amix");
	if (!pMixFilter) {
		OUTPUT_LOG("Could not find the mix filter!\n");
		return;// note: mem leak
	}
	AVFilterContext* pMixFilterContext;
	std::string args("inputs=");
	char digits[8];
	itoa(chanels, digits, 8);
	args += digits;
	avfilter_graph_create_filter(&pMixFilterContext, pMixFilter, "amix",
		args.c_str(), NULL, filterGraph);

	

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

	// Same sample fmts as the output file. */
	AVSampleFormat FMT[2] = { m_pAudioCoder->sample_fmts[0], AV_SAMPLE_FMT_NONE };
	int ret = av_opt_set_int_list(m_pBuffersinkFilterContext,
		"sample_fmts",
		FMT,
		AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		OUTPUT_LOG("Could set options to the abuffersink instance!\n");
		return;// note: mem leak
	}

	char ch_layout[64];
	av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, m_Audios.size());
	av_opt_set(m_pBuffersinkFilterContext, "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);

	ret = avfilter_init_str(m_pBuffersinkFilterContext, NULL);
	if (ret < 0) {
		OUTPUT_LOG("Could not initialize the abuffersink instance!\n");
		return;// note: mem leak
	}

	// Connect the filters.
	for (int i = 0; i < m_pBufferFilterContexts.size(); ++i) {
		ret = avfilter_link(m_pBufferFilterContexts[i], 0, m_pDelayFilterContexts[i], 0);
		if (ret < 0) {
			OUTPUT_LOG("Could not link the buffer filter to mix filter!\n");
			return;// note: mem leak
		}
		ret = avfilter_link(m_pDelayFilterContexts[i], 0, pMixFilterContext, i);
		if (ret < 0) {
			OUTPUT_LOG("Could not link the buffer filter to mix filter!\n");
			return;// note: mem leak
		}
	}

	ret = avfilter_link(pMixFilterContext, 0, m_pBuffersinkFilterContext, 0);
	if (ret < 0) {
		OUTPUT_LOG("Could not link the mix filter to buffersink filter!\n");
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
	m_pInputFormatContexts.resize(m_Audios.size());
	m_pInputCodecContexts.resize(m_Audios.size());
	// open all the input 
	for (int i = 0; i < m_Audios.size(); ++i) {
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
						//ReadAndDecodeAudioFrame(frame, m_pInputFormatContexts[i], m_pInputCodecContexts[i], hasData, finished);
#pragma region	// Convert

						uint8_t *convertedData = NULL;

						if (av_samples_alloc(&convertedData,
							NULL,
							m_pAudioEncoderContext->channels,
							audioFrameConverted->nb_samples,
							m_pAudioEncoderContext->sample_fmt, 0) < 0)
							OUTPUT_LOG("Could not allocate samples");

						int outSamples = swr_convert(m_pSWRctx,
							&convertedData,
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
							convertedData,
							buffer_size,
							0) < 0)
							OUTPUT_LOG("Could not fill frame");
#pragma endregion

							// If there is decoded data, convert and store it. /
							// resample it 
							// Push the audio data from decoded frame into the filtergraph./
						int ret = av_buffersrc_write_frame(m_pBufferFilterContexts[i], audioFrameConverted);

					}
				}
			}

		}

	av_frame_free(&audioFrameConverted);
	av_frame_free(&audioFrameDecoded);
	av_free_packet(&inPacket);

#ifdef ORGINAL
	int hasData = 0;
	bool finished = false;
	AVFrame *frame = av_frame_alloc();
	AVFrame *filtFrame = av_frame_alloc();

	while (0/*!finished*/) {
		ReadAndDecodeAudioFrame(frame, m_pInputFormatContexts[i], m_pInputCodecContexts[i], hasData, finished);
		//	if (finished && !hasData) {
		//		int ret = av_buffersrc_write_frame(m_pBufferFilterContexts[i], NULL);
		//		if (ret < 0) OUTPUT_LOG("Error writing EOF null frame for input %d\n", i);
		//	}
		//	else if (hasData) {
		//		// If there is decoded data, convert and store it. /
		//		// resample it 
		//	
		//		// Push the audio data from decoded frame into the filtergraph./
		//		int ret = av_buffersrc_write_frame(m_pBufferFilterContexts[i], frame);
		//		if (ret < 0) OUTPUT_LOG("Error while feeding the audio filtergraph\n");
		//	}
		//	
		//	// Pull filtered audio frames from the filtergraph 
		//	
		//	while (true) {
		//		int ret = av_buffersink_get_frame(m_pBuffersinkFilterContext, filtFrame);
		//		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
		//			break;
		//		}
		//		ret = EncodeAndWriteAudioFrame(filtFrame, m_pOutputFormatContext, m_pAudioEncoderContext);
		//		av_frame_unref(filtFrame);
		//		if (ret < 0) {
		//			continue;
		//		}
		//		count++;
		//	}// end while

			//int dst_nb_samples = av_rescale_rnd(swr_get_delay(m_pSWRctx, frame->sample_rate) 
			//	+ frame->nb_samples, frame->sample_rate, frame->sample_rate, AV_ROUND_UP);
			//if (m_max_dst_nb_samples < dst_nb_samples) {
			//	if(audioBuf)av_free(audioBuf[0]);
			//	int ret = av_samples_alloc_array_and_samples(&audioBuf, &m_dst_samples_linesize,
			//		m_pAudioEncoderContext->channels,
			//		dst_nb_samples, m_pAudioEncoderContext->sample_fmt, 0);
			//	if (ret < 0)
			//		return;// note
			//	m_max_dst_nb_samples = dst_nb_samples;
			//}
			//int nb = swr_convert(m_pSWRctx, audioBuf, dst_nb_samples, 
			//	(const uint8_t**)frame->data, frame->nb_samples);
			//if (nb < 0) {
			//	OUTPUT_LOG("Error while converting\n");
			//	return ;
			//}
			//static int m_samples_count = 0;
			//frame->nb_samples = dst_nb_samples;
			//AVRational avr = { 1, m_pAudioEncoderContext->sample_rate };
			//frame->pts = av_rescale_q(m_samples_count, avr, m_pAudioEncoderContext->time_base);
			//m_samples_count += dst_nb_samples;

			//int m_dst_samples_size = av_samples_get_buffer_size(NULL, m_pAudioEncoderContext->channels, dst_nb_samples,
			//	m_pAudioEncoderContext->sample_fmt, 0);
			//int ret = avcodec_fill_audio_frame(frame, m_pAudioEncoderContext->channels, m_pAudioEncoderContext->sample_fmt,
			//	*audioBuf, m_dst_samples_size, 0);


			//_movieCodec->encode_audio_frame_data(frame->data[0], frame->linesize[0]);


			//EncodeAndWriteAudioFrame(frame, m_pOutputFormatContext, m_pAudioEncoderContext);
		av_frame_unref(frame);
	}// end while
	av_frame_free(&frame);
	av_frame_free(&filtFrame);
#endif
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
	while (true) {
		int ret = av_buffersink_get_frame(m_pBuffersinkFilterContext, audioFrameFiltered);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		}


		if (ret < 0) {
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

			av_free_packet(&outPacket);
		}
		av_frame_unref(audioFrameFiltered);

	}// end while
	}


	// Don't forget to close the audio encoder
	avcodec_close(m_pAudioStream->codec); 
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
