#pragma once

#include <chrono>
#include <Windows.h>

namespace HOL
{
	class HighResolutionTimer
	{
	public:
		static bool configureProcessTiming();

		HighResolutionTimer();
		~HighResolutionTimer();

		HighResolutionTimer(const HighResolutionTimer&) = delete;
		HighResolutionTimer& operator=(const HighResolutionTimer&) = delete;

		void waitForInterval(std::chrono::milliseconds interval);

	private:
		void waitUntil(std::chrono::steady_clock::time_point deadline);
		void disableTimer(const char* operation, DWORD error);

		HANDLE mTimer = nullptr;
		std::chrono::steady_clock::time_point mNextDeadline;
	};
} // namespace HOL
