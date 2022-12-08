#include "bink.h"
#include <chrono>
#include <thread>

extern LPDIRECTSOUND DSoundPtr;

namespace bink
{
	FFmpeg ff;
	PktQueue queue;
	Audio aud;
	Video vid;

	size_t aux_buf_avail;
	uint8_t* aux_buf_cur;
	Audio::BUF* aux_hdr_cur;

	int THE_END;
	double FPS;

#define MAX_FRAMES		3
#define MAX_AUDIO		5
#define AUDIO_SIZE		2048
#define BUF_MULT		4

	/////////////////////////////////////////////////////////
	// an actual microsleep function
	void usleep(int64_t usec)
	{
		std::this_thread::sleep_for(std::chrono::microseconds(usec));
	}

	/////////////////////////////////////////////////////////
	// direct sound buffer interface for streaming
	DWORD WINAPI DsndBuffer::thread(LPVOID ctx)
	{
		DsndBuffer* th = (DsndBuffer*)ctx;
		DWORD pos;
		DWORD add = 0, snd_dwOffset = 0, bytes1 = 0;
		const DWORD	snd_dwBytes = th->qsize,
			snd_dwTotal = th->size;
		void* ptr;

		while (th->loops)
		{
			//if (th->paused)
			//{
			//	usleep(20 * 1000);
			//	continue;
			//}

			th->buf->GetCurrentPosition(&pos, nullptr);

			if (pos - snd_dwOffset < 0)
				add = th->size;
			if (pos + add - snd_dwOffset > 2 * snd_dwBytes + 16)
			{
				th->buf->Lock(snd_dwOffset, snd_dwBytes, &ptr, &bytes1, nullptr, nullptr, 0);
				memcpy(ptr, aud.data[aud.head].data(), bytes1);
				th->buf->Unlock(ptr, bytes1, nullptr, 0);

				if (aud.apts)
					*aud.apts = aud.ppts[aud.head];
				if (++aud.head == aud.bufnum) aud.head = 0;
				ReleaseSemaphore(aud.bufsem, 1, NULL);

				DWORD total = snd_dwBytes + snd_dwOffset;
				snd_dwOffset = total;
				if (snd_dwTotal <= total)
					snd_dwOffset = total - snd_dwTotal;
			}

			Sleep(0);
		}

		th->exited = 1;

		return 0;
	}

	void DsndBuffer::create(size_t _size)
	{
		// size stuff
		size = _size;
		hsize = size / 2;
		qsize = size / 4;

		// ffmpeg friendly buffer attributes
		WAVEFORMATEX fmt = { 0 };
		fmt.cbSize = sizeof(fmt);
		fmt.nSamplesPerSec = BINK_AUDIO_RATE;
		fmt.nBlockAlign = 4;
		fmt.nChannels = 2;
		fmt.nAvgBytesPerSec = BINK_AUDIO_RATE * 4;
		fmt.wBitsPerSample = 16;
		fmt.wFormatTag = WAVE_FORMAT_PCM;

		// the actual buffer
		DSBUFFERDESC desc = { 0 };
		desc.dwSize = sizeof(desc);
		desc.lpwfxFormat = &fmt;
		desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_LOCSOFTWARE | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS;
		desc.dwBufferBytes = size;
		DSoundPtr->CreateSoundBuffer(&desc, &buf, nullptr);

		// fill thread
		loops = 1;
		exited = 0;
		paused = 0;
		th = CreateThread(nullptr, 0, thread, this, 0, nullptr);

		play();
	}

	void DsndBuffer::destroy()
	{
		if (th) stop();

		// kill the DS buffer
		buf->Release();
		buf = nullptr;
	}

	void DsndBuffer::play()
	{
		buf->Play(0, 0, DSBPLAY_LOOPING);
	}

	void DsndBuffer::stop()
	{
		// stop buffer so thread doesn't crash
		if (!paused)
			buf->Stop();

		// kill thread
		loops = 0;
		//while (exited != 1);
		WaitForSingleObject(th, INFINITE);
		th = nullptr;
	}

