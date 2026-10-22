#pragma once

#include <chrono>
#include <deque>
#include <optional>
#include <string>

namespace HOL
{
	// Compares matched hand/tracker samples without treating missing hand tracking as agreement.
	class TrackerDistanceCheck
	{
	public:
		using Clock = std::chrono::steady_clock;

		void setTracker(const std::string& serial);
		void update(float distanceMeters, float maximumDistanceMM, Clock::time_point now);
		void pause();
		void reset();
		bool isRejected() const;
		// No average is available until two valid comparisons have supplied an elapsed interval.
		std::optional<float> getAverageDistanceMM() const;

	private:
		static constexpr double WindowSeconds = 5.0;
		static constexpr double MaxSampleGapSeconds = 0.25;
		static constexpr float RecoveryDistanceRatio = 0.8f;

		struct Sample
		{
			double durationSeconds;
			float distanceMeters;
		};

		std::string mTrackerSerial;
		std::deque<Sample> mSamples;
		std::optional<Clock::time_point> mPreviousSampleTime;
		float mPreviousDistanceMeters = 0.0f;
		double mDurationSeconds = 0.0;
		double mDistanceSeconds = 0.0;
		bool mRejected = false;
	};
} // namespace HOL
