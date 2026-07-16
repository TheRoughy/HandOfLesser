#pragma once

#include <HandOfLesserCommon.h>
#include "src/openxr/openxr_hand.h"

#include <openvr_driver.h>

#include <d3d11.h> // Why do you need this??
#include <openxr/openxr_platform.h>
#include <openxr/openxr.hpp>

namespace HOL::SteamVR
{
	void getOpenXRJointLocation(XrHandJointLocationEXT* openXRJoints,
								HandSkeletonBone ovrBone,
								HOL::PoseLocation& out);

	class SkeletalInput
	{
	public:
		HOL::SkeletalPayload& getSkeletalPayload(OpenXRHand* hand, HandSide side);

	private:
		// Don't really need to separate them but eh
		HOL::SkeletalPayload mLeftPayload;
		HOL::SkeletalPayload mRightPayload;
	};
} // namespace HOL::SteamVR