	void DsndBuffer::pause()
	{
		if (!paused)
		{
			buf->Stop();
			paused = 1;
		}
	}

	void DsndBuffer::resume()
	{
		if (paused)
		{
			buf->Play(0, 0, DSBPLAY_LOOPING);
			paused = 0;
		}
	}

	/////////////////////////////////////////////////////////
#define COPY_FRAME	1

	DWORD WINAPI Video::thread(LPVOID ctx)
	{
		Video* v = (Video*)ctx;
		v->decode();

		return 0;
	}

	void Video::init(int video_w, int video_h, int bufs)
	{
		tickavdiff = tickframe = ticksleep = 0;
		ticklast = 0;
		completed_counter = 0;
		completed_apts = completed_vpts = 0;
		start_pts = end_pts = start_tick = 0;

		tickframe = (int32_t)(1000. / FPS);
		ticksleep = tickframe;
		start_pts = AV_NOPTS_VALUE;

		apts = vpts = -1;
		end_pts = 0;

		w = video_w;
		h = video_h;

		bufnum = bufs ? bufs : MAX_FRAMES;

		// textures and synch points
		bmp = std::vector<Texture*>(bufnum);
		for (int i = 0; i < bufnum; i++)
		{
			bmp[i] = new Texture;
			bmp[i]->Create(w, h);
		}

#if COPY_FRAME
		disp = new Texture;
		disp->Create(w, h);
		InitializeCriticalSection(&mut);
#else
		disp = nullptr;
#endif
		ppts = std::vector<int64_t>(bufnum);

		head = tail = 0;

		// semaphores for write/read
		semr = CreateSemaphoreA(nullptr, 0, 1024, nullptr);
		semw = CreateSemaphoreA(nullptr, bufnum, 1024, nullptr);

		// thread init
		loops = 1;
		exited = 0;
		paused = 0;
		th = CreateThread(nullptr, 0, thread, this, 0, nullptr);
	}

	void Video::shut()
	{
		if (th) stop();

		// release data structures
		for (int i = 0; i < bufnum; i++)
		{
			if (bmp[i])
			{
				bmp[i]->Release();
				delete bmp[i];
			}
		}
		bmp.clear();
#if COPY_FRAME
		disp->Release();
		delete disp;
		DeleteCriticalSection(&mut);
#endif
		disp = nullptr;
		ppts.clear();

		// release semaphores
		CloseHandle(semr); semr = nullptr;
		CloseHandle(semw); semw = nullptr;
	}

	void Video::stop()
	{
		// kill thread
		loops = 0;
		ReleaseSemaphore(semr, 1, nullptr);
		//while (exited != 1);
		WaitForSingleObject(th, INFINITE);

		th = nullptr;
	}

	void Video::decode()
	{
		while (loops)
		{
			//if (paused)
			//{
			//	usleep(20 * 1000);
			//	continue;
			//}

			WaitForSingleObject(semr, INFINITE);

			int64_t cvpts = vpts = ppts[head];
			if (cvpts != -1)
			{
#if COPY_FRAME
				EnterCriticalSection(&mut);

				LOCKED_RECT rd, rs;
				disp->Lock(&rd);
				bmp[head]->Lock(&rs);

				memcpy(rd.pBits, rs.pBits, rd.Pitch * h);

				disp->Unlock();
				bmp[head]->Unlock();
#else
				disp = bmp[head];
#endif
				LeaveCriticalSection(&mut);
			}

			if (++head == bufnum)
				head = 0;
			ReleaseSemaphore(semw, 1, nullptr);

			if (end_pts)
			{
				if (vpts + (30 * tickframe) > end_pts)
				{
					vpts = AV_NOPTS_VALUE;
					apts = AV_NOPTS_VALUE;
				}
			}

			sync();
		}

		exited = 1;
	}

#define COMPLETE_COUNTER 30

