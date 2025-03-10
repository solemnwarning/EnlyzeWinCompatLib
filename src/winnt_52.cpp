//
// EnlyzeWinCompatLib - Let Clang-compiled applications run on older Windows versions
// Copyright (c) 2021 Colin Finck, ENLYZE GmbH <c.finck@enlyze.com>
// SPDX-License-Identifier: MIT
//

// This file implements required APIs not available in Windows Server 2003 RTM (NT 5.2).
#include "EnlyzeWinCompatLibInternal.h"

#if defined(__clang__) && __has_warning("-Wcast-function-type-mismatch")
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif

#define CRITICAL_SECTION_NO_DEBUG_INFO           RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO
#define RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO  0x01000000

typedef BOOL(WINAPI *PFN_INITIALIZECRITICALSECTIONEX)(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount, DWORD Flags);
typedef int(WINAPI *PFN_GETLOCALEINFOEX)(LPCWSTR lpLocaleName, LCTYPE LCType, LPWSTR lpLCData, int cchData);

static PFN_INITIALIZECRITICALSECTIONEX pfnInitializeCriticalSectionEx = nullptr;
static PFN_GETLOCALEINFOEX pfnGetLocaleInfoEx = nullptr;

static BOOL WINAPI
_CompatInitializeCriticalSectionEx(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount, DWORD Flags)
{
	/* The only (documented) flag at this point is CRITICAL_SECTION_NO_DEBUG_INFO, which we can
	 * safely ignore on pre-Vista platforms with just a performance hit.
	*/

	if(Flags == 0 || Flags == CRITICAL_SECTION_NO_DEBUG_INFO)
	{
		return InitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);
	}
	else{
		SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
		return 0;
	}
}

extern "C" BOOL WINAPI
LibInitializeCriticalSectionEx(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount, DWORD Flags)
{
	if(!pfnInitializeCriticalSectionEx)
	{
		// Check if the API is provided by kernel32, otherwise fall back to our implementation.
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnInitializeCriticalSectionEx = reinterpret_cast<PFN_INITIALIZECRITICALSECTIONEX>(GetProcAddress(hKernel32, "InitializeCriticalSectionEx"));
		if(!pfnInitializeCriticalSectionEx)
		{
			pfnInitializeCriticalSectionEx = _CompatInitializeCriticalSectionEx;
		}
	}

	return pfnInitializeCriticalSectionEx(lpCriticalSection, dwSpinCount, Flags);
}

static int WINAPI
_CompatGetLocaleInfoEx(LPCWSTR lpLocaleName, LCTYPE LCType, LPWSTR lpLCData, int cchData)
{
	(void)(lpLocaleName);
	(void)(LCType);
	(void)(lpLCData);
	(void)(cchData);

	/* TODO: Implement whatever is possible on top of GetLocaleInfo() */
	SetLastError(ERROR_INVALID_FLAGS);
	return 0;
}

extern "C" int WINAPI
LibGetLocaleInfoEx(LPCWSTR lpLocaleName, LCTYPE LCType, LPWSTR lpLCData, int cchData)
{
	if(!pfnGetLocaleInfoEx)
	{
		// Check if the API is provided by kernel32, otherwise fall back to our implementation.
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnGetLocaleInfoEx = reinterpret_cast<PFN_GETLOCALEINFOEX>(GetProcAddress(hKernel32, "GetLocaleInfoEx"));
		if(!pfnGetLocaleInfoEx)
		{
			pfnGetLocaleInfoEx = _CompatGetLocaleInfoEx;
		}
	}

	return pfnGetLocaleInfoEx(lpLocaleName, LCType, lpLCData, cchData);
}
