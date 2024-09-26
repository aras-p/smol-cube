#include "systeminfo.h"

#include <stdint.h>

#ifdef _WIN32
#include <intrin.h>
#include <windows.h>
#endif

const size_t kCacheFlushDataSize = 128 * 1024 * 1024;
static uint64_t s_CacheFlushArray[kCacheFlushDataSize / 8];
static uint64_t s_CacheFlushScramble;

void SysInfoFlushCaches()
{
#	ifdef WIN32
	FlushInstructionCache(GetCurrentProcess(), NULL, 0);
#	endif
	for (size_t i = 0; i < kCacheFlushDataSize / 8; ++i)
	{
		s_CacheFlushArray[i] = i + s_CacheFlushScramble;
	}
	s_CacheFlushScramble = s_CacheFlushArray[kCacheFlushDataSize / 137];
}
