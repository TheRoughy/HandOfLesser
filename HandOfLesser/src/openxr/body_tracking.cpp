#include "body_tracking.h"
#include "src/core/ui/display_global.h"
#include "src/core/ui/user_interface.h"
#include "XrUtils.h"
#include <src/core/settings_global.h>

void HOL::OpenXR::BodyTracking::init()
{
	for (auto& location : this->mLastBodyTrackerLocations)
	{
		location.position = Eigen::Vector3f::Zero();
		location.orientation = Eigen::Quaternionf::Identity();
	}
}

void HOL::OpenXR::BodyTracking::initOpenXR(xr::UniqueDynamicSession& session)
{
	this->mBodyTracker.initOpenXR(session);
}

void HOL::OpenXR::BodyTracking::updateBody(XrSpace space,
										   XrTime time,
										   const HOL::PoseLocation* hmdPose,
										   const std::array<const HOL::HandPose*, HOL::HandSide_MAX>&
											   lastHandPoses,
										   const HOL::BodyTrackingSample* externalSample)
{
	this->mBodyTracker.updateJointLocations(
		space, time, hmdPose, lastHandPoses, externalSample);
}

OpenXRBody& HOL::OpenXR::BodyTracking::getBodyTracker()
{
	return mBodyTracker;
}

const OpenXRBody& HOL::OpenXR::BodyTracking::getBodyTracker() const
{
	return mBodyTracker;
}

const std::array<HOL::PoseLocation,
				 static_cast<int>(HOL::BodyTrackerRole::TrackerRole_MAX)>&
HOL::OpenXR::BodyTracking::getLastBodyTrackerLocations() const
{
	return mLastBodyTrackerLocations;
}

HOL::MultimodalPosePayload HOL::OpenXR::BodyTracking::getMultimodalPosePayload()
{
	MultimodalPosePayload payload;

	auto* bodyJoints = mBodyTracker.getLastJointLocations();
	if (!mBodyTracker.isAvailable() || !mBodyTracker.active || bodyJoints == nullptr)
	{
		return payload;
	}

	/////////////
	// Offsets
	/////////////

	// We have a base offset configured to match what VDXR handtracking gives you.
	// The user-configurable offset is applied in addition to this.
	auto controllerOffset = HOL::getControllerBaseOffset();

	// Matches to controller position, matching what VD does
	Eigen::Vector3f controllerRotationOffset = controllerOffset.orientation;
	Eigen::Vector3f controllerTranslationOffset = controllerOffset.position;

	// Left hand palm
	auto& leftPalm = bodyJoints[XR_BODY_JOINT_LEFT_HAND_PALM_FB];
	{
		Eigen::Vector3f position = OpenXR::toEigenVector(leftPalm.pose.position);
		Eigen::Quaternionf rotation = OpenXR::toEigenQuaternion(leftPalm.pose.orientation);
		/*
		position = HOL::translateLocal(position, rotation, controllerTranslationOffset);
		rotation = HOL::rotateLocal(
			rotation, HOL::quaternionFromEulerAnglesDegrees(controllerRotationOffset));
		*/
		payload.leftHandPose.position = position;
		payload.leftHandPose.orientation = rotation;
		payload.leftHandTracked = leftPalm.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	}

	// Flip for right hand
	controllerRotationOffset = flipHandRotation(controllerRotationOffset);
	controllerTranslationOffset = flipHandTranslation(controllerTranslationOffset);

	// Right hand palm
	auto& rightPalm = bodyJoints[XR_BODY_JOINT_RIGHT_HAND_PALM_FB];
	{
		Eigen::Vector3f position = OpenXR::toEigenVector(rightPalm.pose.position);
		Eigen::Quaternionf rotation = OpenXR::toEigenQuaternion(rightPalm.pose.orientation);
		/*
		position = HOL::translateLocal(position, rotation, controllerTranslationOffset);
		rotation = HOL::rotateLocal(
			rotation, HOL::quaternionFromEulerAnglesDegrees(controllerRotationOffset));
		*/

		payload.rightHandPose.position = position;
		payload.rightHandPose.orientation = rotation;
		payload.rightHandTracked = rightPalm.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	}

	return payload;
}

