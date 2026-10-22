#pragma once

#include <HandOfLesserCommon.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <optional>
namespace HOL
{
	class HandPose
	{
	public:
		FingerBend fingers[FingerType::FingerType_MAX];

		bool active;
		bool poseValid;
		bool poseTracked;
		bool poseStale;
		// Direct, currently tracked palm before fallback and filtering. Calibration must never
		// measure the tracker-driven output against itself, including while always-on is enabled.
		std::optional<HOL::PoseLocation> trackedPalmLocation;

		// Raw palm-space values from the runtime.
		// These are used for SteamVR controller pose generation so the driver can apply the
		// controller offset through DriverFromHead instead of baking it into the sensor pose.
		HOL::PoseLocation palmLocation;
		HOL::PoseVelocity palmVelocity;

		// Final controller-aligned pose after base + user offsets have been applied.
		HOL::PoseLocation controllerLocation;
		HOL::PoseVelocity controllerVelocity;

		// offset to match Index tracker location
		HOL::PoseLocation trackerLocation;
	};
} // namespace HOL
