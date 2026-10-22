#pragma once

#include <HandOfLesserCommon.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <atomic>
#include <chrono>

namespace HOL
{
	struct BodyTrackingDisplay
	{
		float confidence = 0;
		PoseLocation headPose;
		bool headPoseValid = false;
		std::atomic<bool> fallbackTrackerValid[HOL::HandSide_MAX]{};
		std::atomic<bool> trackerCalibrationReady[HOL::HandSide_MAX]{};
	};

	struct DriverStatusDisplay
	{
		bool emulatedControllersActive = false;
		bool hasNormalControllers = false;
		bool hasHandTrackingControllers = false;
		int hookedControllerCount = 0;
		int emulatedTrackerCount = 0;
	};

	struct HandTransformDisplay
	{
		bool active;
		bool positionValid;
		bool positionTracked;
		XrHandTrackingDataSourceEXT dataSource = XR_HAND_TRACKING_DATA_SOURCE_MAX_ENUM_EXT;
		int trackedJointCount = 0;
		std::atomic<float> updateRateMS = 0.0f;
		Eigen::Vector3f finalTranslationOffset;
		Eigen::Vector3f finalOrientationOffset; // In degrees
		PoseLocation rawPose;
		PoseLocation finalPose;
	};

	struct FingerTrackingDisplay
	{
		FingerBend rawBend[FingerType::FingerType_MAX];
		FingerBend humanoidBend[FingerType::FingerType_MAX];
		FingerBend packedBend[FingerType::FingerType_MAX]; // reusing for int values
	};

	namespace display
	{
		extern FingerTrackingDisplay FingerTracking[2];
		extern HandTransformDisplay HandTransform[2];
		extern BodyTrackingDisplay BodyTracking;
		extern DriverStatusDisplay DriverStatus;
		void updateTrackingRate(std::chrono::steady_clock::time_point& lastUpdateTime,
								std::atomic<float>& updateRateMS);
	} // namespace display
} // namespace HOL
