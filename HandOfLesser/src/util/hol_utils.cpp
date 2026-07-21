#pragma once

#include "hol_utils.h"

namespace HOL
{
	bool IntervalTimer::isDue(std::chrono::milliseconds interval)
	{
		const auto now = std::chrono::steady_clock::now();
		if (this->mLastUpdate == std::chrono::steady_clock::time_point{}
			|| now - this->mLastUpdate >= interval)
		{
			this->mLastUpdate = now;
			return true;
		}

		return false;
	}

	int64_t steadyNowMS()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	std::chrono::milliseconds timeSince(std::chrono::steady_clock::time_point time)
	{
		auto currentTime = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - time);
	}
} // namespace HOL
