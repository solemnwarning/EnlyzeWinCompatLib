/* Test suite for Fiber-Local Storage.
 * Copyright (C) 2025 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program tests FLS for both fibers and threads, baseline behaviour was established on a
 * Windows 10 system, it should pass on NT4 running EnlyzeWinCompatLib too.
*/

#include <windows.h>

#include <assert.h>
#include <condition_variable>
#include <functional>
#include <stdio.h>
#include <string>
#include <thread>

static void __stdcall fls_callback(void *data);

static void print_direct(const char *string)
{
	DWORD result;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), string, strlen(string), &result, NULL);
}

struct FlsData
{
	const char *name;
	
	bool deleted;
	DWORD deletion_thread;
	
	FlsData(const char *name):
		name(name),
		deleted(false),
		deletion_thread(0) {}
};

struct FlsSlot
{
	DWORD id;
	
	FlsSlot()
	{
		id = FlsAlloc(&fls_callback);
		
		if(id == FLS_OUT_OF_INDEXES)
		{
			fprintf(stderr, "FlsAlloc: %lu\n", GetLastError());
			abort();
		}
	}
	
	void free()
	{
		if(id != FLS_OUT_OF_INDEXES)
		{
			FlsFree(id);
			id = FLS_OUT_OF_INDEXES;
		}
	}
	
	void set(FlsData *data)
	{
		FlsSetValue(id, data);
	}
	
	FlsData *get() const
	{
		return (FlsData*)(FlsGetValue(id));
	}
};

class Thread
{
	private:
		const char *m_name;
		
		std::thread m_thread;
		DWORD m_thread_id;
		
		std::mutex m_mutex;
		std::condition_variable m_condvar;
		int m_step;
		
		static Thread *g_threads;
		Thread *m_next;
		
	public:
		static DWORD g_main_thread_id;
		
		Thread(const char *name, const std::function<void(Thread*)> &func):
			m_name(name),
			m_step(-2)
		{
			m_thread = std::thread([this, func]()
			{
				m_thread_id = GetCurrentThreadId();
				go_to_step(-1);
				wait_for_step(0);
				
				func(this);
			});
			
			wait_for_step(-1);
			go_to_step(0);
			
			m_next = g_threads;
			g_threads = this;
		}
		
		~Thread()
		{
			Thread **tp = &g_threads;
			
			while((*tp) != this)
			{
				tp = &((*tp)->m_next);
			}
			
			(*tp) = (*tp)->m_next;
		}
		
		void wait_for_step(int step)
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_condvar.wait(lock, [&]() { return m_step == step; });
		}
		
		void go_to_step(int step)
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_step = step;
			lock.unlock();
			
			m_condvar.notify_all();
		}
		
		DWORD get_thread_id() const
		{
			return m_thread_id;
		}
		
		void join()
		{
			m_thread.join();
			print_direct(m_name);
			print_direct(" joined\r\n");
		}
		
		static const char *find_thread_name(DWORD thread_id)
		{
			if(thread_id == g_main_thread_id)
			{
				return "main thread";
			}
			
			for(Thread *t = g_threads; t != NULL; t = t->m_next)
			{
				if(t->m_thread_id == thread_id)
				{
					return t->m_name;
				}
			}
			
			return "unknown thread";
		}
};

Thread *Thread::g_threads = NULL;
DWORD Thread::g_main_thread_id;

class Fiber
{
private:
	void *m_fiber;

	enum class FiberCreationType
	{
		CONVERT_THREAD_TO_FIBER,
		CONVERT_THREAD_TO_FIBER_EX,
		CREATE_FIBER,
	};

	Fiber(FiberCreationType type)
	{
		switch(type)
		{
		case FiberCreationType::CONVERT_THREAD_TO_FIBER:
			m_fiber = ::ConvertThreadToFiber(this);
			break;

		case FiberCreationType::CONVERT_THREAD_TO_FIBER_EX:
			m_fiber = ::ConvertThreadToFiberEx(this, 0);
			break;

		case FiberCreationType::CREATE_FIBER:
			m_fiber = ::CreateFiber(0, &fiber_main, this);
			break;
		}
	}

	void *m_return_fiber;
	const std::function<void()> *m_run_func;

