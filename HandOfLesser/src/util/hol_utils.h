#pragma once

#include <chrono>
#include <cstdint>

namespace HOL
{
	class IntervalTimer
	{
	public:
		bool isDue(std::chrono::milliseconds interval);

	private:
		std::chrono::steady_clock::time_point mLastUpdate{};
	};

	int64_t steadyNowMS();
	std::chrono::milliseconds timeSince(std::chrono::steady_clock::time_point time);
}
