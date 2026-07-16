#pragma once

#include <d3d11.h>
#include <openxr/openxr.h>

#include <cstdint>

namespace HOL
{

	// OpenXR-shaped hand data from either the runtime or an external source such as SteamVR.
	// A generation lets external sources identify updates without comparing transformed poses.
	struct HandTrackingSample
	{
		bool active = false;
		bool hasUpdateGeneration = false;
		uint64_t updateGeneration = 0;
		XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT]{};
		XrHandJointVelocityEXT velocities[XR_HAND_JOINT_COUNT_EXT]{};
		XrHandTrackingAimStateFB aimState{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
		XrHandTrackingDataSourceStateEXT dataSourceState{
			XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
	};
} // namespace HOL
