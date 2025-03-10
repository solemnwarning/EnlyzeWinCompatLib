/* This file provides an implementation of the Fiber Local Storage APIs and
 * some other Fiber functions implemented by Server 2003/Vista and required by
 * the Windows UCRT version 10.0.22621.0 and later, or the vcruntime lib
 * included with Visual Studio 2026.
*/

#include "EnlyzeWinCompatLibInternal.h"

#include <assert.h>
#include <algorithm>
#include <mutex>

#include "EarlyArray.h"
#include "EarlyMap.h"
#include "StaticLock.h"

#if defined(__clang__) && __has_warning("-Wcast-function-type-mismatch")
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif

/* Uncomment this to force use of our fiber-local storage implementation, this
 * is mostly useful for testing it on more modern systems with better debugging
 * tools.
*/
// #define FORCE_OUR_FLS

/**
 * @brief Implementation of Fiber Local Storage.
 *
 * This class implements Fiber Local Storage as provided in Windows 2003/Vista and newer.
 *
 * On modern versions of Windows, this class will simply call the relevant functions from
 * KERNEL32.DLL, while on older Windows it will instead internally maintain any allocated FLS
 * slots, values and which threads are executing fibers.
*/
class FlsManager
{
private:
	/* We must reserve enough space that all threads/slots set up during early/late CRT stages
	 * can be registered without allocating from the heap, when it may be in an unsafe state.
	*/
	static constexpr size_t RESERVE_SLOTS = 8;
	static constexpr size_t RESERVE_THREADS = 16;
	static constexpr size_t RESERVE_FIBERS = 4;

	struct FlsSlot
	{
		bool used;

		PFLS_CALLBACK_FUNCTION callback;

		EarlyMap<DWORD, PVOID, RESERVE_THREADS> thread_values;
		EarlyMap<PVOID, PVOID, RESERVE_FIBERS> fiber_values;

		FlsSlot() :
			used(false) {}

		FlsSlot(const FlsSlot&) = delete;
		FlsSlot &operator=(const FlsSlot&) = delete;

		FlsSlot(FlsSlot &&src) :
			used(src.used),
			callback(src.callback),
			thread_values(std::move(src.thread_values)),
			fiber_values(std::move(src.fiber_values))
		{
			src.used = false;
		}
	};

	static StaticLock mutex; /**< Mutex to synchronise access to all (static) members. */
	static bool initialised; /**< Has FlsManager been initialised yet? */

	static bool native_available; /**< Are all native FLS APIs available? */

	static EarlyArray<FlsSlot, RESERVE_SLOTS> slots; /**< Allocated FLS slots (only if initialised && !native_available) */
	static EarlyMap<DWORD, PVOID, RESERVE_THREADS> thread_fiber_map; /**< Thread ID to fiber mapping (only if initialised && !native_available) */

	typedef DWORD(WINAPI *PFN_FLSALLOC)(PFLS_CALLBACK_FUNCTION lpCallback);
	typedef BOOL(WINAPI *PFN_FLSFREE)(DWORD dwFlsIndex);
	typedef PVOID(WINAPI *PFN_FLSGETVALUE)(DWORD dwFlsIndex);
	typedef BOOL(WINAPI *PFN_FLSSETVALUE)(DWORD dwFlsIndex, PVOID lpFlsData);
	typedef BOOL(WINAPI *PFN_ISTHREADAFIBER)();

	static PFN_FLSALLOC pfnFlsAlloc; /**< Native FlsAlloc() function (only if initialised && native_available) */
	static PFN_FLSFREE pfnFlsFree; /**< Native FlsFree() function (only if initialised && native_available) */
	static PFN_FLSGETVALUE pfnFlsGetValue; /**< Native FlsGetValue() function (only if initialised && native_available) */
	static PFN_FLSSETVALUE pfnFlsSetValue; /**< Native FlsSetValue() function (only if initialised && native_available) */
	static PFN_ISTHREADAFIBER pfnIsThreadAFiber; /**< Native IsThreadAFiber() function (only if initialised && native_available) */

