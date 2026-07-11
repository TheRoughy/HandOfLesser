#include "facing_gesture.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "src/openxr/XrUtils.h"

namespace HOL::Gesture::FacingGesture
{
	namespace
	{
		bool tryGetBodyJointPose(const XrBodyJointLocationFB* bodyJoints,
								XrBodyJointFB jointIndex,
								Eigen::Vector3f& position,
								Eigen::Quaternionf& orientation)
		{
			if (bodyJoints == nullptr)
			{
				return false;
			}

			const auto& joint = bodyJoints[jointIndex];
			const bool positionValid
				= (joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
			const bool orientationValid
				= (joint.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
			if (!positionValid || !orientationValid)
			{
				return false;
			}

			position = OpenXR::toEigenVector(joint.pose.position);
			orientation = OpenXR::toEigenQuaternion(joint.pose.orientation);
			return true;
		}
	}

	float Gesture::evaluateInternal(GestureData data)
	{
		if (this->parameters.side < HOL::LeftHand || this->parameters.side >= HOL::HandSide_MAX)
		{
			return 0.0f;
		}

		const HOL::HandPose* handPose = data.handPose[this->parameters.side];
		if (handPose == nullptr || !handPose->poseValid)
		{
			return 0.0f;
		}

		Eigen::Vector3f sourcePosition = Eigen::Vector3f::Zero();
		Eigen::Quaternionf sourceOrientation = Eigen::Quaternionf::Identity();
		switch (this->parameters.source)
		{
			case Source::Head:
				if (!tryGetBodyJointPose(
						data.bodyJoints, XR_BODY_JOINT_HEAD_FB, sourcePosition, sourceOrientation))
				{
					return 0.0f;
				}
				break;

			case Source::Chest:
				if (!tryGetBodyJointPose(
						data.bodyJoints, XR_BODY_JOINT_CHEST_FB, sourcePosition, sourceOrientation))
				{
					return 0.0f;
				}
				break;

			case Source::Palm:
				sourcePosition = handPose->palmLocation.position;
				sourceOrientation = handPose->palmLocation.orientation;
				break;
		}

		Eigen::Vector3f targetPosition = Eigen::Vector3f::Zero();
		switch (this->parameters.target)
		{
			case Target::HandPalm:
				targetPosition = handPose->palmLocation.position;
				break;

			case Target::Head:
			{
				Eigen::Quaternionf unusedOrientation;
				if (!tryGetBodyJointPose(
						data.bodyJoints, XR_BODY_JOINT_HEAD_FB, targetPosition, unusedOrientation))
				{
					return 0.0f;
				}
				break;
			}
		}

		Eigen::Vector3f toTarget = targetPosition - sourcePosition;
		float targetDistanceSquared = toTarget.squaredNorm();
		if (targetDistanceSquared <= 0.000001f)
		{
			return 0.0f;
		}

		Eigen::Vector3f facingDirection = sourceOrientation * this->parameters.localForward;
		if (facingDirection.squaredNorm() <= 0.000001f)
		{
			return 0.0f;
		}

		facingDirection.normalize();
		toTarget.normalize();

		float clampedFovDegrees = std::clamp(this->parameters.fovDegrees, 1.0f, 179.0f);
		float activationAngleRadians
			= clampedFovDegrees * 0.5f * (std::numbers::pi_v<float> / 180.0f);
		float actualDot = std::clamp(facingDirection.dot(toTarget), -1.0f, 1.0f);
		float actualAngleRadians = std::acos(actualDot);

		if (actualAngleRadians <= activationAngleRadians)
		{
			return 1.0f;
		}

		float fadeRangeRadians = std::numbers::pi_v<float> - activationAngleRadians;
		if (fadeRangeRadians <= 0.000001f)
		{
			return 0.0f;
		}

		float normalized
			= 1.0f - ((actualAngleRadians - activationAngleRadians) / fadeRangeRadians);
		return std::clamp(normalized, 0.0f, 1.0f);
	}
} // namespace HOL::Gesture::FacingGesture