	void Video::sync()
	{
		int tickdiff, scdiff, avdiff = -1;
		int64_t tickcur;

		if (!THE_END)
		{
			if (completed_apts != apts || completed_vpts != vpts)
			{
				completed_apts = apts;
				completed_vpts = vpts;
				completed_counter = 0;
			}
			else if ((vpts == -1 || vpts == AV_NOPTS_VALUE) && ++completed_counter == COMPLETE_COUNTER)
				THE_END = 1;

			tickcur = av_gettime_relative() / 1000;
			tickdiff = (int)(tickcur - ticklast);
			ticklast = tickcur;

			if (start_pts == AV_NOPTS_VALUE)
			{
				start_pts = vpts;
				start_tick = tickcur;
			}

			avdiff = (int)(apts - vpts - tickavdiff);								// diff between audio and video pts
			scdiff = (int)(start_pts + tickcur - start_tick - vpts - tickavdiff);	// diff between system clock and video pts
			if (apts <= 0)
				avdiff = scdiff;	// if apts is invalid, sync vpts to system clock

			if (tickdiff - tickframe > 5) ticksleep--;
			if (tickdiff - tickframe < -5) ticksleep++;
			if (vpts >= 0)
			{
				if (avdiff > 500) ticksleep -= 3;
				else if (avdiff > 50) ticksleep -= 1;
				else if (avdiff < -500) ticksleep += 3;
				else if (avdiff < -50) ticksleep += 1;
			}
			if (ticksleep < 0) ticksleep = 0;
		}
		else ticksleep = tickframe;

		if (ticksleep > 0)
			usleep(ticksleep * 1000);
	}

	void Video::getavpts(int64_t** ppapts, int64_t** ppvpts)
	{
		if (ppapts) *ppapts = &apts;
		if (ppvpts) *ppvpts = &vpts;
	}

	void Video::lock(uint8_t* buffer[], int linesize[])
	{
		WaitForSingleObject(semw, INFINITE);

		LOCKED_RECT r;
		bmp[tail]->Lock(&r);

		if (buffer) buffer[0] = (uint8_t*)r.pBits;
		if (linesize) linesize[0] = r.Pitch;
	}

	void Video::unlock(int64_t pts)
	{
		bmp[tail]->Unlock();
		ppts[tail] = pts;
		if (++tail == bufnum)
			tail = 0;
		ReleaseSemaphore(semr, 1, nullptr);
	}

	void Video::pause()
	{
		if (!paused)
			paused = 1;
	}

	void Video::resume()
	{
		if (paused)
			paused = 0;
	}

	/////////////////////////////////////////////////////////
	void Audio::init(int _bufnum, int _buflen)
	{
		bufnum = _bufnum ? _bufnum : MAX_AUDIO;
		buflen = _buflen ? _buflen : AUDIO_SIZE;
		head = 0;
		tail = 0;
		ppts = std::vector<int64_t>(bufnum);
		bufsem = CreateSemaphoreA(nullptr, bufnum, bufnum, nullptr);

		data = std::vector<std::vector<int16_t>>(bufnum);
		for (int i = 0; i < bufnum; i++)
			data[i] = std::vector<int16_t>(buflen / 2);

		AudioBuf = std::vector<BUF>(bufnum);

		dssnd.create(buflen * BUF_MULT);
		speed = 0.;
	}

	void Audio::shut()
	{
		AudioBuf.clear();
		dssnd.destroy();
		data.clear();
		ppts.clear();

		reset();
		CloseHandle(bufsem); bufsem = nullptr;
	}

	void Audio::reset()
	{
		head = tail = 0;
		ReleaseSemaphore(bufsem, bufnum, NULL);
	}

	void Audio::stop()
	{
		dssnd.stop();
	}

	void Audio::sync(int64_t* papts)
	{
		apts = papts;
	}

