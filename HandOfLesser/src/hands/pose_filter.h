#pragma once

#include <HandOfLesserCommon.h>
#include <cstdint>

namespace HOL
{
	struct PoseFilterParameters
	{
		float positionSmoothingMS = 0.0f;
		float rotationSmoothingMS = 0.0f;
		float positionJitterRadiusMM = 0.0f;
		float rotationJitterRadiusDegrees = 0.0f;
		// Position and rotation may need to discard their history on different state changes.
		bool resetPosition = false;
		bool resetRotation = false;
		bool applyJitter = true;
	};

	struct PoseFilterResult
	{
		PoseLocation pose;
		PoseVelocity velocity;
	};

	// Stateful filter; each tracked pose needs its own instance and history.
	class PoseFilter
	{
	public:
		PoseFilterResult update(const PoseLocation& pose,
								const PoseVelocity& velocity,
								int64_t sampleTimeNanoseconds,
								const PoseFilterParameters& parameters);
		void reset();

	private:
		static bool getSampleDeltaSeconds(int64_t previousSampleTime,
										  int64_t currentSampleTime,
										  float& sampleDeltaSeconds);
		static float getSmoothingAlpha(float smoothingMS,
									   bool hasPreviousSample,
									   bool reset,
									   int64_t previousSampleTime,
									   int64_t currentSampleTime);
		static Eigen::Vector3f reducePositionJitter(const Eigen::Vector3f& measuredPosition,
													const Eigen::Vector3f& filteredPosition,
													const Eigen::Vector3f& filteredVelocity,
													float jitterRadiusMM,
													float sampleDeltaSeconds);
		static Eigen::Quaternionf
		reduceRotationJitter(const Eigen::Quaternionf& measuredOrientation,
							 const Eigen::Quaternionf& filteredOrientation,
							 const Eigen::Vector3f& filteredAngularVelocity,
							 float jitterRadiusDegrees,
							 float sampleDeltaSeconds);

		PoseLocation mFilteredPose{};
		PoseVelocity mFilteredVelocity{};
		int64_t mPreviousSampleTime = 0;
		bool mHasSample = false;
	};
} // namespace HOL
