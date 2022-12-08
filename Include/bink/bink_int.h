#pragma once

#define BINK_AUDIO_RATE		48000

namespace bink
{
	typedef struct LOCKED_RECT
	{
		void* pBits;
		size_t Pitch;
	} LOCKED_RECT;

	class Texture
	{
	public:
		Texture() : w(0), h(0), pitch(0),
			buffer(nullptr),
			lock_cnt(0)
		{}

		void Create(int w, int h);
		void Release();

		void Lock(LOCKED_RECT* r);
		void Unlock();

		DWORD *buffer;
		int w, h;
		int pitch;
		int lock_cnt;
	};

	class Video
	{
	public:
		std::vector<Texture*> bmp;
		Texture* disp;
		int head, tail, bufnum,
			w, h;
		volatile DWORD loops : 1,
			exited : 1,
			paused : 1;
		int64_t vpts, apts,
			completed_vpts,
			completed_apts,
			start_pts,
			end_pts,
			start_tick;
		int32_t completed_counter;
		int32_t tickavdiff,
			tickframe,
			ticksleep;
		int64_t ticklast;
		std::vector<int64_t> ppts;

		// semaphores
		HANDLE semr,	// read
			semw;		// write
		// thread
		HANDLE th;
		CRITICAL_SECTION mut;

		static DWORD WINAPI thread(LPVOID ctx);

		void init(int video_w, int video_h, int bufs);
		void shut();
		void stop();

		void decode();
		void sync();
		void getavpts(int64_t** ppapts, int64_t** ppvpts);

		void lock(uint8_t* buffer[], int linesize[]);
		void unlock(int64_t pts);

		void pause();
		void resume();
	};

	class DsndBuffer
	{
		LPDIRECTSOUNDBUFFER buf;
		HANDLE th;
		size_t size, hsize, qsize;
		volatile unsigned int loops : 1,
			exited : 1,
			paused : 1;

	public:
		static DWORD WINAPI thread(LPVOID ctx);
		void create(size_t _size);
		void destroy();
		void play();
		void stop();

		void pause();
		void resume();
	};

	class Audio
	{
	public:
		typedef struct BUF
		{
			int16_t* data;
			int32_t  size;
		} BUF;

		std::vector<int64_t> ppts;
		int bufnum;
		int buflen;
		int head;
		int tail;
		int64_t* apts;
		HANDLE bufsem;
		std::vector<BUF> AudioBuf;
		DsndBuffer dssnd;
		std::vector<std::vector<int16_t>> data;
		double speed;

		void init(int _bufnum, int _buflen);
		void shut();
		void reset();
		void stop();
		void sync(int64_t* papts);

		void lock(BUF** ppab);
		void unlock(int64_t pts);

		void pause();
		void resume();
	};

	class PktQueue
	{
		HANDLE fsem, asem, vsem, lock;
		std::vector<AVPacket> bpkts;
		std::vector<AVPacket*> fpkts,
			apkts,
			vpkts;
		size_t fsize, vsize, asize,
			fhead, ftail,
			vhead, vtail,
			ahead, atail;

	public:
		PktQueue();

		void init(size_t size);
		void reset();
		void shut();

		void audio_enqueue(AVPacket* pkt);
		AVPacket* audio_dequeue();

		void video_enqueue(AVPacket* pkt);
		AVPacket* video_dequeue();

		void free_enqueue(AVPacket* pkt);
		AVPacket* free_dequeue();

		void free_cancel(AVPacket* pkt);
	};

	extern FFmpeg ff;
	extern PktQueue queue;
	extern Audio aud;
	extern Video vid;

	extern size_t aux_buf_avail;
	extern uint8_t* aux_buf_cur;
	extern Audio::BUF* aux_hdr_cur;

	extern int THE_END;
	extern double FPS;

	void CreateThreads();
	void KillThreads();

	void Init();
	void Shut();
	void Pause();
	void Resume();

};