	void Audio::lock(BUF** ppab)
	{
		WaitForSingleObject(bufsem, INFINITE);
		*ppab = &AudioBuf[tail];

		AudioBuf[tail].data = data[tail].data();
		AudioBuf[tail].size = buflen;
	}

	void Audio::unlock(int64_t pts)
	{
		ppts[tail] = pts;

		if (++tail == bufnum)
			tail = 0;
	}

	void Audio::pause()
	{
		dssnd.pause();
	}

	void Audio::resume()
	{
		dssnd.resume();
	}

	/////////////////////////////////////////////////////////
	PktQueue::PktQueue()
	{
		fsize = asize = vsize = 0;
		fhead = ftail = ahead = 0;
		atail = vhead = vtail = 0;
		fsem = asem = vsem = nullptr;
		lock = nullptr;
	}

	void PktQueue::init(size_t size)
	{
		fsize = size ? size : 256;
		asize = vsize = fsize;

		// packets
		bpkts = std::vector<AVPacket>(fsize);
		fpkts = std::vector<AVPacket*>(fsize);
		apkts = std::vector<AVPacket*>(asize);
		vpkts = std::vector<AVPacket*>(vsize);
		// semaphores
		fsem = CreateSemaphoreA(nullptr, fsize, 1024, nullptr);
		asem = CreateSemaphoreA(nullptr, 0, 1024, nullptr);
		vsem = CreateSemaphoreA(nullptr, 0, 1024, nullptr);
		// mutex
		lock = CreateMutexA(nullptr, false, nullptr);

		for (size_t i = 0; i < fsize; i++)
			fpkts[i] = &bpkts[i];
	}

	void PktQueue::reset()
	{
		AVPacket* packet = nullptr;

		while ((packet = audio_dequeue()) != nullptr)
		{
			av_packet_unref(packet);
			free_enqueue(packet);
		}

		while ((packet = video_dequeue()) != nullptr)
		{
			av_packet_unref(packet);
			free_enqueue(packet);
		}

		fhead = ftail = 0;
		ahead = atail = 0;
		vhead = vtail = 0;
	}

	void PktQueue::shut()
	{
		// free all queues
		reset();
		// delete semaphores
		CloseHandle(fsem); fsem = nullptr;
		CloseHandle(asem); asem = nullptr;
		CloseHandle(vsem); vsem = nullptr;
		// delete mutex
		CloseHandle(lock); lock = nullptr;
		// deallocate buffers
		bpkts.clear();
		fpkts.clear();
		apkts.clear();
		vpkts.clear();
	}

	void PktQueue::audio_enqueue(AVPacket* pkt)
	{
		apkts[atail++ & (asize - 1)] = pkt;
		ReleaseSemaphore(asem, 1, nullptr);
	}

	AVPacket* PktQueue::audio_dequeue()
	{
		if (WaitForSingleObject(asem, 0) != 0)
			return nullptr;
		return apkts[ahead++ & (asize - 1)];
	}

	void PktQueue::video_enqueue(AVPacket* pkt)
	{
		vpkts[vtail++ & (vsize - 1)] = pkt;
		ReleaseSemaphore(vsem, 1, nullptr);
	}

	AVPacket* PktQueue::video_dequeue()
	{
		if (WaitForSingleObject(vsem, 0) != 0)
			return nullptr;
		return vpkts[vhead++ & (vsize - 1)];
	}

	inline void PktQueue::free_enqueue(AVPacket* pkt)
	{
		WaitForSingleObject(lock, INFINITE);
		fpkts[ftail++ & (fsize - 1)] = pkt;
		ReleaseMutex(lock);
		ReleaseSemaphore(fsem, 1, nullptr);
	}

	AVPacket* PktQueue::free_dequeue()
	{
		if (WaitForSingleObject(fsem, 0) != 0)
			return nullptr;
		return fpkts[fhead++ & (fsize - 1)];
	}

