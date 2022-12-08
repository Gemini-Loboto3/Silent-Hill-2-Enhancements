#include <windows.h>
#include "bink\bink.h"
#include "Logging\Logging.h"
#include "Common/Utils.h"
#include "External/Hooking.Patterns/Hooking.Patterns.h"
#include <External/injector/include/injector/injector.hpp>
#include "Common\Settings.h"

void PatchBink()
{
	auto pattern = hook::pattern("E8 ? ? ? ? C3 90 90 90 90 90 90 90  90 90 90 90 90 90 90 90 B8 ? ? ? ? C3 90 90 90 90 90 90 90 90 90 90");
	if (pattern.size() != 1)
	{
		Logging::Log() << __FUNCTION__ " Error: failed to find memory address!";
		return;
	}

#if 1
	WriteJMPtoMemory((BYTE*)0x56FC40, BinkSetSoundOnOff);
	WriteJMPtoMemory((BYTE*)0x56FC46, BinkOpen);
	WriteJMPtoMemory((BYTE*)0x56FC4C, BinkSetSoundTrack);
	WriteJMPtoMemory((BYTE*)0x56FC52, BinkSetSoundSystem);
	WriteJMPtoMemory((BYTE*)0x56FC58, BinkOpenDirectSound);
	WriteJMPtoMemory((BYTE*)0x56FC5E, BinkClose);
	WriteJMPtoMemory((BYTE*)0x56FC64, BinkCopyToBuffer);
	WriteJMPtoMemory((BYTE*)0x56FC6A, BinkNextFrame);
	WriteJMPtoMemory((BYTE*)0x56FC70, BinkDoFrame);
	WriteJMPtoMemory((BYTE*)0x56FC76, BinkWait);
	WriteJMPtoMemory((BYTE*)0x56FC7C, BinkPause);
#else
	uint8_t* ptr_BinkSetSoundOnOff = (uint8_t*)(pattern.count(1).get(0).get<uint32_t>(0)) + 0x25;

	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x00, BinkSetSoundOnOff);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x06, BinkOpen);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x0C, BinkSetSoundTrack);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x12, BinkSetSoundSystem);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x18, BinkOpenDirectSound);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x1E, BinkClose);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x24, BinkCopyToBuffer);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x2A, BinkNextFrame);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x30, BinkDoFrame);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x36, BinkWait);
	WriteJMPtoMemory(ptr_BinkSetSoundOnOff + 0x3C, BinkPause);
#endif
}
