#include "skeletal_pose_utils.h"

#include <algorithm>

namespace HOL::SteamVR
{
	namespace
	{
		HOL::PoseLocation composePose(const HOL::PoseLocation& parent,
								  const HOL::PoseLocation& child)
		{
			HOL::PoseLocation result;
			result.position = parent.position + parent.orientation * child.position;
			result.orientation = parent.orientation * child.orientation;
			return result;
		}

		void undoSkeletalPayloadConversion(HOL::HandSide side,
									   HandSkeletonBone bone,
									   HOL::PoseLocation& pose)
		{
			// Reverse buildSkeletalPoseFromPayload's handedness and basis conversions. SteamVR bones
			// are parent-relative, but we need them back in OpenXR coordinates first.
			const bool isMetacarpal = bone == eBone_Thumb0 || bone == eBone_IndexFinger0
				|| bone == eBone_MiddleFinger0 || bone == eBone_RingFinger0
				|| bone == eBone_PinkyFinger0;
			if (isMetacarpal)
			{
				const Eigen::Quaternionf magic
					= side == HandSide::LeftHand
						  ? Eigen::Quaternionf(0.5f, 0.5f, -0.5f, 0.5f)
						  : Eigen::Quaternionf(-0.5f, 0.5f, -0.5f, -0.5f);
				pose.position = magic.inverse() * pose.position;
				pose.orientation = magic.inverse() * pose.orientation;
			}

			if (side == HandSide::LeftHand)
			{
				pose.position.x() *= -1.0f;
				pose.position.y() *= -1.0f;
				pose.orientation.x() *= -1.0f;
				pose.orientation.y() *= -1.0f;
			}

			const Eigen::Vector3f convertedPosition = pose.position;
			pose.position = Eigen::Vector3f(
				-convertedPosition.z(), convertedPosition.y(), convertedPosition.x());
			const Eigen::Quaternionf convertedOrientation = pose.orientation;
			pose.orientation = Eigen::Quaternionf(convertedOrientation.w(),
											 -convertedOrientation.z(),
											 convertedOrientation.y(),
											 convertedOrientation.x());

			if (side == HandSide::RightHand && bone == eBone_Wrist)
			{
				pose.position.x() *= -1.0f;
				pose.position.y() *= -1.0f;
				pose.orientation.x() *= -1.0f;
				pose.orientation.y() *= -1.0f;
			}
		}
	} // namespace

	vr::VRBoneTransform_t poseLocationToBoneTransform(const HOL::PoseLocation& location)
	{
		vr::VRBoneTransform_t transform{};
		transform.position.v[0] = location.position.x();
		transform.position.v[1] = location.position.y();
		transform.position.v[2] = location.position.z();
		transform.position.v[3] = 1.0f;
		transform.orientation.w = location.orientation.w();
		transform.orientation.x = location.orientation.x();
		transform.orientation.y = location.orientation.y();
		transform.orientation.z = location.orientation.z();
		return transform;
	}

	HOL::PoseLocation boneTransformToPoseLocation(const vr::VRBoneTransform_t& transform)
	{
		HOL::PoseLocation pose;
		pose.position = Eigen::Vector3f(
			transform.position.v[0], transform.position.v[1], transform.position.v[2]);
		pose.orientation = Eigen::Quaternionf(transform.orientation.w,
										 transform.orientation.x,
										 transform.orientation.y,
										 transform.orientation.z);
		return pose;
	}

	vr::HmdMatrix34_t poseLocationToMatrix34(const HOL::PoseLocation& location)
	{
		vr::HmdMatrix34_t transform{};
		const Eigen::Matrix3f rotation = location.orientation.normalized().toRotationMatrix();
		for (int row = 0; row < 3; row++)
		{
			for (int column = 0; column < 3; column++)
			{
				transform.m[row][column] = rotation(row, column);
			}
			transform.m[row][3] = location.position[row];
		}
		return transform;
	}

	HOL::PoseLocation getRelativePose(const HOL::PoseLocation& reference,
									  const HOL::PoseLocation& pose)
	{
		const Eigen::Quaternionf inverseReference = reference.orientation.inverse();
		HOL::PoseLocation relative;
		relative.position = inverseReference * (pose.position - reference.position);
		relative.orientation = inverseReference * pose.orientation;
		return relative;
	}

