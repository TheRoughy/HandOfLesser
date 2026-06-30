#pragma once

#include <chrono>
#include <cstdint>

namespace HOL
{
	int64_t steadyNowMS();
	std::chrono::milliseconds timeSince(std::chrono::steady_clock::time_point time);
}
