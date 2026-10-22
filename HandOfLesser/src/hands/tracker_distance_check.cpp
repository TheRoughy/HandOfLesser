#include "tracker_distance_check.h"

#include <algorithm>
#include <cmath>

namespace HOL
{
	void TrackerDistanceCheck::setTracker(const std::string& serial)
	{
		if (serial != mTrackerSerial)
		{
			mTrackerSerial = serial;
			reset();
		}
	}

	void TrackerDistanceCheck::update(float distanceMeters,
									float maximumDistanceMM,
									Clock::time_point now)
	{
		if (!std::isfinite(distanceMeters) || distanceMeters < 0.0f)
		{
			pause();
			return;
		}

		const double elapsedSeconds = mPreviousSampleTime
			? std::chrono::duration<double>(now - *mPreviousSampleTime).count() : 0.0;
		if (elapsedSeconds > 0.0 && elapsedSeconds < MaxSampleGapSeconds)
		{
			// Weight by elapsed time, not loop count, so faster polling does not change the result.
			mSamples.push_back({elapsedSeconds, mPreviousDistanceMeters});
			mDurationSeconds += elapsedSeconds;
			mDistanceSeconds += mPreviousDistanceMeters * elapsedSeconds;

			// Retain exactly the last five seconds of valid comparison time, trimming the oldest
			// interval if the window boundary falls partway through it.
			double excessSeconds = std::max(0.0, mDurationSeconds - WindowSeconds);
			while (excessSeconds > 0.0 && !mSamples.empty())
			{
				auto& oldest = mSamples.front();
				const double removedSeconds = std::min(excessSeconds, oldest.durationSeconds);
				oldest.durationSeconds -= removedSeconds;
				mDurationSeconds -= removedSeconds;
				mDistanceSeconds -= oldest.distanceMeters * removedSeconds;
				excessSeconds -= removedSeconds;
				if (oldest.durationSeconds <= 0.0)
				{
					mSamples.pop_front();
				}
			}
		}
		mPreviousSampleTime = now;
		mPreviousDistanceMeters = distanceMeters;

		// New trackers remain usable until a full window exists. Rejected trackers continue to
		// accumulate comparisons, but must come comfortably inside the limit before recovering.
		if (mDurationSeconds >= WindowSeconds - 0.000001)
		{
			const double averageMeters = mDistanceSeconds / mDurationSeconds;
			const float limitMeters = std::max(0.0f, maximumDistanceMM) / 1000.0f;
			if (mRejected)
			{
				mRejected = averageMeters > limitMeters * RecoveryDistanceRatio;
			}
			else
			{
				mRejected = averageMeters > limitMeters;
			}
		}
	}

	void TrackerDistanceCheck::pause()
	{
		// Do not count time spent without a comparison or turn it into a zero-distance sample.
		// Keep the history and verdict so losing hand tracking cannot unblock a bad tracker.
		mPreviousSampleTime.reset();
	}

	void TrackerDistanceCheck::reset()
	{
		mSamples.clear();
		mPreviousSampleTime.reset();
		mPreviousDistanceMeters = 0.0f;
		mDurationSeconds = 0.0;
		mDistanceSeconds = 0.0;
		mRejected = false;
	}

	bool TrackerDistanceCheck::isRejected() const
	{
		return mRejected;
	}

	std::optional<float> TrackerDistanceCheck::getAverageDistanceMM() const
	{
		if (mDurationSeconds <= 0.0)
		{
			return std::nullopt;
		}
		return static_cast<float>(std::max(0.0, mDistanceSeconds / mDurationSeconds) * 1000.0);
	}
} // namespace HOL
