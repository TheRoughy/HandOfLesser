#include "high_resolution_timer.h"

#include "windows_utils.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <thread>

namespace HOL
{
	bool HighResolutionTimer::configureProcessTiming()
	{
		PROCESS_POWER_THROTTLING_STATE throttlingState{};
		throttlingState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
		// Taking control of these policies with a cleared state mask keeps Windows from
		// reducing execution speed or timer precision when the application is occluded.
		throttlingState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED
								  | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
		throttlingState.StateMask = 0;

		if (!SetProcessInformation(GetCurrentProcess(),
							   ProcessPowerThrottling,
							   &throttlingState,
							   sizeof(throttlingState)))
		{
			std::cerr << "Failed to disable process power throttling: "
					  << FormatWindowsError(GetLastError()) << std::endl;
			return false;
		}

		return true;
	}

	HighResolutionTimer::HighResolutionTimer() : mNextDeadline(std::chrono::steady_clock::now())
	{
		mTimer = CreateWaitableTimerExW(nullptr,
								  nullptr,
								  CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
								  TIMER_MODIFY_STATE | SYNCHRONIZE);
		if (mTimer == nullptr)
		{
			std::cerr << "Failed to create high-resolution timer, using standard sleeps: "
					  << FormatWindowsError(GetLastError()) << std::endl;
		}
	}

	HighResolutionTimer::~HighResolutionTimer()
	{
		if (mTimer != nullptr)
		{
			CloseHandle(mTimer);
		}
	}

	void HighResolutionTimer::waitForInterval(std::chrono::milliseconds interval)
	{
		if (interval <= std::chrono::milliseconds::zero())
		{
			return;
		}

		mNextDeadline += interval;
		const auto now = std::chrono::steady_clock::now();
		if (mNextDeadline <= now)
		{
			// Preserve the cadence without running rapid catch-up iterations after an overrun.
			const auto missedIntervals = (now - mNextDeadline) / interval + 1;
			mNextDeadline += interval * missedIntervals;
		}

		waitUntil(mNextDeadline);
	}

	void HighResolutionTimer::waitUntil(std::chrono::steady_clock::time_point deadline)
	{
		const auto now = std::chrono::steady_clock::now();
		if (deadline <= now)
		{
			return;
		}

		if (mTimer == nullptr)
		{
			std::this_thread::sleep_until(deadline);
			return;
		}

		using HundredNanoseconds = std::chrono::duration<int64_t, std::ratio<1, 10000000>>;
		const int64_t remainingTicks
			= std::chrono::ceil<HundredNanoseconds>(deadline - now).count();

		LARGE_INTEGER dueTime{};
		dueTime.QuadPart = -std::max<int64_t>(remainingTicks, 1);
		if (!SetWaitableTimerEx(mTimer, &dueTime, 0, nullptr, nullptr, nullptr, 0))
		{
			disableTimer("set high-resolution timer", GetLastError());
			std::this_thread::sleep_until(deadline);
			return;
		}

		const DWORD waitResult = WaitForSingleObject(mTimer, INFINITE);
		if (waitResult != WAIT_OBJECT_0)
		{
			const DWORD error = waitResult == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
			disableTimer("wait for high-resolution timer", error);
			std::this_thread::sleep_until(deadline);
		}
	}

	void HighResolutionTimer::disableTimer(const char* operation, DWORD error)
	{
		std::cerr << "Failed to " << operation << ", using standard sleeps: "
				  << FormatWindowsError(error) << std::endl;
		CloseHandle(mTimer);
		mTimer = nullptr;
	}
} // namespace HOL