	static void __stdcall fiber_main(void *lpFiberParameter)
	{
		Fiber *self = (Fiber*)(lpFiberParameter);

		while(self->m_run_func != NULL)
		{
			(*(self->m_run_func))();
			SwitchToFiber(self->m_return_fiber);
		}
	}

public:
	static Fiber *ConvertThreadToFiber()
	{
		return new Fiber(FiberCreationType::CONVERT_THREAD_TO_FIBER);
	}

	static Fiber *ConvertThreadToFiberEx()
	{
		return new Fiber(FiberCreationType::CONVERT_THREAD_TO_FIBER_EX);
	}

	static Fiber *CreateFiber()
	{
		return new Fiber(FiberCreationType::CREATE_FIBER);
	}

	/**
	 * @brief Delete the underlying fiber.
	*/
	void DeleteFiber()
	{
		/* NOTE: DeleteFiber() won't return if called on the current fiber. */
		void *fiber = m_fiber;
		m_fiber = NULL;

		::DeleteFiber(fiber);
	}

	~Fiber()
	{
		if(m_fiber != NULL)
		{
			DeleteFiber();
		}
	}

	/**
	 * @brief Execute a function within the fiber.
	*/
	void run(const std::function<void()> &func)
	{
		m_return_fiber = GetCurrentFiber();
		m_run_func = &func;

		SwitchToFiber(m_fiber);
	}

	/**
	 * @brief Return from the fiber.
	*/
	void end()
	{
		m_run_func = NULL;

		SwitchToFiber(m_fiber);
	}
};

static void __stdcall fls_callback(void *data)
{
	FlsData *fd = (FlsData*)(data);
	
	print_direct(fd->name);
	print_direct(" deleted on ");
	print_direct(Thread::find_thread_name(GetCurrentThreadId()));
	print_direct("\r\n");
	
	assert(!(fd->deleted));
	
	fd->deletion_thread = GetCurrentThreadId();
	fd->deleted = true;
}

static int fls_thread_data()
{
	FlsSlot slot;

	static FlsData main_thread_data("main_thread_data");
	static FlsData thread1_data("thread1_data");
	static FlsData thread2_data("thread2_data");

	slot.set(&main_thread_data);

	Thread thread1("thread1", [&](Thread *thread)
	{
		slot.set(&thread1_data);

		thread->go_to_step(1);
		thread->wait_for_step(2);

		print_direct("thread1 got ");
		print_direct(slot.get()->name);
		print_direct(" from the slot\r\n");

		thread->go_to_step(3);
		thread->wait_for_step(4);
	});

	Thread thread2("thread2", [&](Thread *thread)
	{
		slot.set(&thread2_data);

		thread->go_to_step(1);
		thread->wait_for_step(2);

		print_direct("thread2 got ");
		print_direct(slot.get()->name);
		print_direct(" from the slot\r\n");

		thread->go_to_step(3);
		thread->wait_for_step(4);
	});

	thread1.wait_for_step(1);
	thread2.wait_for_step(1);

	print_direct("main thread got ");
	print_direct(slot.get()->name);
	print_direct(" from the slot\r\n");

	thread1.go_to_step(2);
	thread1.wait_for_step(3);

	thread2.go_to_step(2);
	thread2.wait_for_step(3);

	print_direct("about to call ExitProcess() on main thread\r\n");
	ExitProcess(0);
}

static int fls_thread_return()
{
	FlsSlot slot;
	
	FlsData thread_data("thread_data");
	
	Thread thread("thread", [&](Thread *thread)
	{
		slot.set(&thread_data);
		print_direct("about to return from thread\r\n");
	});
	
	thread.join();

	print_direct("about to return from main()\r\n");
	
	return 0;
}

static int fls_thread_ThreadExit()
{
	FlsSlot slot;
	
	FlsData thread_data("thread_data");
	
	Thread thread("thread", [&](Thread *thread)
	{
		slot.set(&thread_data);

		print_direct("about to call ExitThread() in thread\r\n");
		ExitThread(0);
	});
	
	thread.join();

	print_direct("about to return from main()\r\n");
	
	return 0;
}

static int fls_thread_exit()
{
	FlsSlot slot;
	
	FlsData thread_data("thread_data");
	
	Thread thread("thread", [&](Thread *thread)
	{
		slot.set(&thread_data);

		print_direct("about to call exit() in thread\r\n");
		exit(0);
	});
	
	thread.join();
	
	abort();
}

