#include <windows.h>
#include "bink.h"

// ======================================
// THIS STUFF IS NECESSARY TO LINK FFMPEG
// ======================================
//typedef unsigned long NTSTATUS;
//typedef struct BCRYPT_ALG_HANDLE { unsigned char dunno; } BCRYPT_ALG_HANDLE;
#define STATUS_SUCCESS			0

extern "C" NTSTATUS WINAPI BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE * phAlgorithm, LPCWSTR pszAlgId, LPCWSTR pszImplementation, ULONG dwFlags)
{
	UNREFERENCED_PARAMETER(phAlgorithm);
	UNREFERENCED_PARAMETER(pszAlgId);
	UNREFERENCED_PARAMETER(pszImplementation);
	UNREFERENCED_PARAMETER(dwFlags);

	return STATUS_SUCCESS;
}
extern "C" NTSTATUS WINAPI BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, ULONG dwFlags)
{
	UNREFERENCED_PARAMETER(hAlgorithm);
	UNREFERENCED_PARAMETER(dwFlags);

	return STATUS_SUCCESS;
}

extern "C" NTSTATUS WINAPI BCryptGenRandom(BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags)
{
	UNREFERENCED_PARAMETER(hAlgorithm);
	UNREFERENCED_PARAMETER(pbBuffer);
	UNREFERENCED_PARAMETER(cbBuffer);
	UNREFERENCED_PARAMETER(dwFlags);

	return STATUS_SUCCESS;
}

int FFmpeg::open(const char* name, int Lang_id)
{
	if (avformat_open_input(&ctx, name, nullptr, nullptr) < 0) return 0;
	if (avformat_find_stream_info(ctx, nullptr) < 0) return 0;

	// find video stream
	video_stream = -1;
	for (unsigned int i = 0; i < ctx->nb_streams; i++)
		if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			video_stream = i;
	// find audio stream(s)
	for (unsigned int i = 0; i < ctx->nb_streams; i++)
		if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			audio_streams.push_back(i);
	// detect which audio stream to use
	audio_stream = lang_to_astream(Lang_id);
	// these are the streams we're gonna push from now on
	if (audio_stream >= 0) astream = ctx->streams[audio_stream];
	if (video_stream >= 0) vstream = ctx->streams[video_stream];
	// gather codecs for the streams
	vcodec = avcodec_find_decoder(vstream->codecpar->codec_id);
	acodec = avcodec_find_decoder(astream->codecpar->codec_id);

	vctx = avcodec_alloc_context3(vcodec);
	actx = avcodec_alloc_context3(acodec);
	avcodec_parameters_to_context(vctx, vstream->codecpar);
	avcodec_parameters_to_context(actx, astream->codecpar);

	aframe = av_frame_alloc();
	vframe = av_frame_alloc();
	vframeRGB = av_frame_alloc();

	// image conversion
	sws = sws_getContext(vctx->width, vctx->height, vctx->pix_fmt,
		vctx->width, vctx->height, AV_PIX_FMT_BGRA, SWS_POINT, NULL, NULL, NULL);
	// sound conversion
	swr = swr_alloc();
	swr_alloc_set_opts(swr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, BINK_AUDIO_RATE,
		actx->channel_layout, actx->sample_fmt, actx->sample_rate, 0, nullptr);
	swr_init(swr);

	vnumBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, vctx->width, vctx->height, 1);
	vbuffer = (uint8_t*)av_malloc(vnumBytes * sizeof(uint8_t));
	av_image_fill_arrays(vframeRGB->data, vframeRGB->linesize, vbuffer, AV_PIX_FMT_BGRA, vctx->width, vctx->height, 1);

	if (avcodec_open2(actx, acodec, nullptr) < 0) return 0;
	if (avcodec_open2(vctx, vcodec, nullptr) < 0) return 0;

	return 1;
}