	HOL::PoseLocation getSteamVRTrackingReferencePose(const vr::DriverPose_t& pose)
	{
		// WorldFromDriver places the driver's tracking reference in SteamVR space. Deliberately
		// leave DriverFromHead unapplied: hand-tracking controllers use this raw pose as the palm.
		HOL::PoseLocation worldFromDriver;
		worldFromDriver.position = Eigen::Vector3f(pose.vecWorldFromDriverTranslation[0],
											 pose.vecWorldFromDriverTranslation[1],
											 pose.vecWorldFromDriverTranslation[2]);
		worldFromDriver.orientation = Eigen::Quaternionf(pose.qWorldFromDriverRotation.w,
											  pose.qWorldFromDriverRotation.x,
											  pose.qWorldFromDriverRotation.y,
											  pose.qWorldFromDriverRotation.z);

		HOL::PoseLocation trackingReference;
		trackingReference.position
			= Eigen::Vector3f(pose.vecPosition[0], pose.vecPosition[1], pose.vecPosition[2]);
		trackingReference.orientation = Eigen::Quaternionf(
			pose.qRotation.w, pose.qRotation.x, pose.qRotation.y, pose.qRotation.z);
		return composePose(worldFromDriver, trackingReference);
	}

	HOL::PoseLocation getSteamVRDevicePose(const vr::DriverPose_t& pose)
	{
		// SteamVR applies DriverFromHead after the tracking-reference pose to obtain the visible
		// device pose. The HMD's visible pose is what corresponds to OpenXR's view pose.
		HOL::PoseLocation driverFromHead;
		driverFromHead.position = Eigen::Vector3f(pose.vecDriverFromHeadTranslation[0],
											pose.vecDriverFromHeadTranslation[1],
											pose.vecDriverFromHeadTranslation[2]);
		driverFromHead.orientation = Eigen::Quaternionf(pose.qDriverFromHeadRotation.w,
											 pose.qDriverFromHeadRotation.x,
											 pose.qDriverFromHeadRotation.y,
											 pose.qDriverFromHeadRotation.z);
		return composePose(getSteamVRTrackingReferencePose(pose), driverFromHead);
	}

	void setSteamVRDevicePose(vr::DriverPose_t& pose, const HOL::PoseLocation& devicePose)
	{
		const HOL::PoseLocation worldFromDriver{
			Eigen::Vector3f(pose.vecWorldFromDriverTranslation[0],
							pose.vecWorldFromDriverTranslation[1],
							pose.vecWorldFromDriverTranslation[2]),
			Eigen::Quaternionf(pose.qWorldFromDriverRotation.w,
							   pose.qWorldFromDriverRotation.x,
							   pose.qWorldFromDriverRotation.y,
							   pose.qWorldFromDriverRotation.z)};
		const HOL::PoseLocation driverFromHead{
			Eigen::Vector3f(pose.vecDriverFromHeadTranslation[0],
							pose.vecDriverFromHeadTranslation[1],
							pose.vecDriverFromHeadTranslation[2]),
			Eigen::Quaternionf(pose.qDriverFromHeadRotation.w,
							   pose.qDriverFromHeadRotation.x,
							   pose.qDriverFromHeadRotation.y,
							   pose.qDriverFromHeadRotation.z)};

		const Eigen::Quaternionf trackingReferenceOrientation
			= devicePose.orientation * driverFromHead.orientation.inverse();
		const Eigen::Vector3f trackingReferencePosition
			= devicePose.position
			  - trackingReferenceOrientation * driverFromHead.position;
		const Eigen::Quaternionf localOrientation
			= worldFromDriver.orientation.inverse() * trackingReferenceOrientation;
		const Eigen::Vector3f localPosition = worldFromDriver.orientation.inverse()
			* (trackingReferencePosition - worldFromDriver.position);

		pose.vecPosition[0] = localPosition.x();
		pose.vecPosition[1] = localPosition.y();
		pose.vecPosition[2] = localPosition.z();
		pose.qRotation = {localOrientation.w(),
						  localOrientation.x(),
						  localOrientation.y(),
						  localOrientation.z()};
	}

	HOL::PoseVelocity getSteamVRTrackingReferenceVelocity(const vr::DriverPose_t& pose)
	{
		const Eigen::Quaternionf worldFromDriver(pose.qWorldFromDriverRotation.w,
										 pose.qWorldFromDriverRotation.x,
										 pose.qWorldFromDriverRotation.y,
										 pose.qWorldFromDriverRotation.z);
		HOL::PoseVelocity velocity;
		velocity.linearVelocity = worldFromDriver
			* Eigen::Vector3f(pose.vecVelocity[0], pose.vecVelocity[1], pose.vecVelocity[2]);
		velocity.angularVelocity = worldFromDriver
			* Eigen::Vector3f(pose.vecAngularVelocity[0],
								  pose.vecAngularVelocity[1],
								  pose.vecAngularVelocity[2]);
		return velocity;
	}

