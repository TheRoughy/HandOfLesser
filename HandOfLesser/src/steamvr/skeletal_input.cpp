#pragma once

#include "skeletal_input.h"
#include "src/openxr/xr_joint_utils.h"
#include <src/core/ui/user_interface.h>

#include "src/openxr/XrUtils.h"
#include <src/core/settings_global.h>

namespace HOL::SteamVR
{
	void getOpenXRJointLocation(XrHandJointLocationEXT* openXRJoints,
								HandSkeletonBone ovrBone,
								HOL::PoseLocation& out)
	{
		XrHandJointLocationEXT& joint = openXRJoints[skeletalBoneToOpenXRJoint(ovrBone)];

		out.position = HOL::OpenXR::getJointPosition(joint);
		out.orientation = HOL::OpenXR::getJointOrientation(joint);

		return;
	}

	HOL::SkeletalPayload& SkeletalInput::getSkeletalPayload(OpenXRHand* hand, HandSide side)
	{
		// Root is where the controller origin is.
		// Wrist should be the offset from that to the wrist location.
		// The rest of the children should be in the local space of their parent.
		// In the example +x is foward, and Y is reversed for the right hand.
		// Honestly I just flipped numbers until something worked.
		// Following that the metacarpals need to be rotated AND? rolled by 90 degrees
		// from the wrist, because of some legacy FBX compatibility nonsense.
		// I don't know. I don't understand. I don't care anymore. It works.

		const HandSkeletonBone firstJoint[5] = {
			eBone_Thumb0,		 // thumb
			eBone_IndexFinger0,	 // index
			eBone_MiddleFinger0, // middle
			eBone_RingFinger0,	 // ring
			eBone_PinkyFinger0,	 // pinky
		};

		const float fingerJointCount[5] = {
			4, // thumb
			5, // index
			5, // middle
			5, // ring
			5, // pinky
		};

		// Might as well write directly to the payload
		SkeletalPayload& payload
			= side == HandSide::LeftHand ? this->mLeftPayload : this->mRightPayload;

		// Convert all the raw locations to eigen. Aux joints are generated on the driver side.
		for (int i = 1; i < HandSkeletonBone::eBone_Aux_Thumb; i++)
		{
			getOpenXRJointLocation(
				hand->getLastJointLocations(), (HandSkeletonBone)i, payload.locations[i]);
		}

		HOL::PoseLocation& rootJoint = payload.locations[HandSkeletonBone::eBone_Root];
		HOL::PoseLocation& wristJoint = payload.locations[HandSkeletonBone::eBone_Wrist];

		// root should be 0
		rootJoint.position.setZero();
		rootJoint.orientation.setIdentity();

		for (int i = 0; i < 5; i++)
		{
			int jointCount = fingerJointCount[i];
			HandSkeletonBone lastJoint = (HandSkeletonBone)(firstJoint[i] + jointCount - 1);

			for (int j = 0; j < jointCount; j++)
			{
				// Start from tip joint. If last iteration then use wrist as parent, otherwise -1
				// index.
				bool lastIteration = j == jointCount - 1;
				HandSkeletonBone currentJoint = (HandSkeletonBone)(lastJoint - j);
				PoseLocation& joint = payload.locations[currentJoint];
				PoseLocation& parent
					= lastIteration ? wristJoint : payload.locations[currentJoint - 1];

				joint.position = parent.orientation.inverse() * (joint.position - parent.position);
				joint.orientation = parent.orientation.inverse() * joint.orientation;
			}
		}

		// OpenVR seems to assume all joints have a specific length, probably because they
		// never accounted for actual handtracking, only esimated finger-positions
		// based on which buttons on a controller your fingers are touching.
		// OpenXR will still be slightly broken because their palm bone is in the wrong
		// location, but nothing we can do about that.

		// In my case they're a bit short, so let's stretch them a bit.
		if (Config.skeletal.jointLengthMultiplier != 1.0f)
		{
			for (int i = 2; i < HandSkeletonBone::eBone_Aux_Thumb; i++)
			{
				HOL::PoseLocation& joint = payload.locations[(HandSkeletonBone)i];
				joint.position *= Config.skeletal.jointLengthMultiplier;
			}
		}

		// Wrist should be offset from the raw controller pose to the wrist
		// emphasis on should.
		wristJoint.position = Config.skeletal.positionOffset;
		wristJoint.orientation
			= HOL::quaternionFromEulerAnglesDegrees(Config.skeletal.orientationOffset);

		if (side == HandSide::RightHand)
		{
			// Flip along x axix
			wristJoint.position.x() *= -1.f;
			wristJoint.orientation.y() *= -1.f;
			wristJoint.orientation.z() *= -1.f;
		}

		// Set the side and we're done.
		// The rest of the weird transforms are handled on the driver side,
		// so you can submit without having to think too much about it.

		// also TODO the aux joints
		payload.side = side;

		return payload;
	}
} // namespace HOL::SteamVR
