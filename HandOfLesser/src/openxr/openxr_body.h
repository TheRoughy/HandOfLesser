#pragma once

#include <array>
#include <d3d11.h> // Why do you need this??
#include <openxr/openxr_platform.h>
#include <openxr/openxr.hpp>
#include <HandOfLesserCommon.h>
#include "src/hands/hand_pose.h"

using namespace HOL;

class OpenXRBody
{
public:
	~OpenXRBody();
	void init(xr::UniqueDynamicSession& session);
	void updateJointLocations(xr::UniqueDynamicSpace& space,
								  XrTime time,
								  const HOL::PoseLocation* hmdPose,
								  const std::array<const HOL::HandPose*, HOL::HandSide_MAX>&
									  lastHandPoses);

	XrBodyJointLocationFB* getLastJointLocations();
	bool isAvailable() const;
	XrBodyTrackerFB getBodyTrackerFB();
	bool getHeadPose(HOL::PoseLocation& pose) const;
	Eigen::Quaternionf getReferenceOrientation(HOL::settings::JoystickReferenceMode mode,
											   bool& valid) const;
	float confidence = 0.0f;
	bool active = false;

private:
	struct StoredPalmTransform
	{
		Eigen::Vector3f relativePos = Eigen::Vector3f::Zero();
		Eigen::Quaternionf relativeRot = Eigen::Quaternionf::Identity();
		bool valid = false;
	};

	void shutdown();
	void calculateRelativeTransform(const XrBodyJointLocationFB& baseJoint,
									const XrBodyJointLocationFB& childJoint,
									Eigen::Vector3f& relativePos,
									Eigen::Quaternionf& relativeRot);
	void applyRelativeTransform(const XrBodyJointLocationFB& baseJoint,
								XrBodyJointLocationFB& childJoint,
								const Eigen::Vector3f& relativePos,
								const Eigen::Quaternionf& relativeRot);

	void preservePalmPose(HandSide side);
	bool canUseArmTrackingAnchor() const;
	void generateMissingPalmJoint(HandSide side);
	void updateTrackedPalmTransform(HandSide side, XrBodyJointFB anchorJoint, XrTime time);
	void setFallbackJointLocations(
		const HOL::PoseLocation* hmdPose,
		const std::array<const HOL::HandPose*, HOL::HandSide_MAX>& lastHandPoses);

	Eigen::Quaternionf fixOculusJointOrientation(const Eigen::Vector3f& currentJointPos,
												 const Eigen::Vector3f& nextJointPos,
												 const Eigen::Quaternionf& bogusOrientation);
	StoredPalmTransform mStoredPalmTransforms[2]{};
	XrTime mHandTrackingAcquiredTime[2]{};
	bool mWasHandTrackingTracked[2]{};

	XrBodyTrackerFB mBodyTracker = nullptr;
	XrPath mInputSourcePath;
	XrBodyJointLocationFB mJointLocations[XR_BODY_JOINT_COUNT_FB]{};
	XrBodyJointLocationFB mPreviousJointLocations[XR_BODY_JOINT_COUNT_FB]{};
	XrBodyJointLocationFB mCorrectedJointLocations[XR_BODY_JOINT_COUNT_FB]{};
};
