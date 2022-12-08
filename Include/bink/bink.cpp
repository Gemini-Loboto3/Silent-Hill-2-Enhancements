#include "bink.h"

LPDIRECTSOUND DSoundPtr;
using namespace bink;
int track = 0;

S32 WINAPI BinkSetSoundOnOff(HBINK bnk, int onoff)
{
	UNREFERENCED_PARAMETER(bnk);
	UNREFERENCED_PARAMETER(onoff);

	return 0;
}

HBINK WINAPI BinkOpen(const char* name, U32 flags)
{
	UNREFERENCED_PARAMETER(flags);

	if (ff.open(name, track) == 0)
		return nullptr;

	BINK* bnk = new BINK;
	memset(bnk, 0, sizeof(BINK));

	if(ff.vstream->nb_frames)
		bnk->Frames = (U32)ff.vstream->nb_frames;	// mp4
	else
		bnk->Frames = (U32)ff.vstream->duration;	// bink

	FPS = av_q2d(ff.vstream->avg_frame_rate);
	bnk->FrameRate = (U32)FPS;
	bnk->Width = ff.vctx->width;
	bnk->Height = ff.vctx->height;

	Init();

	return bnk;
}

void WINAPI BinkSetSoundTrack(int total_tracks, U32* tracks)
{
	UNREFERENCED_PARAMETER(total_tracks);
	track = *tracks;
}

S32  WINAPI BinkSetSoundSystem(BINKSNDSYSOPEN open, U32 param)
{
	open(param);

	return 1;
}

void WINAPI BinkOpenDirectSound(U32 param)
{
	DSoundPtr = (LPDIRECTSOUND)param;
}

void WINAPI BinkClose(HBINK bnk)
{
	Pause();
	Shut();
	ff.close();

	track = 0;

	delete bnk;
}

S32  WINAPI BinkCopyToBuffer(HBINK bnk, void* dest, U32 destpitch, U32 destheight, U32 destx, U32 desty, U32 flags)
{
	if (vid.disp == nullptr)
		return 0;

	UNREFERENCED_PARAMETER(bnk);
	UNREFERENCED_PARAMETER(flags);

	uint8_t* d = (uint8_t*)dest;
	
	LOCKED_RECT r;
	vid.disp->Lock(&r);

	uint8_t* s = (uint8_t*)r.pBits;

	d = &d[desty * destpitch + destx];

	for (UINT y = 0; y < destheight; y++)
	{
		memcpy(d, s, destpitch);
		d += destpitch;
		s += r.Pitch;
	}

	vid.disp->Unlock();

	return 0;	// return no frames skipped
}

void WINAPI BinkNextFrame(HBINK bnk)
{
	bnk->FrameNum++;
}

S32  WINAPI BinkDoFrame(HBINK bnk)
{
	UNREFERENCED_PARAMETER(bnk);

	return 0;	// return no frames skipped
}

S32  WINAPI BinkWait(HBINK bnk)
{
	UNREFERENCED_PARAMETER(bnk);

	return 0;	// it's okay to display frame
}

S32  WINAPI BinkPause(HBINK bnk, S32 pause)
{
	UNREFERENCED_PARAMETER(bnk);

	// pause
	if (pause)
	{
		Pause();
		return 0;
	}
	// resume
	else
	{
		Resume();
		return 1;
	}
}
