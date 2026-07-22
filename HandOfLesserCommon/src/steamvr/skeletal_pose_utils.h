#pragma once

#include "src/packet/nativepacket.h"

namespace HOL::SteamVR
{
	vr::VRBoneTransform_t poseLocationToBoneTransform(const HOL::PoseLocation& location);
	HOL::PoseLocation boneTransformToPoseLocation(const vr::VRBoneTransform_t& transform);
	vr::HmdMatrix34_t poseLocationToMatrix34(const HOL::PoseLocation& location);
	HOL::PoseLocation getRelativePose(const HOL::PoseLocation& reference,
									  const HOL::PoseLocation& pose);
	HOL::PoseLocation getSteamVRTrackingReferencePose(const vr::DriverPose_t& pose);
	HOL::PoseLocation getSteamVRDevicePose(const vr::DriverPose_t& pose);
	void setSteamVRDevicePose(vr::DriverPose_t& pose, const HOL::PoseLocation& devicePose);
	HOL::PoseVelocity getSteamVRTrackingReferenceVelocity(const vr::DriverPose_t& pose);
	HOL::PoseVelocity getSteamVRVelocityAtPosition(
		const vr::DriverPose_t& pose, const Eigen::Vector3f& worldPosition);
	void setSteamVRVelocityAtPosition(
		vr::DriverPose_t& pose,
		const Eigen::Vector3f& worldPosition,
		const HOL::PoseVelocity& velocity);

	void buildSkeletalPoseFromPayload(
		const HOL::SkeletalPayload& payload,
		vr::VRBoneTransform_t outPose[SteamVR::HandSkeletonBone::eBone_Count]);

	void buildOpenXRPalmRelativeJointPoseFromSkeletalPose(
		HOL::HandSide side,
		const vr::VRBoneTransform_t skeletalPose[SteamVR::HandSkeletonBone::eBone_Count],
		HOL::PoseLocation outJoints[XR_HAND_JOINT_COUNT_EXT]);

	void applySteamVRPoseToOpenXRJointPose(
		const HOL::PoseLocation& palmPose,
		const HOL::PoseLocation relativeJoints[XR_HAND_JOINT_COUNT_EXT],
		HOL::PoseLocation outJoints[XR_HAND_JOINT_COUNT_EXT]);

} // namespace HOL::SteamVR