	static void EnsureInitialised();

public:
	/**
	 * @brief Allocate a Fiber Local Storage slot (see FlsAlloc() on MSDN).
	*/
	static DWORD Alloc(PFLS_CALLBACK_FUNCTION lpCallback);

	/**
	 * @brief Delete a Fiber Local Storage slot (see FlsFree() on MSDN).
	*/
	static BOOL Free(DWORD dwFlsIndex);

	/**
	 * @brief Load a value from a Fiber Local Storage slot (see FlsGetValue() on MSDN).
	*/
	static PVOID GetValue(DWORD dwFlsIndex);

	/**
	 * @brief Store a value in a Fiber Local Storage slot (see FlsSetValue() on MSDN).
	*/
	static BOOL SetValue(DWORD dwFlsIndex, PVOID lpFlsData);

	/**
	 * @brief Notify FlsManager that the thread is becoming or switching between fibers.
	 *
	 * This method is called when the current thread becomes or switches between fiber contexts
	 * to save any stored value into the old fiber (if applicabl) and load any previously-saved
	 * values from the new one.
	*/
	static void ThreadEnterFiber(LPVOID lpFiber);

	/**
	 * @brief Notify FlsManager that the thread is exiting.
	 *
	 * This method is called when the current thread is about to exit or cease to be a fiber
	 * in order for any callbacks to be executed and thread/fiber data to be removed from the
	 * FLS data structures.
	*/
	static void ThreadExit();

	/**
	 * @brief Notify FlsManager that a fiber is being deleted.
	 *
	 * This method is called when the a fiber is about to be deleted in order for any callbacks
	 * to be executed and fiber data to be removed from the FLS data structures.
	*/
	static void FiberDelete(LPVOID lpFiber);

	/**
	 * @brief Check if the current thread is recorded as being a fiber.
	*/
	static bool ThreadIsFiber();
};

StaticLock FlsManager::mutex;
bool FlsManager::initialised = false;
bool FlsManager::native_available;

EarlyArray<FlsManager::FlsSlot, FlsManager::RESERVE_SLOTS> FlsManager::slots(true);
EarlyMap<DWORD, PVOID, FlsManager::RESERVE_THREADS> FlsManager::thread_fiber_map;

FlsManager::PFN_FLSALLOC FlsManager::pfnFlsAlloc;
FlsManager::PFN_FLSFREE FlsManager::pfnFlsFree;
FlsManager::PFN_FLSGETVALUE FlsManager::pfnFlsGetValue;
FlsManager::PFN_FLSSETVALUE FlsManager::pfnFlsSetValue;
FlsManager::PFN_ISTHREADAFIBER FlsManager::pfnIsThreadAFiber;

void FlsManager::EnsureInitialised()
{
	if(initialised)
	{
		return;
	}

#ifndef FORCE_OUR_FLS
	HMODULE hKernel32 = GetModuleHandleW(L"kernel32");

	pfnFlsAlloc = reinterpret_cast<PFN_FLSALLOC>(GetProcAddress(hKernel32, "FlsAlloc"));
	pfnFlsFree = reinterpret_cast<PFN_FLSFREE>(GetProcAddress(hKernel32, "FlsFree"));
	pfnFlsGetValue = reinterpret_cast<PFN_FLSGETVALUE>(GetProcAddress(hKernel32, "FlsGetValue"));
	pfnFlsSetValue = reinterpret_cast<PFN_FLSSETVALUE>(GetProcAddress(hKernel32, "FlsSetValue"));
	pfnIsThreadAFiber = reinterpret_cast<PFN_ISTHREADAFIBER>(GetProcAddress(hKernel32, "IsThreadAFiber"));

	if(pfnFlsAlloc != NULL && pfnFlsFree != NULL && pfnFlsGetValue != NULL && pfnFlsSetValue != NULL && pfnIsThreadAFiber != NULL)
	{
		native_available = true;
	}
#endif

	initialised = true;
}

