//============ Copyright (c) Valve Corporation, All rights reserved. ============
#pragma once

#include "openvr_driver.h"
#include <openxr/openxr.h>

namespace HOL::SteamVR
{
	// 0-1 values (1 fully curled)
	struct MyFingerCurls
	{
		float thumb;
		float index;
		float middle;
		float ring;
		float pinky;
	};

	//-1-1 values (1 fully to the left)
	struct MyFingerSplays
	{
		float thumb;
		float index;
		float middle;
		float ring;
		float pinky;
	};

	enum HandSkeletonBone : vr::BoneIndex_t
	{
		eBone_Root = 0,
		eBone_Wrist,
		eBone_Thumb0,
		eBone_Thumb1,
		eBone_Thumb2,
		eBone_Thumb3,
		eBone_IndexFinger0,
		eBone_IndexFinger1,
		eBone_IndexFinger2,
		eBone_IndexFinger3,
		eBone_IndexFinger4,
		eBone_MiddleFinger0,
		eBone_MiddleFinger1,
		eBone_MiddleFinger2,
		eBone_MiddleFinger3,
		eBone_MiddleFinger4,
		eBone_RingFinger0,
		eBone_RingFinger1,
		eBone_RingFinger2,
		eBone_RingFinger3,
		eBone_RingFinger4,
		eBone_PinkyFinger0,
		eBone_PinkyFinger1,
		eBone_PinkyFinger2,
		eBone_PinkyFinger3,
		eBone_PinkyFinger4,
		eBone_Aux_Thumb,
		eBone_Aux_IndexFinger,
		eBone_Aux_MiddleFinger,
		eBone_Aux_RingFinger,
		eBone_Aux_PinkyFinger,
		eBone_Count
	};

	inline XrHandJointEXT skeletalBoneToOpenXRJoint(HandSkeletonBone bone)
	{
		switch (bone)
		{
			case eBone_Wrist: return XR_HAND_JOINT_WRIST_EXT;
			case eBone_Thumb0: return XR_HAND_JOINT_THUMB_METACARPAL_EXT;
			case eBone_Thumb1: return XR_HAND_JOINT_THUMB_PROXIMAL_EXT;
			case eBone_Thumb2: return XR_HAND_JOINT_THUMB_DISTAL_EXT;
			case eBone_Thumb3: return XR_HAND_JOINT_THUMB_TIP_EXT;
			case eBone_IndexFinger0: return XR_HAND_JOINT_INDEX_METACARPAL_EXT;
			case eBone_IndexFinger1: return XR_HAND_JOINT_INDEX_PROXIMAL_EXT;
			case eBone_IndexFinger2: return XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
			case eBone_IndexFinger3: return XR_HAND_JOINT_INDEX_DISTAL_EXT;
			case eBone_IndexFinger4: return XR_HAND_JOINT_INDEX_TIP_EXT;
			case eBone_MiddleFinger0: return XR_HAND_JOINT_MIDDLE_METACARPAL_EXT;
			case eBone_MiddleFinger1: return XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT;
			case eBone_MiddleFinger2: return XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT;
			case eBone_MiddleFinger3: return XR_HAND_JOINT_MIDDLE_DISTAL_EXT;
			case eBone_MiddleFinger4: return XR_HAND_JOINT_MIDDLE_TIP_EXT;
			case eBone_RingFinger0: return XR_HAND_JOINT_RING_METACARPAL_EXT;
			case eBone_RingFinger1: return XR_HAND_JOINT_RING_PROXIMAL_EXT;
			case eBone_RingFinger2: return XR_HAND_JOINT_RING_INTERMEDIATE_EXT;
			case eBone_RingFinger3: return XR_HAND_JOINT_RING_DISTAL_EXT;
			case eBone_RingFinger4: return XR_HAND_JOINT_RING_TIP_EXT;
			case eBone_PinkyFinger0: return XR_HAND_JOINT_LITTLE_METACARPAL_EXT;
			case eBone_PinkyFinger1: return XR_HAND_JOINT_LITTLE_PROXIMAL_EXT;
			case eBone_PinkyFinger2: return XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT;
			case eBone_PinkyFinger3: return XR_HAND_JOINT_LITTLE_DISTAL_EXT;
			case eBone_PinkyFinger4: return XR_HAND_JOINT_LITTLE_TIP_EXT;
			default: return XR_HAND_JOINT_MAX_ENUM_EXT;
		}
	}

}

