
#include "xr_joint_utils.h"
#include "XrUtils.h"

namespace HOL::OpenXR
{
	XrBodyJointLocationFB toXrBodyJointLocation(const HOL::PoseLocation& pose)
	{
		XrBodyJointLocationFB joint{};
		joint.locationFlags
			= XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
		joint.pose.position = toXrVector(pose.position);
		joint.pose.orientation = toXrQuaternion(pose.orientation);
		return joint;
	}

	bool reconstructPalmJoint(XrHandJointLocationEXT jointLocations[],
							  XrHandJointVelocityEXT jointVelocities[])
	{
		auto& palm = jointLocations[XR_HAND_JOINT_PALM_EXT];
		const auto& metacarpal = jointLocations[XR_HAND_JOINT_MIDDLE_METACARPAL_EXT];
		const auto& proximal = jointLocations[XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT];
		const XrSpaceLocationFlags requiredFlags
			= XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
		if ((metacarpal.locationFlags & requiredFlags) != requiredFlags
			|| !(proximal.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
		{
			palm = {};
			jointVelocities[XR_HAND_JOINT_PALM_EXT] = {};
			return false;
		}

		// The palm center lies halfway along the middle metacarpal segment. Its orientation
		// follows the metacarpal, whose local axes use the same hand-joint convention.
		palm.pose.position = toXrVector(
			(toEigenVector(metacarpal.pose.position) + toEigenVector(proximal.pose.position))
			* 0.5f);
		palm.pose.orientation = metacarpal.pose.orientation;
		palm.locationFlags = metacarpal.locationFlags;

		auto& palmVelocity = jointVelocities[XR_HAND_JOINT_PALM_EXT];
		const auto& metacarpalVelocity
			= jointVelocities[XR_HAND_JOINT_MIDDLE_METACARPAL_EXT];
		const auto& proximalVelocity
			= jointVelocities[XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT];
		palmVelocity = {};
		if ((metacarpalVelocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
			&& (proximalVelocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT))
		{
			palmVelocity.linearVelocity = toXrVector(
				(toEigenVector(metacarpalVelocity.linearVelocity)
				 + toEigenVector(proximalVelocity.linearVelocity))
				* 0.5f);
			palmVelocity.velocityFlags |= XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
		}
		if (metacarpalVelocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
		{
			palmVelocity.angularVelocity = metacarpalVelocity.angularVelocity;
			palmVelocity.velocityFlags |= XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
		}
		return true;
	}

	bool reconstructPalmJoint(XrBodyJointLocationFB jointLocations[], HOL::HandSide side)
	{
		const XrBodyJointFB metacarpalJoint
			= side == HOL::LeftHand ? XR_BODY_JOINT_LEFT_HAND_MIDDLE_METACARPAL_FB
									: XR_BODY_JOINT_RIGHT_HAND_MIDDLE_METACARPAL_FB;
		const XrBodyJointFB proximalJoint
			= side == HOL::LeftHand ? XR_BODY_JOINT_LEFT_HAND_MIDDLE_PROXIMAL_FB
									: XR_BODY_JOINT_RIGHT_HAND_MIDDLE_PROXIMAL_FB;
		const XrBodyJointFB palmJoint = side == HOL::LeftHand
			? XR_BODY_JOINT_LEFT_HAND_PALM_FB
			: XR_BODY_JOINT_RIGHT_HAND_PALM_FB;

		auto& palm = jointLocations[palmJoint];
		const auto& metacarpal = jointLocations[metacarpalJoint];
		const auto& proximal = jointLocations[proximalJoint];
		const XrSpaceLocationFlags requiredFlags
			= XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
		if ((metacarpal.locationFlags & requiredFlags) != requiredFlags
			|| !(proximal.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
		{
			palm = {};
			return false;
		}

		palm.pose.position = toXrVector(
			(toEigenVector(metacarpal.pose.position) + toEigenVector(proximal.pose.position))
			* 0.5f);
		palm.pose.orientation = metacarpal.pose.orientation;
		palm.locationFlags = metacarpal.locationFlags;
		return true;
	}

	XrHandJointLocationEXT& getJoint(XrHandJointLocationEXT leftHandJoints[],
									 XrHandJointLocationEXT rightHandJoints[],
									 XrHandJointEXT joint,
									 HOL::HandSide side)
	{
		return (side == HOL::HandSide::LeftHand ? leftHandJoints[joint] : rightHandJoints[joint]);
	}
	XrHandJointLocationEXT& getJoint(XrHandJointLocationEXT joints[], XrHandJointEXT joint)
	{
		return joints[joint];
	}

	Eigen::Vector3f getJointPosition(XrHandJointLocationEXT leftHandJoints[],
									 XrHandJointLocationEXT rightHandJoints[],
									 XrHandJointEXT joint,
									 HOL::HandSide side)
	{
		return HOL::OpenXR::toEigenVector(
			(side == HOL::HandSide::LeftHand ? leftHandJoints[joint] : rightHandJoints[joint])
				.pose.position);
	}

	Eigen::Vector3f getJointPosition(XrHandJointLocationEXT joints[], XrHandJointEXT joint)
	{
		return HOL::OpenXR::toEigenVector(joints[joint].pose.position);
	}

	Eigen::Vector3f getJointPosition(XrHandJointLocationEXT joint)
	{
		return HOL::OpenXR::toEigenVector(joint.pose.position);
	}

	Eigen::Quaternionf getJointOrientation(XrHandJointLocationEXT leftHandJoints[],
										   XrHandJointLocationEXT rightHandJoints[],
										   XrHandJointEXT joint,
										   HOL::HandSide side)
	{
		return HOL::OpenXR::toEigenQuaternion(
			(side == HOL::HandSide::LeftHand ? leftHandJoints[joint] : rightHandJoints[joint])
				.pose.orientation);
	}

	Eigen::Quaternionf getJointOrientation(XrHandJointLocationEXT joints[], XrHandJointEXT joint)
	{
		return HOL::OpenXR::toEigenQuaternion(joints[joint].pose.orientation);
	}

	Eigen::Quaternionf getJointOrientation(XrHandJointLocationEXT joint)
	{
		return HOL::OpenXR::toEigenQuaternion(joint.pose.orientation);
	}

	XrHandJointEXT getRootJoint(HOL::FingerType fingerType)
	{
		switch (fingerType)
		{
			case FingerType::FingerThumb:
				return XrHandJointEXT::XR_HAND_JOINT_WRIST_EXT; // Thumb joint calculated from wrist
			case FingerType::FingerIndex:
				return XrHandJointEXT::XR_HAND_JOINT_INDEX_METACARPAL_EXT;
			case FingerType::FingerMiddle:
				return XrHandJointEXT::XR_HAND_JOINT_MIDDLE_METACARPAL_EXT;
			case FingerType::FingerRing:
				return XrHandJointEXT::XR_HAND_JOINT_RING_METACARPAL_EXT;
			case FingerType::FingerLittle:
				return XrHandJointEXT::XR_HAND_JOINT_LITTLE_METACARPAL_EXT;
			default:
				return XrHandJointEXT::XR_HAND_JOINT_WRIST_EXT;
		}
	}

	XrHandJointEXT getFirstFingerJoint(HOL::FingerType fingerType)
	{
		switch (fingerType)
		{
			case FingerType::FingerThumb:
				return XrHandJointEXT::XR_HAND_JOINT_THUMB_METACARPAL_EXT;
			case FingerType::FingerIndex:
				return XrHandJointEXT::XR_HAND_JOINT_INDEX_PROXIMAL_EXT;
			case FingerType::FingerMiddle:
				return XrHandJointEXT::XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT;
			case FingerType::FingerRing:
				return XrHandJointEXT::XR_HAND_JOINT_RING_PROXIMAL_EXT;
			case FingerType::FingerLittle:
				return XrHandJointEXT::XR_HAND_JOINT_LITTLE_PROXIMAL_EXT;
			default:
				return XrHandJointEXT::XR_HAND_JOINT_WRIST_EXT;
		}
	}

	XrHandJointEXT getSecondFingerJoint(HOL::FingerType fingerType)
	{
		return (XrHandJointEXT)(getFirstFingerJoint(fingerType) + 1);
	}

	XrHandJointEXT getFingerTip(HOL::FingerType fingerType)
	{
		switch (fingerType)
		{
			case FingerType::FingerThumb:
				return XrHandJointEXT::XR_HAND_JOINT_THUMB_TIP_EXT;
			case FingerType::FingerIndex:
				return XrHandJointEXT::XR_HAND_JOINT_INDEX_TIP_EXT;
			case FingerType::FingerMiddle:
				return XrHandJointEXT::XR_HAND_JOINT_MIDDLE_TIP_EXT;
			case FingerType::FingerRing:
				return XrHandJointEXT::XR_HAND_JOINT_RING_TIP_EXT;
			case FingerType::FingerLittle:
				return XrHandJointEXT::XR_HAND_JOINT_LITTLE_TIP_EXT;
			default:
				return XrHandJointEXT::XR_HAND_JOINT_WRIST_EXT;
		}
	}

} // namespace HOL::OpenXR
