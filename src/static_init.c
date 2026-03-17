// SPDX-License-Identifier: CC0-1.0

// SPDX-FileCopyrightText: 2019 Timo Kreuzer
// SPDX-FileCopyrightText: 2020 Stefan Schmidt

// Derived from ReactOS CRT code to provide thread safe statics initialization
// in nxdk, as required by C++11.

#include <stdint.h>
#include <limits.h>
#include <windows.h>

long _Init_global_epoch = LONG_MIN;
__declspec(thread) long _Init_thread_epoch = LONG_MIN;

/*
    This function tries to acquire a lock on the initialization for the static
    variable by changing the value saved in *ptss to -1. If *ptss is 0, the
    variable was not initialized yet and the function tries to set it to -1.
    If that succeeds, the function will return. If the value is already -1,
    another thread is in the process of doing the initialization and we
    wait for it. If it is any other value the initialization is complete.
    After returning the compiler generated code will check the value:
    if it is -1 it will continue with the initialization, otherwise the
    initialization must be complete and will be skipped.
*/
void _Init_thread_header (volatile int *ptss)
{
    int spin_count = 0;

    while (1) {
        /* Try to acquire the initialization lock */
        long oldTss = _InterlockedCompareExchange((volatile long *)ptss, -1, 0);
        
        if (oldTss == 0) {
            /* We acquired the lock. Caller proceeds with initialization. */
            return;
        } 
        else if (oldTss == -1) {
            if (spin_count < 4000) {
                YieldProcessor(); 
                spin_count++;
            } else {
                Sleep(1); 
            }
            continue;
        }

        /* The initialization is complete, update the epoch so this call can be skipped in the future. */
        _Init_thread_epoch = _Init_global_epoch;
        return;
    }
}

void _Init_thread_footer (volatile int *ptss)
{
    /* Increment the global epoch */
    long new_epoch = _InterlockedIncrement(&_Init_global_epoch);
    while (new_epoch == 0 || new_epoch == -1) {
        new_epoch = _InterlockedIncrement(&_Init_global_epoch);
    }

    _Init_thread_epoch = new_epoch;
    /* Initialization is complete */
    _InterlockedExchange((volatile long *)ptss, new_epoch);
}

void _Init_thread_abort (volatile int *ptss)
{
    /* Abort the initialization */
    _InterlockedAnd((volatile long *)ptss, 0);
}