	void PktQueue::free_cancel(AVPacket* pkt)
	{
		WaitForSingleObject(lock, INFINITE);
		fpkts[ftail++ & (fsize - 1)] = pkt;
		ReleaseMutex(lock);
		ReleaseSemaphore(fsem, 1, NULL);
	}

	/////////////////////////////////////////////////////////
	// decoding for threads
	void process_video(AVFrame* vframe)
	{
		AVFrame pic = { 0 };

		vid.lock(pic.data, pic.linesize);
		if (vframe->data[0] && vframe->pts != -1)
			sws_scale(ff.sws, vframe->data, vframe->linesize, 0, vframe->height, pic.data, pic.linesize);
		vid.unlock(vframe->pts);
	}

	void process_audio(AVFrame* aframe)
	{
		int sampnum = 0;
		int64_t apts = aframe->pts;

		do
		{
			if (aux_buf_avail == 0)
			{
				aud.lock(&aux_hdr_cur);
				apts += (int64_t)(aud.speed / FPS);
				if (aud.speed == 0.)
					aud.speed = 1000.;
				aux_buf_avail = (int)aux_hdr_cur->size;
				aux_buf_cur = (uint8_t*)aux_hdr_cur->data;
			}

			//++ do resample audio data ++//
			sampnum = swr_convert(ff.swr,
				(uint8_t**)&aux_buf_cur, aux_buf_avail / 4,
				(const uint8_t**)aframe->extended_data, aframe->nb_samples);
			aframe->extended_data = NULL;
			aframe->nb_samples = 0;
			aux_buf_avail -= sampnum * 4;
			aux_buf_cur += sampnum * 4;
			//-- do resample audio data --//

			if (aux_buf_avail == 0)
				aud.unlock(apts);

		} while (sampnum > 0);
	}

	/////////////////////////////////////////////////////////
	// threaded interface for ffmpeg decoding
	HANDLE hVid, hAud, hDem;
	volatile BYTE vid_decode,
		aud_decode,
		dem_decode,
		dec_exited,
		is_suspended,
		is_asafe,
		is_vsafe,
		is_dsafe;

	DWORD WINAPI video_thread(LPVOID ctx)
	{
		int frame = 0;
		static const AVRational TIMEBASE_MS = { 1, 1000 };

		while (vid_decode)
		{
			if (is_suspended)
			{
				is_vsafe = 1;
				usleep(20 * 1000);
				continue;
			}

			auto pkt = queue.video_dequeue();
			if (pkt == nullptr)
				usleep(20 * 1000);
			else
			{
				if (ff.decode_video(ff.vctx, ff.vframe, pkt))
				{
					ff.vframe->pts = av_rescale_q(ff.vframe->pts, ff.vstream->time_base, TIMEBASE_MS);

					process_video(ff.vframe);
					frame++;
				}

				av_packet_unref(pkt);
				queue.free_enqueue(pkt);
			}
		}

		dec_exited |= 1;

		return 0;
	}

	DWORD WINAPI audio_thread(LPVOID ctx)
	{
		int64_t apts;
		int frame = 0;
		static const AVRational TIMEBASE_MS = { 1, 1000 };
		ff.aframe->pts = -1;

		while (aud_decode)
		{
			if (is_suspended)
			{
				is_asafe = 1;
				usleep(20 * 1000);
				continue;
			}

			auto pkt = queue.audio_dequeue();
			if (pkt == nullptr)
				usleep(20 * 1000);
			else
			{
				apts = AV_NOPTS_VALUE;
				if (ff.decode_audio(ff.actx, ff.aframe, pkt))
				{
					static const AVRational tb_sample_rate{ 1, ff.actx->sample_rate };

					if (apts == AV_NOPTS_VALUE)
						apts = av_rescale_q(pkt->pts, ff.actx->time_base, tb_sample_rate);
					else apts += ff.aframe->nb_samples;

					ff.aframe->pts = av_rescale_q(apts, tb_sample_rate, TIMEBASE_MS);
					process_audio(ff.aframe);
				}

				av_packet_unref(pkt);
				queue.free_enqueue(pkt);
			}
		}

		dec_exited |= 2;

		return 0;
	}