static int fls_thread_free1()
{
	FlsSlot slot;
	
	FlsData thread_data("thread_data");
	
	Thread thread("thread", [&](Thread *thread)
	{
		slot.set(&thread_data);
		
		print_direct("calling FlsFree() on thread...\r\n");
		slot.free();
		print_direct("returned from FlsFree()\r\n");
	});
	
	thread.join();
	
	assert(thread_data.deleted);
	assert(thread_data.deletion_thread == thread.get_thread_id());
	
	return 0;
}

static int fls_thread_free2()
{
	FlsSlot slot;
	
	FlsData thread_data("thread_data");
	
	Thread thread("thread", [&](Thread *thread)
	{
		slot.set(&thread_data);
		
		thread->go_to_step(1);
		thread->wait_for_step(2);
	});
	
	thread.wait_for_step(1);
	
	print_direct("calling FlsFree() on main thread...\r\n");
	slot.free();
	print_direct("returned from FlsFree()\r\n");
	
	thread.go_to_step(2);
	thread.join();
	
	return 0;
}

static int fls_main_thread_return()
{
	FlsSlot slot;
	
	static FlsData main_thread_data("main_thread_data");
	slot.set(&main_thread_data);
	
	return 0;
}

static int fls_main_thread_exit()
{
	FlsSlot slot;
	
	static FlsData main_thread_data("main_thread_data");
	slot.set(&main_thread_data);
	
	exit(0);
}

static int fls_fiber_data()
{
	FlsSlot slot;

	static FlsData main_thread_data("main_thread_data");
	static FlsData this_fiber_data("this_fiber_data");
	static FlsData fiber1_data("fiber1_data");
	static FlsData fiber2_data("fiber2_data");

	Fiber *this_fiber = Fiber::ConvertThreadToFiber();

	Fiber *fiber1 = Fiber::CreateFiber();
	Fiber *fiber2 = Fiber::CreateFiber();

	slot.set(&this_fiber_data);
	fiber1->run([&]() { slot.set(&fiber1_data);  });
	fiber2->run([&]() { slot.set(&fiber2_data);  });
	
	print_direct("this fiber got "); print_direct(slot.get()->name); print_direct(" from the slot\r\n");
	fiber1->run([&]() { print_direct("fiber1 got "); print_direct(slot.get()->name); print_direct(" from the slot\r\n"); });
	fiber2->run([&]() { print_direct("fiber2 got "); print_direct(slot.get()->name); print_direct(" from the slot\r\n"); });

	print_direct("about to call ExitProcess() on the main fiber\r\n");

	ExitProcess(0);

	return 0;
}

static int fls_fiber_delete_self()
{
	FlsSlot slot;

	static FlsData main_thread_data("main_thread_data");
	static FlsData this_fiber_data("this_fiber_data");
	static FlsData fiber1_data("fiber1_data");
	static FlsData fiber2_data("fiber2_data");

	Fiber *this_fiber = Fiber::ConvertThreadToFiber();

	Fiber *fiber1 = Fiber::CreateFiber();
	Fiber *fiber2 = Fiber::CreateFiber();

	slot.set(&this_fiber_data);
	fiber1->run([&]() { slot.set(&fiber1_data);  });
	fiber2->run([&]() { slot.set(&fiber2_data);  });

	fiber1->run([&]() {
		print_direct("about to call DeleteFiber() on the running fiber\r\n"); 
		fiber1->DeleteFiber();
	});

	print_direct("about to call ExitProcess() on the main fiber\r\n");

	ExitProcess(0);

	return 0;
}

static int fls_fiber_delete_other()
{
	FlsSlot slot;

	static FlsData main_thread_data("main_thread_data");
	static FlsData this_fiber_data("this_fiber_data");
	static FlsData fiber1_data("fiber1_data");
	static FlsData fiber2_data("fiber2_data");

	Fiber *this_fiber = Fiber::ConvertThreadToFiberEx();

	Fiber *fiber1 = Fiber::CreateFiber();
	Fiber *fiber2 = Fiber::CreateFiber();

	slot.set(&this_fiber_data);
	fiber1->run([&]() { slot.set(&fiber1_data);  });
	fiber2->run([&]() { slot.set(&fiber2_data);  });

	print_direct("about to delete fiber1 from the main fiber\r\n");
	fiber1->DeleteFiber();

	print_direct("about to call ExitProcess() on the main fiber\r\n");

	ExitProcess(0);

	return 0;
}