DWORD FlsManager::Alloc(PFLS_CALLBACK_FUNCTION lpCallback)
{
	std::unique_lock<StaticLock> lock_guard(mutex);
	EnsureInitialised();

	if(native_available)
	{
		return pfnFlsAlloc(lpCallback);
	}
	else {
		try {
			FlsSlot* free_it = std::find_if(slots.begin(), slots.end(), [](const FlsSlot &slot) { return !(slot.used); });
			if(free_it != slots.end())
			{
				free_it->callback = lpCallback;
				free_it->used = true;
				return free_it - slots.begin();
			}
			else {
				slots.resize(slots.size() + 1);
				slots.back().callback = lpCallback;
				slots.back().used = true;
				
				return slots.size() - 1;
			}
		}
		catch(const std::bad_alloc&)
		{
			return (DWORD)0xFFFFFFFF; /* FLS_OUT_OF_INDEXES */
		}
	}
}

BOOL FlsManager::Free(DWORD dwFlsIndex)
{
	std::unique_lock<StaticLock> lock_guard(mutex);
	EnsureInitialised();

	if(native_available)
	{
		return pfnFlsFree(dwFlsIndex);
	}
	else {
		if(slots.size() <= dwFlsIndex || !(slots[dwFlsIndex].used))
		{
			SetLastError(ERROR_INVALID_PARAMETER);
			return FALSE;
		}

		FlsSlot slot = std::move(slots[dwFlsIndex]);

		lock_guard.unlock();

		if(slot.callback != NULL)
		{
			for(auto it = slot.thread_values.begin(); it != slot.thread_values.end(); ++it)
			{
				if(it->value != NULL)
				{
					slot.callback(it->value);
				}
			}

			for(auto it = slot.fiber_values.begin(); it != slot.fiber_values.end(); ++it)
			{
				if(it->value != NULL)
				{
					slot.callback(it->value);
				}
			}
		}

		return TRUE;
	}
}

PVOID FlsManager::GetValue(DWORD dwFlsIndex)
{
	std::unique_lock<StaticLock> lock_guard(mutex);
	EnsureInitialised();

	if(native_available)
	{
		return pfnFlsGetValue(dwFlsIndex);
	}
	else {
		if(slots.size() <= dwFlsIndex || !(slots[dwFlsIndex].used))
		{
			SetLastError(ERROR_INVALID_PARAMETER);
			return NULL;
		}

		FlsSlot &slot = slots[dwFlsIndex];

		auto elem = slot.thread_values.find(GetCurrentThreadId());
		if(elem != slot.thread_values.end())
		{
			return elem->value;
		}
		else {
			return NULL;
		}
	}
}

BOOL FlsManager::SetValue(DWORD dwFlsIndex, PVOID lpFlsData)
{
	std::unique_lock<StaticLock> lock_guard(mutex);
	EnsureInitialised();

	if(native_available)
	{
		return pfnFlsSetValue(dwFlsIndex, lpFlsData);
	}
	else {
		if(slots.size() <= dwFlsIndex || !(slots[dwFlsIndex].used))
		{
			SetLastError(ERROR_INVALID_PARAMETER);
			return FALSE;
		}

		FlsSlot &slot = slots[dwFlsIndex];

		slot.thread_values.set(GetCurrentThreadId(), lpFlsData);

		return TRUE;
	}
}

void FlsManager::ThreadEnterFiber(LPVOID lpFiber)
{
	DWORD dwThreadId = GetCurrentThreadId();

	std::unique_lock<StaticLock> lock_guard(mutex);
	EnsureInitialised();

	if(native_available)
	{
		return;
	}
	else {
		LPVOID oldFiber = NULL;

		auto thread_fibers_it = thread_fiber_map.find(dwThreadId);
		if(thread_fibers_it != thread_fiber_map.end())
		{
			oldFiber = thread_fibers_it->value;
			assert(oldFiber != NULL);
		}

		for(auto slot_it = slots.begin(); slot_it != slots.end(); ++slot_it)
		{
			FlsSlot &slot = *slot_it;

			if(oldFiber != NULL)
			{
				assert(slot.fiber_values.find(oldFiber) == slot.fiber_values.end());
			}

			auto thread_value_it = slot.thread_values.find(dwThreadId);
			if(thread_value_it != slot.thread_values.end())
			{
				if(oldFiber == NULL)
				{
					oldFiber = GetCurrentFiber();
					assert(oldFiber != NULL);
				}

				slot.fiber_values.set(oldFiber, thread_value_it->value);
				slot.thread_values.erase(thread_value_it);
			}

			auto fiber_value_it = slot.fiber_values.find(lpFiber);
			if(fiber_value_it != slot.fiber_values.end())
			{
				slot.thread_values.set(dwThreadId, fiber_value_it->value);
				slot.fiber_values.erase(fiber_value_it);
			}
		}

		thread_fiber_map.set(dwThreadId, lpFiber);
	}
}

