#include "proximity_gesture.h"

#include <Eigen/Core>
#include <Eigen/Geometry>
#include "src/openxr/xr_joint_utils.h"

namespace HOL::Gesture
{
	float ProximityGesture::evaluateInternal(GestureData data)
	{
		float distance = 0.0f;

		if (this->mUseLineDistance)
		{
			auto line1Start
				= OpenXR::getJointPosition(data.joints[this->mSide1], this->mJoint1Start);
			auto line1End
				= OpenXR::getJointPosition(data.joints[this->mSide1], this->mJoint1);
			auto line2Start
				= OpenXR::getJointPosition(data.joints[this->mSide2], this->mJoint2Start);
			auto line2End
				= OpenXR::getJointPosition(data.joints[this->mSide2], this->mJoint2);

			distance = getClosestSegmentDistance(line1Start, line1End, line2Start, line2End);
		}
		else
		{
			auto pos1 = OpenXR::getJointPosition(data.joints[this->mSide1], this->mJoint1);
			auto pos2 = OpenXR::getJointPosition(data.joints[this->mSide2], this->mJoint2);
			distance = (pos1 - pos2).norm();
		}

		float distanceRange = this->mMaxDistance - this->mMinDistance;
		distance -= this->mMinDistance;
		distance = std::clamp(distance, 0.0f, distanceRange);
		distance /= distanceRange;
		distance = 1.f - distance;

		return distance;
	}

	void ProximityGesture::setup(HOL::FingerType fingerTip1,
								 HOL::HandSide side1,
								 HOL::FingerType fingerTip2,
								 HOL::HandSide side2,
								 float minDistance,
								 float maxDistance)
	{
		this->name = "ProximityGesture";

		this->mJoint1 = OpenXR::getFingerTip(fingerTip1);
		this->mJoint1Start = static_cast<XrHandJointEXT>(this->mJoint1 - 1);
		this->mSide1 = side1;
		this->mJoint2 = OpenXR::getFingerTip(fingerTip2);
		this->mJoint2Start = static_cast<XrHandJointEXT>(this->mJoint2 - 1);
		this->mSide2 = side2;
		this->mUseLineDistance = true;
		this->mMinDistance = minDistance;
		this->mMaxDistance = maxDistance;
	}

	void ProximityGesture::setup(XrHandJointEXT joint1,
								 HOL::HandSide side1,
								 XrHandJointEXT joint2,
								 HOL::HandSide side2,
								 float minDistance,
								 float maxDistance)
	{
		this->name = "ProximityGesture";

		this->mJoint1 = joint1;
		this->mJoint1Start = joint1;
		this->mSide1 = side1;
		this->mJoint2 = joint2;
		this->mJoint2Start = joint2;
		this->mSide2 = side2;
		this->mUseLineDistance = false;
		this->mMinDistance = minDistance;
		this->mMaxDistance = maxDistance;
	}

	void ProximityGesture::setup(HOL::FingerType fingerTip1, HOL::HandSide side1)
	{
		setup(fingerTip1, side1, HOL::FingerType::FingerThumb, side1);
	}

	void ProximityGesture::setup(
		HOL::FingerType fingerTip1, HOL::HandSide side1, float minDistance)
	{
		setup(fingerTip1, side1, HOL::FingerType::FingerThumb, side1, minDistance);
	}

}

