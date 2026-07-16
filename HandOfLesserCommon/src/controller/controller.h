#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <src/hand/hand.h>
#include "openvr_driver.h"

#include <d3d11.h>
#include <openxr/openxr.h>

namespace HOL
{
	enum EmulatedControllerProfile
	{
		EmulatedControllerProfile_Index,
		EmulatedControllerProfile_OculusTouch,
		EmulatedControllerProfile_MAX
	};

	enum EmulatedControllerVariant
	{
		EmulatedControllerVariant_IndexPartial,
		EmulatedControllerVariant_IndexFull,
		EmulatedControllerVariant_OculusTouchPartial,
		EmulatedControllerVariant_OculusTouchFull,
		EmulatedControllerVariant_MAX
	};

	inline EmulatedControllerVariant getEmulatedControllerVariant(
		EmulatedControllerProfile profile,
		vr::EVRSkeletalTrackingLevel trackingLevel)
	{
		bool fullTracking = trackingLevel == vr::VRSkeletalTracking_Full;

		if (profile == EmulatedControllerProfile::EmulatedControllerProfile_OculusTouch)
		{
			return fullTracking ? EmulatedControllerVariant_OculusTouchFull
								: EmulatedControllerVariant_OculusTouchPartial;
		}

		return fullTracking ? EmulatedControllerVariant_IndexFull
							: EmulatedControllerVariant_IndexPartial;
	}

	// TODO: need to be able to save these
	enum ControllerOffsetPreset
	{
		ZERO,
		RoughyVRChatHand,
		ControllerOffsetPreset_MAX
	};

	enum ControllerMode
	{
		NoControllerMode,
		EmulateControllerMode,
		HookedControllerMode,
		ControllerMode_MAX
	};

	enum PossessionBehavior
	{
		PossessionBehavior_Full,
		PossessionBehavior_Fallback,
		PossessionBehavior_Input,
		PossessionBehavior_MAX
	};

	enum class BodyTrackerRole : int
	{
		Hips = 0,
		Chest = 1,
		LeftUpperArm = 2,
		LeftLowerArm = 3,
		RightUpperArm = 4,
		RightLowerArm = 5,
		TrackerRole_MAX = 6
	};

	PoseLocationEuler getControllerBaseOffset();
	PoseLocation getControllerPoseOffset(HandSide side,
									 bool applyBaseOffset,
									 Eigen::Vector3f userTranslationOffset,
									 Eigen::Vector3f userRotationOffset);

	PoseLocationEuler getControllerOffsetPreset(ControllerOffsetPreset type);

	// Body tracker helper functions
	const char* bodyTrackerRoleToString(BodyTrackerRole role);
	const char* bodyTrackerRoleToSerial(BodyTrackerRole role);
	XrBodyJointFB bodyTrackerRoleToJoint(BodyTrackerRole role);
	const char* bodyTrackerRoleToTrackerRoleString(BodyTrackerRole role);
} // namespace HOL