	DWORD WINAPI demux_thread(LPVOID ctx)
	{
		while (dem_decode)
		{
			if (is_suspended)
			{
				is_dsafe = 1;
				usleep(20 * 1000);
				continue;
			}

			auto pkt = queue.free_dequeue();
			if (pkt == nullptr)
				usleep(20 * 1000);
			else
			{
				auto retv = av_read_frame(ff.ctx, pkt);
				if (retv < 0)
				{
					av_packet_unref(pkt);
					queue.free_cancel(pkt);
					usleep(20 * 1000);
				}
				else
				{
					if (pkt->stream_index == ff.video_stream)
						queue.video_enqueue(pkt);
					else if (pkt->stream_index == ff.audio_stream)
						queue.audio_enqueue(pkt);
					else
					{
						av_packet_unref(pkt);
						queue.free_cancel(pkt);
					}
				}
			}
		}

		dec_exited |= 4;

		return 0;
	}

	void CreateThreads()
	{
		aux_buf_avail = 0;
		aux_buf_cur = nullptr;
		aux_hdr_cur = nullptr;

		aud_decode = 1;
		vid_decode = 1;
		dem_decode = 1;
		dec_exited = 0;

		int64_t* papts = nullptr;
		vid.getavpts(&papts, nullptr);
		aud.sync(papts);

		hDem = CreateThread(nullptr, 0, demux_thread, nullptr, 0, nullptr);
		hAud = CreateThread(nullptr, 0, audio_thread, nullptr, 0, nullptr);
		hVid = CreateThread(nullptr, 0, video_thread, nullptr, 0, nullptr);
	}

	void KillThreads()
	{
		vid.stop();
		aud.stop();

		dem_decode = 0;
		aud_decode = 0;
		vid_decode = 0;

		WaitForSingleObject(hDem, INFINITE); hDem = nullptr;
		WaitForSingleObject(hAud, INFINITE); hAud = nullptr;
		WaitForSingleObject(hVid, INFINITE); hVid = nullptr;
	}

	void Init()
	{
		is_suspended = 0;

		queue.init(0);
		vid.init(ff.vctx->width, ff.vctx->height, MAX_FRAMES);
		aud.init(MAX_AUDIO, AUDIO_SIZE);

		CreateThreads();
	}

	void Shut()
	{
		KillThreads();
		queue.shut();
		aud.shut();
		vid.shut();
	}

	void Pause()
	{
		if (is_suspended == 0)
		{
			is_vsafe = 0;
			is_asafe = 0;
			is_dsafe = 0;
			is_suspended = 1;
			while (!is_vsafe);
			while (!is_asafe);
			while (!is_dsafe);
		}

		aud.pause();
		vid.pause();
	}

	void Resume()
	{
		aud.resume();
		vid.resume();

		if (is_suspended == 1)
		{
			is_suspended = 0;
			SwitchToThread();

			is_vsafe = 0;
			is_asafe = 0;
			is_dsafe = 0;
		}
	}

	void Texture::Create(int _w, int _h)
	{
		if (buffer)
			delete[] buffer;

		w = _w;
		h = _h;
		buffer = new DWORD[_w * _h];
		pitch = w * sizeof(*buffer);
	}

	void Texture::Release()
	{
		if (buffer)
		{
			while (lock_cnt)
				usleep(20 * 1000);
			delete[] buffer;
			buffer = nullptr;
		}
		w = 0;
		h = 0;
		pitch = 0;
	}

	void Texture::Lock(LOCKED_RECT* r)
	{
		lock_cnt++;
		r->pBits = buffer;
		r->Pitch = pitch;
	}

	void Texture::Unlock()
	{
		lock_cnt--;
	}
};