std::vector<HOL::BodyTrackerPosePayload> HOL::OpenXR::BodyTracking::getBodyTrackerPayloads()
{
	std::vector<HOL::BodyTrackerPosePayload> payloads;

	// Get body tracking joint data
	XrBodyJointLocationFB* jointLocations = mBodyTracker.getLastJointLocations();

	if (!mBodyTracker.isAvailable() || !mBodyTracker.active || !jointLocations)
		return payloads;

	// Send payload for each enabled tracker
	for (int i = 0; i < static_cast<int>(BodyTrackerRole::TrackerRole_MAX); i++)
	{
		BodyTrackerRole role = static_cast<BodyTrackerRole>(i);

		bool trackerEnabled = Config.bodyTrackers.enabled[static_cast<int>(role)];

		bool shouldProcess = trackerEnabled || Config.visualizer.showBodyTrackerAxes;
		if (!shouldProcess)
		{
			continue;
		}

		// Get the joint for this tracker role
		XrBodyJointFB joint = bodyTrackerRoleToJoint(role);
		auto& location = jointLocations[joint];

		// Calculate tracker position (may be adjusted from joint position)
		Eigen::Vector3f trackerPosition;

		// Adjust position based on tracker role to match physical tracker mounting positions
		switch (role)
		{
			case BodyTrackerRole::LeftLowerArm:
			case BodyTrackerRole::RightLowerArm:
			{
				// Lower arm tracker: midpoint between elbow (lower arm joint) and wrist
				XrBodyJointFB wristJoint = (role == BodyTrackerRole::LeftLowerArm)
					? XR_BODY_JOINT_LEFT_HAND_WRIST_TWIST_FB
					: XR_BODY_JOINT_RIGHT_HAND_WRIST_TWIST_FB;
				auto& wristLocation = jointLocations[wristJoint];

				trackerPosition.x() = (location.pose.position.x + wristLocation.pose.position.x) * 0.5f;
				trackerPosition.y() = (location.pose.position.y + wristLocation.pose.position.y) * 0.5f;
				trackerPosition.z() = (location.pose.position.z + wristLocation.pose.position.z) * 0.5f;
				break;
			}
			case BodyTrackerRole::LeftUpperArm:
			case BodyTrackerRole::RightUpperArm:
			{
				// Upper arm tracker: midpoint between shoulder (upper arm joint) and elbow (lower arm joint)
				XrBodyJointFB elbowJoint = (role == BodyTrackerRole::LeftUpperArm)
					? XR_BODY_JOINT_LEFT_ARM_LOWER_FB
					: XR_BODY_JOINT_RIGHT_ARM_LOWER_FB;
				auto& elbowLocation = jointLocations[elbowJoint];

				trackerPosition.x() = (location.pose.position.x + elbowLocation.pose.position.x) * 0.5f;
				trackerPosition.y() = (location.pose.position.y + elbowLocation.pose.position.y) * 0.5f;
				trackerPosition.z() = (location.pose.position.z + elbowLocation.pose.position.z) * 0.5f;
				break;
			}
			default:
				// For other trackers (hips, chest), use the joint position directly
				trackerPosition.x() = location.pose.position.x;
				trackerPosition.y() = location.pose.position.y;
				trackerPosition.z() = location.pose.position.z;
				break;
		}

		// Create payload
		BodyTrackerPosePayload payload;
		payload.role = role;
		payload.active = mBodyTracker.active;
		payload.valid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
		payload.tracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;

		// Set adjusted position
		payload.location.position = trackerPosition;

		// Use orientation from the primary joint
		payload.location.orientation.w() = location.pose.orientation.w;
		payload.location.orientation.x() = location.pose.orientation.x;
		payload.location.orientation.y() = location.pose.orientation.y;
		payload.location.orientation.z() = location.pose.orientation.z;

		
		// Arm-mounted trackers look visually upside down with the raw body-joint
		// orientation, so flip them 180 degrees around their local forward axis.
		if (role == BodyTrackerRole::LeftUpperArm || role == BodyTrackerRole::LeftLowerArm)
		{
			payload.location.orientation = HOL::rotateLocal(
				payload.location.orientation, HOL::quaternionFromEulerAnglesDegrees(180, 0, 0));
		}

		// Velocities left at zero (body tracking velocity data is unreliable)
		payload.velocity.linearVelocity = Eigen::Vector3f::Zero();
		payload.velocity.angularVelocity = Eigen::Vector3f::Zero();

		this->mLastBodyTrackerLocations[static_cast<int>(role)] = payload.location;
		if (trackerEnabled)
		{
			payloads.push_back(payload);
		}
	}

	return payloads;
}