	HOL::PoseVelocity getSteamVRVelocityAtPosition(
		const vr::DriverPose_t& pose, const Eigen::Vector3f& worldPosition)
	{
		HOL::PoseVelocity velocity = getSteamVRTrackingReferenceVelocity(pose);
		const Eigen::Vector3f offset
			= worldPosition - getSteamVRTrackingReferencePose(pose).position;
		velocity.linearVelocity += velocity.angularVelocity.cross(offset);
		return velocity;
	}

	void setSteamVRVelocityAtPosition(
		vr::DriverPose_t& pose,
		const Eigen::Vector3f& worldPosition,
		const HOL::PoseVelocity& velocity)
	{
		const Eigen::Quaternionf worldFromDriver(pose.qWorldFromDriverRotation.w,
										 pose.qWorldFromDriverRotation.x,
										 pose.qWorldFromDriverRotation.y,
										 pose.qWorldFromDriverRotation.z);
		const Eigen::Vector3f offset
			= worldPosition - getSteamVRTrackingReferencePose(pose).position;
		const Eigen::Vector3f trackingReferenceVelocity
			= velocity.linearVelocity - velocity.angularVelocity.cross(offset);
		const Eigen::Vector3f localVelocity
			= worldFromDriver.inverse() * trackingReferenceVelocity;
		const Eigen::Vector3f localAngularVelocity
			= worldFromDriver.inverse() * velocity.angularVelocity;

		pose.vecVelocity[0] = localVelocity.x();
		pose.vecVelocity[1] = localVelocity.y();
		pose.vecVelocity[2] = localVelocity.z();
		pose.vecAngularVelocity[0] = localAngularVelocity.x();
		pose.vecAngularVelocity[1] = localAngularVelocity.y();
		pose.vecAngularVelocity[2] = localAngularVelocity.z();
	}

	void buildSkeletalPoseFromPayload(
		const HOL::SkeletalPayload& payload,
		vr::VRBoneTransform_t outPose[SteamVR::HandSkeletonBone::eBone_Count])
	{
		// Convert OpenXR-oriented local bones to SteamVR's handed skeletal basis.
		HOL::SkeletalPayload workingPayload = payload;

		if (workingPayload.side == HandSide::RightHand)
		{
			auto& wristJoint = workingPayload.locations[eBone_Wrist];
			wristJoint.position.x() *= -1.0f;
			wristJoint.position.y() *= -1.0f;
			wristJoint.orientation.x() *= -1.0f;
			wristJoint.orientation.y() *= -1.0f;
		}

		for (int i = 1; i < eBone_Count; i++)
		{
			auto& joint = workingPayload.locations[i];
			std::swap(joint.position.x(), joint.position.z());
			joint.position.z() *= -1.0f;
			std::swap(joint.orientation.x(), joint.orientation.z());
			joint.orientation.z() *= -1.0f;

			if (workingPayload.side == HandSide::LeftHand)
			{
				joint.position.x() *= -1.0f;
				joint.position.y() *= -1.0f;
				joint.orientation.x() *= -1.0f;
				joint.orientation.y() *= -1.0f;
			}
		}

		const HandSkeletonBone fingerStarts[]
			= {eBone_Thumb0, eBone_IndexFinger0, eBone_MiddleFinger0, eBone_RingFinger0,
			   eBone_PinkyFinger0};
		const int fingerCounts[] = {4, 5, 5, 5, 5};
		const Eigen::Quaternionf leftMagic(0.5f, 0.5f, -0.5f, 0.5f);
		const Eigen::Quaternionf rightMagic(-0.5f, 0.5f, -0.5f, -0.5f);
		for (int finger = 0; finger < 5; finger++)
		{
			auto& joint = workingPayload.locations[fingerStarts[finger]];
			const Eigen::Quaternionf magic
				= workingPayload.side == HandSide::LeftHand ? leftMagic : rightMagic;
			joint.orientation = magic * joint.orientation;
			joint.position = magic * joint.position;
		}

		// SteamVR auxiliary bones are wrist-relative absolute fingertip poses, not hierarchy nodes.
		const auto& wristJoint = workingPayload.locations[eBone_Wrist];
		for (int finger = 0; finger < 5; finger++)
		{
			const HandSkeletonBone firstJoint = fingerStarts[finger];
			auto& auxJoint = workingPayload.locations[eBone_Aux_Thumb + finger];
			auxJoint.position = wristJoint.position;
			auxJoint.orientation = wristJoint.orientation;

			for (int joint = 1; joint < fingerCounts[finger]; joint++)
			{
				const auto childBone
					= static_cast<HandSkeletonBone>(static_cast<int>(firstJoint) + joint);
				const auto& child = workingPayload.locations[childBone];
				auxJoint.position += auxJoint.orientation * child.position;
				auxJoint.orientation = auxJoint.orientation * child.orientation;
			}
		}

		for (int i = 0; i < eBone_Count; i++)
		{
			outPose[i] = poseLocationToBoneTransform(workingPayload.locations[i]);
		}
	}

