#pragma once

#ifdef _NVWA
#include "..\nvwa\debug_new.h"
#endif

#ifndef BARE_BINK
#include <windows.h>
#include <dsound.h>
#include <vector>
#include "ffmpeg.h"
#include "bink_int.h"
#endif

typedef unsigned int U32;
typedef unsigned char U8;
typedef signed int S32;
typedef signed char S8;

typedef void(__stdcall *BINKSNDSYSOPEN)(S32);

typedef struct BINKRECT
{
	S32 Left;
	S32 Top;
	S32 Width;
	S32 Height;
} BINKRECT;

typedef struct BINK
{
	U32 Width;
	U32 Height;
	U32 Frames;
	U32 FrameNum;
	U32 LastFrameNum;
	U32 FrameRate;
	U32 FrameRateDiv;
	U32 ReadError;
	U32 OpenFlags;
	U32 BinkType;
	U32 Size;
	U32 FrameSize;
	U32 SndSize;
	BINKRECT FrameRects[8];
	S32 NumRects;
	U32 PlaneNum;
	void* YPlane[2];
	void* APlane[2];
	U32 YWidth;
	U32 YHeight;
	U32 UVWidth;
	U32 UVHeight;
	void* MaskPlane;
	U32 MaskPitch;
	U32 MaskLength;
	U32 LargestFrameSize;
	U32 InternalFrames;
	S32 NumTracks;
	U32 Highest1SecRate;
	U32 Highest1SecFrame;
	S32 Paused;
	U32 BackgroundThread;
	void* compframe;
	void* preloadptr;
	U32* frameoffsets;
} BINK, *HBINK;

S32 WINAPI BinkSetSoundOnOff(HBINK bnk, int onoff);
HBINK WINAPI BinkOpen(const char* name, U32 flags);
void WINAPI BinkSetSoundTrack(int total_tracks, U32* tracks);
S32  WINAPI BinkSetSoundSystem(BINKSNDSYSOPEN open, U32 param);
void WINAPI BinkOpenDirectSound(U32 param);
void WINAPI BinkClose(HBINK bnk);
S32  WINAPI BinkCopyToBuffer(HBINK bnk, void* dest, U32 destpitch, U32 destheight, U32 destx, U32 desty, U32 flags);
void WINAPI BinkNextFrame(HBINK bnk);
S32  WINAPI BinkDoFrame(HBINK bnk);
S32  WINAPI BinkWait(HBINK bnk);
S32  WINAPI BinkPause(HBINK bnk, S32 pause);