void FlsManager::ThreadExit()
{
	DWORD dwThreadId = GetCurrentThreadId();

	std::unique_lock<StaticLock> lock_guard(mutex);
	EnsureInitialised();

	if(native_available)
	{
		return;
	}
	else {
		struct PendingCallback
		{
			PFLS_CALLBACK_FUNCTION callback;
			PVOID value;

			PendingCallback(PFLS_CALLBACK_FUNCTION callback, PVOID value) :
				callback(callback), value(value) {}
		};

		EarlyArray<PendingCallback, RESERVE_SLOTS> callbacks;

		for(auto slot_it = slots.begin(); slot_it != slots.end(); ++slot_it)
		{
			FlsSlot &slot = *slot_it;

			auto thread_value_it = slot.thread_values.find(dwThreadId);
			if(thread_value_it != slot.thread_values.end())
			{
				if(slot.callback != NULL && thread_value_it->value != NULL)
				{
					callbacks.push(PendingCallback(slot.callback, thread_value_it->value));
				}

				slot.thread_values.erase(thread_value_it);
			}
		}

		thread_fiber_map.erase(dwThreadId);

		lock_guard.unlock();

		for(auto it = callbacks.begin(); it != callbacks.end(); ++it)
		{
			it->callback(it->value);
		}
	}
}

void FlsManager::FiberDelete(LPVOID lpFiber)
{
	std::unique_lock<StaticLock> lock_guard(mutex);
	EnsureInitialised();

	if(native_available)
	{
		return;
	}
	else {
		/* Check if DeleteFiber() has been called by the thread currently executing it, in
		 * which case we simply handle if like a thread termination.
		*/
		auto thread_fiber_it = thread_fiber_map.find(GetCurrentThreadId());
		if(thread_fiber_it != thread_fiber_map.end() && thread_fiber_it->value == lpFiber)
		{
			lock_guard.unlock();

			ThreadExit();
			return;
		}

		/* ...otherwise assume the fiber being deleted is an inactive one, since deleting
		 *    an executing fiber from outside it is UB.
		*/

		struct PendingCallback
		{
			PFLS_CALLBACK_FUNCTION callback;
			PVOID value;

			PendingCallback(PFLS_CALLBACK_FUNCTION callback, PVOID value) :
				callback(callback), value(value) {
			}
		};

		EarlyArray<PendingCallback, RESERVE_SLOTS> callbacks;

		for(auto slot_it = slots.begin(); slot_it != slots.end(); ++slot_it)
		{
			FlsSlot &slot = *slot_it;

			auto fiber_value_it = slot.fiber_values.find(lpFiber);
			if(fiber_value_it != slot.fiber_values.end())
			{
				if(slot.callback != NULL && fiber_value_it->value != NULL)
				{
					callbacks.push(PendingCallback(slot.callback, fiber_value_it->value));
				}

				slot.fiber_values.erase(fiber_value_it);
			}
		}

		lock_guard.unlock();

		for(auto it = callbacks.begin(); it != callbacks.end(); ++it)
		{
			it->callback(it->value);
		}
	}
}

bool FlsManager::ThreadIsFiber()
{
	std::unique_lock<StaticLock> lock_guard(mutex);
	EnsureInitialised();

	if(native_available)
	{
		return pfnIsThreadAFiber() != FALSE;
	}
	else {
		return thread_fiber_map.find(GetCurrentThreadId()) != NULL;
	}
}

extern "C" DWORD WINAPI LibFlsAlloc(PFLS_CALLBACK_FUNCTION lpCallback)
{
	return FlsManager::Alloc(lpCallback);
}