static int fls_fiber_return_from_main()
{
	FlsSlot slot;

	static FlsData main_thread_data("main_thread_data");
	static FlsData this_fiber_data("this_fiber_data");
	static FlsData fiber1_data("fiber1_data");
	static FlsData fiber2_data("fiber2_data");

	Fiber *this_fiber = Fiber::ConvertThreadToFiberEx();

	Fiber *fiber1 = Fiber::CreateFiber();
	Fiber *fiber2 = Fiber::CreateFiber();

	slot.set(&this_fiber_data);
	fiber1->run([&]() { slot.set(&fiber1_data);  });
	fiber2->run([&]() { slot.set(&fiber2_data);  });

	print_direct("about to return from main()\r\n");

	return 0;
}

static int fls_fiber_delete_slot()
{
	FlsSlot slot;

	static FlsData main_thread_data("main_thread_data");
	static FlsData this_fiber_data("this_fiber_data");
	static FlsData fiber1_data("fiber1_data");
	static FlsData fiber2_data("fiber2_data");

	Fiber *this_fiber = Fiber::ConvertThreadToFiberEx();

	Fiber *fiber1 = Fiber::CreateFiber();
	Fiber *fiber2 = Fiber::CreateFiber();

	slot.set(&this_fiber_data);
	fiber1->run([&]() { slot.set(&fiber1_data);  });
	fiber2->run([&]() { slot.set(&fiber2_data);  });

	print_direct("about to delete slot from the main fiber\r\n");
	slot.free();

	print_direct("about to call ExitProcess() on the main fiber\r\n");

	ExitProcess(0);

	return 0;
}

struct Test
{
	const char *name;
	int (*impl)();
	const char *reference;
};

static Test tests[] = {
	{ "fls_thread_data", &fls_thread_data,
		"main thread got main_thread_data from the slot\r\n"
		"thread1 got thread1_data from the slot\r\n"
		"thread2 got thread2_data from the slot\r\n"
		"about to call ExitProcess() on main thread\r\n"
		"main_thread_data deleted on main thread\r\n"
	},

	{ "fls_thread_return", &fls_thread_return,
		"about to return from thread\r\n"
		"thread_data deleted on thread\r\n"
		"thread joined\r\n"
		"about to return from main()\r\n"
	},
	
	{ "fls_thread_ThreadExit", &fls_thread_ThreadExit,
		"about to call ExitThread() in thread\r\n"
		"thread_data deleted on thread\r\n"
		"thread joined\r\n"
		"about to return from main()\r\n"
	},
	
	{ "fls_thread_exit", &fls_thread_exit,
		"about to call exit() in thread\r\n"
		"thread_data deleted on thread\r\n"
	},
	
	{ "fls_thread_free1", &fls_thread_free1,
		"calling FlsFree() on thread...\r\n"
		"thread_data deleted on thread\r\n"
		"returned from FlsFree()\r\n"
		"thread joined\r\n"
	},
	
	{ "fls_thread_free2", &fls_thread_free2,
		"calling FlsFree() on main thread...\r\n"
		"thread_data deleted on main thread\r\n"
		"returned from FlsFree()\r\n"
		"thread joined\r\n"
	},
	
	{ "fls_main_thread_return", &fls_main_thread_return,
		"main_thread_data deleted on main thread\r\n"
	},
	
	{ "fls_main_thread_exit", &fls_main_thread_exit,
		"main_thread_data deleted on main thread\r\n"
	},

	{ "fls_fiber_data", &fls_fiber_data,
		"this fiber got this_fiber_data from the slot\r\n"
		"fiber1 got fiber1_data from the slot\r\n"
		"fiber2 got fiber2_data from the slot\r\n"
		"about to call ExitProcess() on the main fiber\r\n"
		"this_fiber_data deleted on main thread\r\n"
	},

	{ "fls_fiber_delete_self", &fls_fiber_delete_self,
		"about to call DeleteFiber() on the running fiber\r\n"
		"fiber1_data deleted on main thread\r\n"
	},

	{ "fls_fiber_delete_other", &fls_fiber_delete_other,
		"about to delete fiber1 from the main fiber\r\n"
		"fiber1_data deleted on main thread\r\n"
		"about to call ExitProcess() on the main fiber\r\n"
		"this_fiber_data deleted on main thread\r\n"
	},

	{ "fls_fiber_return_from_main", &fls_fiber_return_from_main,
		"about to return from main()\r\n"
		"this_fiber_data deleted on main thread\r\n"
	},

	{ "fls_fiber_delete_slot", &fls_fiber_delete_slot,
		"about to delete slot from the main fiber\r\n"
		"this_fiber_data deleted on main thread\r\n"
		"fiber1_data deleted on main thread\r\n"
		"fiber2_data deleted on main thread\r\n"
		"about to call ExitProcess() on the main fiber\r\n"
	},
};

