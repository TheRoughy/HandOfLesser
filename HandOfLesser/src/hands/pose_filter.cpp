#include "pose_filter.h"

#include <algorithm>
#include <cmath>

#include <Eigen/Geometry>

namespace HOL
{
	namespace
	{
		// Tracking history is no longer representative after a long update gap.
		constexpr float MaxSampleGapSeconds = 0.25f;
	} // namespace

	bool PoseFilter::getSampleDeltaSeconds(int64_t previousSampleTime,
										   int64_t currentSampleTime,
										   float& sampleDeltaSeconds)
	{
		if (previousSampleTime <= 0 || currentSampleTime <= previousSampleTime)
		{
			return false;
		}

		sampleDeltaSeconds = (float)(currentSampleTime - previousSampleTime) / 1000000000.0f;
		return sampleDeltaSeconds < MaxSampleGapSeconds;
	}

	float PoseFilter::getSmoothingAlpha(float smoothingMS,
										bool hasPreviousSample,
										bool reset,
										int64_t previousSampleTime,
										int64_t currentSampleTime)
	{
		if (!hasPreviousSample || reset || previousSampleTime <= 0
			|| currentSampleTime <= previousSampleTime)
		{
			return 1.0f;
		}

		const float smoothingTimeSeconds = smoothingMS / 1000.0f;
		if (smoothingTimeSeconds <= 0.0f)
		{
			return 1.0f;
		}

		float sampleDeltaSeconds = 0.0f;
		if (!getSampleDeltaSeconds(previousSampleTime, currentSampleTime, sampleDeltaSeconds))
		{
			return 1.0f;
		}

		// Exponential smoothing keeps the effective time constant independent of update cadence.
		return 1.0f - std::exp(-sampleDeltaSeconds / smoothingTimeSeconds);
	}

	Eigen::Vector3f PoseFilter::reducePositionJitter(const Eigen::Vector3f& measuredPosition,
													 const Eigen::Vector3f& filteredPosition,
													 const Eigen::Vector3f& filteredVelocity,
													 float jitterRadiusMM,
													 float sampleDeltaSeconds)
	{
		const float jitterRadiusMeters = jitterRadiusMM / 1000.0f;
		if (jitterRadiusMeters <= 0.0f)
		{
			return measuredPosition;
		}

		const Eigen::Vector3f predictedPosition
			= filteredPosition + filteredVelocity * sampleDeltaSeconds;
		const Eigen::Vector3f residual = measuredPosition - predictedPosition;
		const float residualDistance = residual.norm();
		if (residualDistance >= jitterRadiusMeters)
		{
			return measuredPosition;
		}

		// Scale small corrections instead of using a hard deadband that would make slow movement
		// stick and then jump when it crosses the configured radius.
		return predictedPosition + residual * (residualDistance / jitterRadiusMeters);
	}

	Eigen::Quaternionf
	PoseFilter::reduceRotationJitter(const Eigen::Quaternionf& measuredOrientation,
									 const Eigen::Quaternionf& filteredOrientation,
									 const Eigen::Vector3f& filteredAngularVelocity,
									 float jitterRadiusDegrees,
									 float sampleDeltaSeconds)
	{
		const float jitterRadiusRadians = HOL::degreesToRadians(jitterRadiusDegrees);
		if (jitterRadiusRadians <= 0.0f)
		{
			return measuredOrientation;
		}

		Eigen::Quaternionf predictedOrientation = filteredOrientation;
		const float angularSpeed = filteredAngularVelocity.norm();
		if (angularSpeed > 0.0f)
		{
			const Eigen::Quaternionf predictedRotation(Eigen::AngleAxisf(
				angularSpeed * sampleDeltaSeconds, filteredAngularVelocity / angularSpeed));
			// OpenXR angular velocity is expressed in the reference space.
			predictedOrientation = predictedRotation * filteredOrientation;
			predictedOrientation.normalize();
		}

		const float orientationDot
			= std::clamp(std::abs(predictedOrientation.dot(measuredOrientation)), 0.0f, 1.0f);
		const float residualAngle = 2.0f * std::acos(orientationDot);
		if (residualAngle >= jitterRadiusRadians)
		{
			return measuredOrientation;
		}

		return predictedOrientation.slerp(residualAngle / jitterRadiusRadians, measuredOrientation);
	}

	PoseFilterResult PoseFilter::update(const PoseLocation& pose,
										const PoseVelocity& velocity,
										int64_t sampleTimeNanoseconds,
										const PoseFilterParameters& parameters)
	{
		PoseLocation adjustedPose = pose;
		float sampleDeltaSeconds = 0.0f;
		// Remove small deviations from the predicted trajectory before low-pass smoothing.
		if (parameters.applyJitter && mHasSample
			&& getSampleDeltaSeconds(
				mPreviousSampleTime, sampleTimeNanoseconds, sampleDeltaSeconds))
		{
			if (!parameters.resetPosition)
			{
				adjustedPose.position = reducePositionJitter(adjustedPose.position,
															 mFilteredPose.position,
															 mFilteredVelocity.linearVelocity,
															 parameters.positionJitterRadiusMM,
															 sampleDeltaSeconds);
			}
			if (!parameters.resetRotation)
			{
				adjustedPose.orientation
					= reduceRotationJitter(adjustedPose.orientation,
										   mFilteredPose.orientation,
										   mFilteredVelocity.angularVelocity,
										   parameters.rotationJitterRadiusDegrees,
										   sampleDeltaSeconds);
			}
		}

		const float positionSmoothingAlpha = getSmoothingAlpha(parameters.positionSmoothingMS,
															   mHasSample,
															   parameters.resetPosition,
															   mPreviousSampleTime,
															   sampleTimeNanoseconds);
		const float rotationSmoothingAlpha = getSmoothingAlpha(parameters.rotationSmoothingMS,
															   mHasSample,
															   parameters.resetRotation,
															   mPreviousSampleTime,
															   sampleTimeNanoseconds);

		if (positionSmoothingAlpha >= 1.0f && rotationSmoothingAlpha >= 1.0f)
		{
			// This also initializes Eigen state on the first sample or after reset().
			mFilteredPose = adjustedPose;
			mFilteredVelocity = velocity;
		}
		else
		{
			mFilteredPose.position
				+= positionSmoothingAlpha * (adjustedPose.position - mFilteredPose.position);
			mFilteredPose.orientation
				= mFilteredPose.orientation.slerp(rotationSmoothingAlpha, adjustedPose.orientation);
			mFilteredVelocity.linearVelocity
				+= positionSmoothingAlpha
				   * (velocity.linearVelocity - mFilteredVelocity.linearVelocity);
			mFilteredVelocity.angularVelocity
				+= rotationSmoothingAlpha
				   * (velocity.angularVelocity - mFilteredVelocity.angularVelocity);
		}

		mHasSample = true;
		mPreviousSampleTime = sampleTimeNanoseconds;
		return {mFilteredPose, mFilteredVelocity};
	}

	void PoseFilter::reset()
	{
		mHasSample = false;
		mPreviousSampleTime = 0;
	}
} // namespace HOL