	void buildOpenXRPalmRelativeJointPoseFromSkeletalPose(
		HOL::HandSide side,
		const vr::VRBoneTransform_t skeletalPose[SteamVR::HandSkeletonBone::eBone_Count],
		HOL::PoseLocation outJoints[XR_HAND_JOINT_COUNT_EXT])
	{
		// Undo the submission conversion, then walk SteamVR's parent-relative hierarchy to obtain
		// absolute joints. The final result is normalized around the palm so any controller pose can
		// place the hand later without rebuilding the skeleton.
		HOL::PoseLocation local[eBone_Count]{};
		HOL::PoseLocation relative[eBone_Count]{};
		for (int i = 0; i < eBone_Count; i++)
		{
			local[i] = boneTransformToPoseLocation(skeletalPose[i]);
			if (i > eBone_Root && i < eBone_Aux_Thumb)
			{
				undoSkeletalPayloadConversion(side, static_cast<HandSkeletonBone>(i), local[i]);
			}
		}

		relative[eBone_Root] = local[eBone_Root];
		relative[eBone_Wrist] = composePose(relative[eBone_Root], local[eBone_Wrist]);

		const int fingerStarts[] = {eBone_Thumb0,
								  eBone_IndexFinger0,
								  eBone_MiddleFinger0,
								  eBone_RingFinger0,
								  eBone_PinkyFinger0};
		const int fingerCounts[] = {4, 5, 5, 5, 5};
		for (int finger = 0; finger < 5; finger++)
		{
			for (int joint = 0; joint < fingerCounts[finger]; joint++)
			{
				const int bone = fingerStarts[finger] + joint;
				const int parent = joint == 0 ? eBone_Wrist : bone - 1;
				relative[bone] = composePose(relative[parent], local[bone]);
			}
		}

		for (int i = eBone_Wrist; i < eBone_Aux_Thumb; i++)
		{
			const XrHandJointEXT joint
				= skeletalBoneToOpenXRJoint(static_cast<HandSkeletonBone>(i));
			if (joint != XR_HAND_JOINT_MAX_ENUM_EXT)
			{
				outJoints[joint] = relative[i];
			}
		}

		// SteamVR has no palm bone; reconstruct OpenXR's palm from the middle metacarpal.
		const auto& middleMetacarpal = outJoints[XR_HAND_JOINT_MIDDLE_METACARPAL_EXT];
		const auto& middleProximal = outJoints[XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT];
		outJoints[XR_HAND_JOINT_PALM_EXT].position
			= (middleMetacarpal.position + middleProximal.position) * 0.5f;
		outJoints[XR_HAND_JOINT_PALM_EXT].orientation = middleMetacarpal.orientation;

		const HOL::PoseLocation palm = outJoints[XR_HAND_JOINT_PALM_EXT];
		const Eigen::Quaternionf inversePalm = palm.orientation.inverse();
		for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
		{
			outJoints[i].position = inversePalm * (outJoints[i].position - palm.position);
			outJoints[i].orientation = inversePalm * outJoints[i].orientation;
		}
	}

	void applySteamVRPoseToOpenXRJointPose(
		const HOL::PoseLocation& palmPose,
		const HOL::PoseLocation relativeJoints[XR_HAND_JOINT_COUNT_EXT],
		HOL::PoseLocation outJoints[XR_HAND_JOINT_COUNT_EXT])
	{
		for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
		{
			outJoints[i] = composePose(palmPose, relativeJoints[i]);
		}
	}

} // namespace HOL::SteamVR