extern "C" BOOL WINAPI LibFlsFree(DWORD dwFlsIndex)
{
	return FlsManager::Free(dwFlsIndex);
}

extern "C" BOOL WINAPI LibFlsSetValue(DWORD dwFlsIndex, PVOID lpFlsData)
{
	return FlsManager::SetValue(dwFlsIndex, lpFlsData);
}

extern "C" PVOID WINAPI LibFlsGetValue(DWORD dwFlsIndex)
{
	return FlsManager::GetValue(dwFlsIndex);
}

extern "C" BOOL WINAPI LibIsThreadAFiber()
{
	return FlsManager::ThreadIsFiber() ? TRUE : FALSE;
}

typedef BOOL(WINAPI *PFN_CONVERTFIBERTOTHREAD)();
typedef LPVOID(WINAPI *PFN_CONVERTTHREADTOFIBER)(LPVOID lpParameter);
typedef LPVOID(WINAPI *PFN_CONVERTTHREADTOFIBEREX)(LPVOID lpParameter, DWORD dwFlags);
typedef VOID(WINAPI *PFN_SWITCHTOFIBER)(LPVOID lpFiber);
typedef VOID(WINAPI *PFN_DELETEFIBER)(LPVOID lpFiber);

typedef VOID(WINAPI *PFN_EXITTHREAD)(DWORD dwExitCode);
typedef VOID(WINAPI *PFN_EXITPROCESS)(UINT uExitCode);
typedef VOID(WINAPI *PFN_FREELIBRARYANDEXITTHREAD)(HMODULE hLibModule, DWORD dwExitCode);

static PFN_CONVERTFIBERTOTHREAD pfnConvertFiberToThread = NULL;
static PFN_CONVERTTHREADTOFIBER pfnConvertThreadToFiber = NULL;
static PFN_CONVERTTHREADTOFIBEREX pfnConvertThreadToFiberEx = NULL;
static PFN_SWITCHTOFIBER pfnSwitchToFiber = NULL;
static PFN_DELETEFIBER pfnDeleteFiber = NULL;
static PFN_EXITTHREAD pfnExitThread = NULL;
static PFN_EXITPROCESS pfnExitProcess = NULL;
static PFN_FREELIBRARYANDEXITTHREAD pfnFreeLibraryAndExitThread = NULL;

extern "C" BOOL WINAPI LibConvertFiberToThread()
{
	if(!pfnConvertFiberToThread)
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnConvertFiberToThread = reinterpret_cast<PFN_CONVERTFIBERTOTHREAD>(GetProcAddress(hKernel32, "ConvertFiberToThread"));
		if(!pfnConvertFiberToThread)
		{
			SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
			return FALSE;
		}
	}

	BOOL result = pfnConvertFiberToThread();
	
	if(result)
	{
		/* The thread itself isn't technically exiting, however it is returning to being
		 * a normal thread and any FLS callbacks should be called for values which are set
		 * within the fiber.
		*/

		FlsManager::ThreadExit();
	}

	return result;
}

extern "C" LPVOID WINAPI LibConvertThreadToFiber(LPVOID lpParameter)
{
	if(!pfnConvertThreadToFiber)
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnConvertThreadToFiber = reinterpret_cast<PFN_CONVERTTHREADTOFIBER>(GetProcAddress(hKernel32, "ConvertThreadToFiber"));
	}

	LPVOID fiber = pfnConvertThreadToFiber(lpParameter);

	if(fiber != NULL)
	{
		FlsManager::ThreadEnterFiber(fiber);
	}

	return fiber;
}

