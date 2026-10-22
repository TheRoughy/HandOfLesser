#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <d3d11.h> // Why do you need this??
#include <openxr/openxr_platform.h>
#include <openxr/openxr.hpp>
#include <HandOfLesserCommon.h>
#include "src/hand/body_tracking_sample.h"
#include "src/hands/hand_pose.h"

using namespace HOL;

class OpenXRBody
{
public:
	// The UI requests calibration; the tracking thread captures matching hand/tracker samples.
	void calibrateTrackers();
	~OpenXRBody();
	void initOpenXR(xr::UniqueDynamicSession& session);
	void updateJointLocations(XrSpace space,
								  XrTime time,
								  const HOL::PoseLocation* hmdPose,
								  const std::array<const HOL::HandPose*, HOL::HandSide_MAX>&
									  lastHandPoses,
								  const HOL::BodyTrackingSample* externalSample);
	// Refresh tracker-driven palms without reacquiring body or finger joints.
	void updateTrackerFallback(XrTime time,
		const std::array<const HOL::HandPose*, HOL::HandSide_MAX>& lastHandPoses,
		const std::array<std::optional<HOL::PoseLocation>, HOL::HandSide_MAX>& trackerReferences);

	XrBodyJointLocationFB* getLastJointLocations();
	const XrBodyJointLocationFB* getLastJointLocations() const;
	const XrBodyJointLocationFB& getRuntimePalmJoint(HOL::HandSide side) const;
	bool isAvailable() const;
	XrBodyTrackerFB getBodyTrackerFB();
	bool getHeadPose(HOL::PoseLocation& pose) const;
	Eigen::Quaternionf getReferenceOrientation(HOL::settings::JoystickReferenceMode mode,
											   bool& valid) const;
	float confidence = 0.0f;
	bool active = false;

private:
	// Body head joint sits below and behind the eye/view origin, measured in HMD-local meters.
	inline static const Eigen::Vector3f HmdLocalHeadOffset{0.0f, -0.093f, 0.075f};

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

	void preservePalmPose(HandSide side, const XrBodyJointLocationFB& anchor);
	void updatePalmFallback(HandSide side, XrTime time, const HOL::HandPose& lastHandPose,
		bool usingSyntheticBody);
	void updateTrackerPalm(HandSide side, XrTime time, const HOL::HandPose& lastHandPose,
		const std::optional<HOL::PoseLocation>& trackerReference, bool calibrate);
	bool canUseArmTrackingAnchor() const;
	void reconstructPalmJoint(HandSide side);
	void updateTrackedPalmTransform(HandSide side, const XrBodyJointLocationFB& anchor, XrTime time);
	void setFallbackJointLocations(
		const HOL::PoseLocation* hmdPose,
		const std::array<const HOL::HandPose*, HOL::HandSide_MAX>& lastHandPoses);

	Eigen::Quaternionf fixOculusJointOrientation(const Eigen::Vector3f& currentJointPos,
												 const Eigen::Vector3f& nextJointPos,
												 const Eigen::Quaternionf& bogusOrientation);
	StoredPalmTransform mStoredPalmTransforms[2]{};
	std::array<std::string, HOL::HandSide_MAX> mFallbackTrackerSerials{};
	std::atomic<bool> mTrackerCalibrationRequested = false;
	XrTime mHandTrackingAcquiredTime[2]{};
	bool mWasHandTrackingTracked[2]{};

	XrBodyTrackerFB mBodyTracker = nullptr;
	// External body samples count as available even though no native tracker handle exists.
	bool mUsingExternalBody = false;
	bool mUsingSyntheticBody = true;
	// Tracker availability must not keep the body active after both trackers disappear.
	bool mBodyActive = false;
	HOL::HandFallbackMode mFallbackMode = HOL::HandFallbackMode::BodyTracking;
	XrPath mInputSourcePath;
	XrBodyJointLocationFB mJointLocations[XR_BODY_JOINT_COUNT_FB]{};
	XrBodyJointLocationFB mPreviousJointLocations[XR_BODY_JOINT_COUNT_FB]{};
	XrBodyJointLocationFB mCorrectedJointLocations[XR_BODY_JOINT_COUNT_FB]{};
	// Controller-held detection must use real body palms, not palms attached to fallback trackers.
	XrBodyJointLocationFB mRuntimePalmJoints[HOL::HandSide_MAX]{};
};
