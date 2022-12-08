#pragma once

extern "C"
{
#pragma warning(disable: 4244)

#include "ffmpeg\include\libavcodec\avcodec.h"
#include "ffmpeg\include\libavfilter\avfilter.h"
#include "ffmpeg\include\libavformat\avformat.h"
#include "ffmpeg\include\libavutil\avutil.h"
#include "ffmpeg\include\libavutil\channel_layout.h"
#include "ffmpeg\include\libavutil\frame.h"
#include "ffmpeg\include\libavutil\mem.h"
#include "ffmpeg\include\libavutil\dict.h"
#include "ffmpeg\include\libavutil\time.h"
#include "ffmpeg\include\libswresample\swresample.h"
#include "ffmpeg\include\libswscale\swscale.h"
#include "ffmpeg\include\libavutil\imgutils.h"

#ifndef FFMPEG_LIBS
#define FFMPEG_LIBS
#pragma comment(lib, "include\\bink\\ffmpeg\\lib\\libavcodec.a")
#pragma comment(lib, "include\\bink\\ffmpeg\\lib\\libavfilter.a")
#pragma comment(lib, "include\\bink\\ffmpeg\\lib\\libavformat.a")
#pragma comment(lib, "include\\bink\\ffmpeg\\lib\\libavutil.a")
#pragma comment(lib, "include\\bink\\ffmpeg\\lib\\libswresample.a")
#pragma comment(lib, "include\\bink\\ffmpeg\\lib\\libswscale.a")
#endif
}

#include <vector>

class FFmpeg
{
public:
	int open(const char* name, int Lang_id);
	int open_flc(const char* name);
	void close();

	static int decode_video(AVCodecContext* avctx, AVFrame* frame, AVPacket* pkt);
	static int decode_audio(AVCodecContext* avctx, AVFrame* frame, AVPacket* pkt);

	void (*process_video)(AVFrame* frame) = nullptr;
	void (*process_audio)(AVFrame* frame) = nullptr;

	int packet_queue();
	int lang_to_astream(int lang_id);

	AVFormatContext* ctx;
	int video_stream, audio_stream;
	const AVCodec* vcodec, * acodec;
	std::vector<int> audio_streams;
	AVStream *astream, *vstream;
	AVCodecContext *actx, *vctx;
	AVCodecParserContext *aparse, *vparse;
	AVFrame *vframe, *vframeRGB,
		*aframe;

	SwsContext* sws;
	SwrContext* swr;
	uint8_t* vbuffer = nullptr;
	size_t vnumBytes = 0;
};