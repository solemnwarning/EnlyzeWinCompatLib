#pragma once

#include <Windows.h>
#include <atomic>

/**
 * @brief Spin lock suitable for use during CRT initialisation.
 *
 * This spin lock class has no requirements on heap/etc functions so it may be
 * used before the CRT is initialised and implements the BasicLockable C++
 * interface requirements.
*/
class StaticLock
{
private:
	std::atomic_bool m_locked;
	unsigned int m_spin_count;

public:
	/* Somewhat arbitrarily chosen default spin count based on the Windows heap manager (MSDN):
	 *
	 * > You can improve performance significantly by choosing a small spin count for a
	 * > critical section of short duration. The heap manager uses a spin count of roughly 4000
	 * > for its per-heap critical sections. This gives great performance and scalability in
	 * > almost all worst-case scenarios.
	*/
	constexpr StaticLock(unsigned int spin_count = 4000) :
		m_locked(false),
		m_spin_count(spin_count) {}

	void lock()
	{
		unsigned int count = 0;

		while(true)
		{
			bool expected = false;
			if(m_locked.compare_exchange_weak(expected, true))
			{
				return;
			}

			/* Periodically yield to other threads when we fail to acquire the lock. */
			if(++count >= m_spin_count)
			{
				SwitchToThread();
				count = 0;
			}
		}
	}

	void unlock()
	{
		m_locked.store(false);
	}
};