static int num_tests = sizeof(tests) / sizeof(*tests);

int main(int argc, char **argv)
{
	Thread::g_main_thread_id = GetCurrentThreadId();
	
	if(argc == 1)
	{
		int failures = 0;
		
		printf("Running tests... \n");
		
		for(int i = 0; i < num_tests; ++i)
		{
			printf("%s... ", tests[i].name);
			
			HANDLE output_rh, output_wh;
			
			SECURITY_ATTRIBUTES pipe_sa = {
				sizeof(SECURITY_ATTRIBUTES),
				NULL,
				TRUE
			};
			
			if(!CreatePipe(&output_rh, &output_wh, &pipe_sa, 0))
			{
				DWORD error = GetLastError();
				printf("CreatePipe failed with error code %lu\n", error);
				
				++failures;
				continue;
			}
			
			SetHandleInformation(output_rh, HANDLE_FLAG_INHERIT, FALSE);
			
			STARTUPINFO si;
			memset(&si, 0, sizeof(si));
			
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdOutput = output_wh;
			si.hStdError = output_wh;
			
			PROCESS_INFORMATION pi;
			
			char cmdline[256];
			snprintf(cmdline, sizeof(cmdline), "\"%s\" \"%s\"", argv[0], tests[i].name);
			
			if(!CreateProcess(
				argv[0],  /* lpApplicationName */
				cmdline,  /* lpCommandLine */
				NULL,     /* lpProcessAttributes */
				NULL,     /* lpThreadAttributes */
				TRUE,     /* bInheritHandles */
				0,        /* dwCreationFlags */
				NULL,     /* lpEnvironment */
				NULL,     /* lpCurrentDirectory */
				&si,      /* lpStartupInfo */
				&pi))     /* lpProcessInformation */
			{
				DWORD error = GetLastError();
				printf("CreateProcess failed with error code %lu\n", error);
				
				CloseHandle(output_wh);
				CloseHandle(output_rh);
				
				++failures;
				continue;
			}
			
			CloseHandle(pi.hThread);
			
			CloseHandle(output_wh);
			
			WaitForSingleObject(pi.hProcess, INFINITE);
			CloseHandle(pi.hProcess);
			
			std::string output;
			
			char output_buf[1024];
			DWORD output_count;
			
			while(ReadFile(output_rh, output_buf, sizeof(output_buf), &output_count, NULL))
			{
				output += std::string(output_buf, output_count);
			}
			
			DWORD read_error = GetLastError();
			if(read_error != ERROR_BROKEN_PIPE)
			{
				printf("ReadFile failed with error code %lu\n", read_error);
				
				CloseHandle(output_rh);
				
				++failures;
				continue;
			}
			
			CloseHandle(output_rh);
			
			if(output == std::string(tests[i].reference))
			{
				printf("PASSED\n");
			}
			else{
				printf("FAILED\n");
				printf("Expected output:\n%s\n\n", tests[i].reference);
				printf("Actual output:\n%s\n\n", output.c_str());
				
				++failures;
			}
		}
		
		if(failures > 0)
		{
			printf("%d failed tests\n", failures);
			return 1;
		}
		else{
			printf("All tests passed!\n");
			return 0;
		}
	}
	else if(argc == 2)
	{
		for(int i = 0; i < num_tests; ++i)
		{
			if(strcmp(tests[i].name, argv[1]) == 0)
			{
				return tests[i].impl();
			}
		}
		
		fprintf(stderr, "Unknown test %s\n", argv[1]);
		return 1;
	}
	else{
		fprintf(stderr, "Usage: %s [<test name>]\n", argv[0]);
		return 1;
	}
}
