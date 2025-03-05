//
// EnlyzeWinCompatLib - Let Clang-compiled applications run on older Windows versions
// Copyright (c) 2021 Colin Finck, ENLYZE GmbH <c.finck@enlyze.com>
// SPDX-License-Identifier: MIT
//

// This file implements required APIs not available in Windows 7 RTM (NT 6.1).
#include "EnlyzeWinCompatLibInternal.h"

#if defined(__clang__) && __has_warning("-Wcast-function-type-mismatch")
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif

typedef VOID(WINAPI *PFN_GETSYSTEMTIMEASPRECISEFILETIME)(LPFILETIME lpSystemTimeAsFileTime);

static PFN_GETSYSTEMTIMEASPRECISEFILETIME pfnGetSystemTimeAsPreciseFileTime = nullptr;

static VOID WINAPI
_CompatGetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
	// Fall back to GetSystemTimeAsFileTime() which does the same thing but with less precision.
	GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}

extern "C" VOID WINAPI
LibGetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
	if(!pfnGetSystemTimeAsPreciseFileTime)
	{
		// Check if the API is provided by kernel32, otherwise fall back to our implementation.
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnGetSystemTimeAsPreciseFileTime = reinterpret_cast<PFN_GETSYSTEMTIMEASPRECISEFILETIME>(GetProcAddress(hKernel32, "GetSystemTimePreciseAsFileTime"));
		if(!pfnGetSystemTimeAsPreciseFileTime)
		{
			pfnGetSystemTimeAsPreciseFileTime = _CompatGetSystemTimePreciseAsFileTime;
		}
	}

	return pfnGetSystemTimeAsPreciseFileTime(lpSystemTimeAsFileTime);
}
