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
		EmulatedControllerProfile_SteamLinkHandTouch,
		EmulatedControllerProfile_SteamLinkHandNative,
		EmulatedControllerProfile_MAX
	};

	enum EmulatedControllerVariant
	{
		EmulatedControllerVariant_IndexPartial,
		EmulatedControllerVariant_IndexFull,
		EmulatedControllerVariant_OculusTouchPartial,
		EmulatedControllerVariant_OculusTouchFull,
		EmulatedControllerVariant_SteamLinkHandTouchPartial,
		EmulatedControllerVariant_SteamLinkHandTouchFull,
		EmulatedControllerVariant_SteamLinkHandNative,
		EmulatedControllerVariant_MAX
	};

	inline bool isSteamLinkHandProfile(EmulatedControllerProfile profile)
	{
		return profile == EmulatedControllerProfile_SteamLinkHandTouch
			   || profile == EmulatedControllerProfile_SteamLinkHandNative;
	}

	inline EmulatedControllerVariant
	getEmulatedControllerVariant(EmulatedControllerProfile profile,
								 vr::EVRSkeletalTrackingLevel trackingLevel)
	{
		bool fullTracking = trackingLevel == vr::VRSkeletalTracking_Full;

		if (profile == EmulatedControllerProfile::EmulatedControllerProfile_OculusTouch)
		{
			return fullTracking ? EmulatedControllerVariant_OculusTouchFull
								: EmulatedControllerVariant_OculusTouchPartial;
		}
		if (profile == EmulatedControllerProfile::EmulatedControllerProfile_SteamLinkHandTouch)
		{
			return fullTracking ? EmulatedControllerVariant_SteamLinkHandTouchFull
								: EmulatedControllerVariant_SteamLinkHandTouchPartial;
		}
		if (profile == EmulatedControllerProfile::EmulatedControllerProfile_SteamLinkHandNative)
		{
			// Steam Link's native hand profile only advertises full skeletal tracking.
			return EmulatedControllerVariant_SteamLinkHandNative;
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
