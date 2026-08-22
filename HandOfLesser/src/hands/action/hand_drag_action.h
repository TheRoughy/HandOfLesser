#pragma once

#include "base_action.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <HandOfLesserCommon.h>
#include "src/openxr/XrUtils.h"

namespace HOL
{
	class HandDragAction : public BaseAction, public std::enable_shared_from_this<HandDragAction>
	{
	public:
		HandDragAction() : BaseAction({InputType::XAxis, InputType::ZAxis, InputType::Touch}){};
		static std::shared_ptr<HandDragAction> Create()
		{
			return std::make_shared<HandDragAction>();
		}

		// Locally defined function for initializing action.
		// In addition to this all actions should accept a map of values
		// accepting the same arguments ( TODO )
		std::shared_ptr<HandDragAction>
		setup(HandSide side, XrHandJointEXT joint, float sensitivity = 1.0f);
		void onEvaluate(GestureData gestureData, ActionData actionData) override;

	private:
		Eigen::Vector3f mStartPosition = Eigen::Vector3f(0, 0, 0);
		HandSide mHandSide = HandSide::LeftHand;
		XrHandJointEXT mTargetJoint = XrHandJointEXT::XR_HAND_JOINT_THUMB_TIP_EXT;
		float mSensitivity = 1.0f;
		static constexpr float BaseMultiplier = 5.0f;
		
		std::shared_ptr<BaseInput<float>> mAxisInputX;
		std::shared_ptr<BaseInput<float>> mAxisInputY;
	};

} // namespace HOL