extern "C" LPVOID WINAPI LibConvertThreadToFiberEx(LPVOID lpParameter, DWORD dwFlags)
{
	if(!pfnConvertThreadToFiberEx)
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnConvertThreadToFiberEx = reinterpret_cast<PFN_CONVERTTHREADTOFIBEREX>(GetProcAddress(hKernel32, "ConvertThreadToFiberEx"));
	}

	LPVOID fiber;
	if(pfnConvertThreadToFiberEx)
	{
		fiber = pfnConvertThreadToFiberEx(lpParameter, dwFlags);
	}
	else {
		if(!pfnConvertThreadToFiber)
		{
			HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
			pfnConvertThreadToFiber = reinterpret_cast<PFN_CONVERTTHREADTOFIBER>(GetProcAddress(hKernel32, "ConvertThreadToFiber"));
		}

		/* > Win32 also provides SwitchToFiberEx that can optionally save the floating
		 * > point context. The Microsoft documentation warns that if the appropriate flag
		 * > is not set the floating point context may not be saved and restored correctly.
		 * > In practice this seems not to be needed because the calling conventions on
		 * > this platform requires the floating point register stack to be empty before
		 * > calling any function, SwitchToFiber included. The exception is that if the
		 * > floating point control word is modified, other fibers will see the new
		 * > floating point status. This should be expected thought, because the control
		 * > word should be treated as any other shared state. Currently Boost.Coroutine
		 * > does not set the "save floating point" flag (saving the floating point control
		 * > word is a very expensive operation), but seems to work fine anyway. To
		 * > complicate the matter more, recent Win32 documentation reveal that the
		 * > FIBER_FLAG_FLOAT_SWITCH flag is no longer supported since Windows XP and
		 * > Windows 2000 SP4.
		 *
		 * tl;dr - we map to ConvertThreadToFiber() whether or not that flag is set and
		 * hope for the best.
		*/

		if(dwFlags != 0 && dwFlags != FIBER_FLAG_FLOAT_SWITCH)
		{
			SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
			return NULL;
		}

		fiber = pfnConvertThreadToFiber(lpParameter);
	}

	if(fiber != NULL)
	{
		FlsManager::ThreadEnterFiber(fiber);
	}

	return fiber;
}

extern "C" VOID WINAPI LibSwitchToFiber(LPVOID lpFiber)
{
	if(!pfnSwitchToFiber)
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnSwitchToFiber = reinterpret_cast<PFN_SWITCHTOFIBER>(GetProcAddress(hKernel32, "SwitchToFiber"));
	}

	FlsManager::ThreadEnterFiber(lpFiber);

	pfnSwitchToFiber(lpFiber);
}

extern "C" VOID WINAPI LibDeleteFiber(LPVOID lpFiber)
{
	if(!pfnDeleteFiber)
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnDeleteFiber = reinterpret_cast<PFN_DELETEFIBER>(GetProcAddress(hKernel32, "DeleteFiber"));
	}

	FlsManager::FiberDelete(lpFiber);

	pfnDeleteFiber(lpFiber);
}

/* We hook the ExitThread() function in order to detect threads exiting and execute FLS callbacks
 * as required for C++ thread_local variable destruction.
 *
 * This works both for threads explicitly terminated by calling ExitThread() and those that return
 * from their ThreadProc() (or main()) functions because the CRT internally calls ExitThread() when
 * they return.
*/
extern "C" VOID WINAPI LibExitThread(DWORD dwExitCode)
{
	if(!pfnExitThread)
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnExitThread = reinterpret_cast<PFN_EXITTHREAD>(GetProcAddress(hKernel32, "ExitThread"));
	}

	FlsManager::ThreadExit();

	pfnExitThread(dwExitCode);
}

/* ...or sometimes the CRT likes to end a thread this way instead. */
extern "C" VOID WINAPI LibFreeLibraryAndExitThread(HMODULE hLibModule, DWORD dwExitCode)
{
	if(!pfnFreeLibraryAndExitThread)
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnFreeLibraryAndExitThread = reinterpret_cast<PFN_FREELIBRARYANDEXITTHREAD>(GetProcAddress(hKernel32, "FreeLibraryAndExitThread"));
	}

	FlsManager::ThreadExit();

	pfnFreeLibraryAndExitThread(hLibModule, dwExitCode);
}

extern "C" VOID WINAPI LibExitProcess(UINT uExitCode)
{
	if(!pfnExitProcess)
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		pfnExitProcess = reinterpret_cast<PFN_EXITPROCESS>(GetProcAddress(hKernel32, "ExitProcess"));
	}

	FlsManager::ThreadExit();

	pfnExitProcess(uExitCode);
}