int FFmpeg::open_flc(const char* name)
{
	if (avformat_open_input(&ctx, name, nullptr, nullptr) < 0) return 0;
	if (avformat_find_stream_info(ctx, nullptr) < 0) return 0;

	// find video stream
	video_stream = -1;
	for (unsigned int i = 0; i < ctx->nb_streams; i++)
		if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			video_stream = i;
	// these are the streams we're gonna push from now on
	if (video_stream >= 0) vstream = ctx->streams[video_stream];
	// gather codecs for the streams
	vcodec = avcodec_find_decoder(vstream->codecpar->codec_id);

	vctx = avcodec_alloc_context3(vcodec);
	avcodec_parameters_to_context(vctx, vstream->codecpar);

	vframe = av_frame_alloc();
	vframeRGB = av_frame_alloc();

	// image conversion
	sws = sws_getContext(vctx->width, vctx->height, vctx->pix_fmt,
		vctx->width, vctx->height, AV_PIX_FMT_BGRA, SWS_POINT, NULL, NULL, NULL);

	vnumBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, vctx->width, vctx->height, 1);
	vbuffer = (uint8_t*)av_malloc(vnumBytes * sizeof(uint8_t));
	av_image_fill_arrays(vframeRGB->data, vframeRGB->linesize, vbuffer, AV_PIX_FMT_BGRA, vctx->width, vctx->height, 1);

	if (avcodec_open2(vctx, vcodec, nullptr) < 0) return 0;

	return 1;
}

int FFmpeg::decode_video(AVCodecContext* avctx, AVFrame* frame, AVPacket* pkt)
{
	int ret;
again:
	ret = avcodec_send_packet(avctx, pkt);
	if (ret < 0)
		return 0;
	do
	{
		ret = avcodec_receive_frame(avctx, frame);
		if (ret == AVERROR(EAGAIN))
			goto again;
		if (ret == AVERROR_EOF)
			return 0;
	} while (ret != 0);

	return 1;
}

void FFmpeg::close()
{
	if (swr)
	{
		swr_close(swr);
		swr_free(&swr);
	}
	if (sws)
	{
		sws_freeContext(sws);
		sws = nullptr;
	}

	if (aframe)
		av_frame_free(&aframe);
	if (vframe)
		av_frame_free(&vframe);
	if (vframeRGB)
	{
		av_free(vbuffer);
		av_frame_free(&vframeRGB);
	}

	if (vctx)
	{
		avcodec_close(vctx);
		avcodec_free_context(&vctx);
	}
	if (actx)
	{
		avcodec_close(actx);
		avcodec_free_context(&actx);
	}
	if (ctx)
		avformat_close_input(&ctx);

	audio_stream = -1;
	video_stream = -1;
	audio_streams.clear();
	astream = nullptr;
	acodec = nullptr;
	vstream = nullptr;
	vcodec = nullptr;

	process_audio = nullptr;
	process_video = nullptr;
}

int FFmpeg::decode_audio(AVCodecContext* avctx, AVFrame* frame, AVPacket* pkt)
{
	int ret;
	ret = avcodec_send_packet(avctx, pkt);
	switch (ret)
	{
	case AVERROR(EAGAIN):
		break;
	case AVERROR_EOF:
	case AVERROR(EINVAL):
	case AVERROR(ENOMEM):
		return 0;
	}

	do
	{
		ret = avcodec_receive_frame(avctx, frame);
		if (ret == AVERROR(EAGAIN))
			return 0;
		if (ret == AVERROR_EOF)
			return 0;
	} while (ret != 0);

	return 1;
}

int FFmpeg::packet_queue()
{
	AVPacket packet;
	while (av_read_frame(ctx, &packet) >= 0)
	{
		if (packet.stream_index == video_stream)
		{
			if (decode_video(vctx, vframe, &packet))
			{
				sws_scale(sws, vframe->data, vframe->linesize, 0, vctx->height, vframeRGB->data, vframeRGB->linesize);
				if (process_video)
					process_video(vframeRGB);
				return 0;
			}
		}
		else if (packet.stream_index == audio_stream)
		{
			if (decode_audio(actx, aframe, &packet))
			{
				//const int max_buffer_size = av_samples_get_buffer_size(NULL, 2, aframe->nb_samples, AV_SAMPLE_FMT_S16, 1);
				//uint8_t* buffer = (uint8_t*)av_malloc(max_buffer_size);
				//swr_convert(swr, (uint8_t**)&buffer, max_buffer_size, (const uint8_t**)aframe->extended_data, aframe->nb_samples);
				//if (process_audio)
				//	process_audio(aframe);
				//av_free(buffer);
				return 1;
			}
		}
	}
	return -1;
}

int FFmpeg::lang_to_astream(int lang_id)
{
	if (ctx->nb_streams <= 1)
		return -1;
	if (ctx->nb_streams <= 2)
		return audio_streams[0];

	return audio_streams[lang_id];
}