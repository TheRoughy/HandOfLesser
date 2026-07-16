#pragma once

#include <d3d11.h> // Why do you need this??
#include <openxr/openxr_platform.h>
#include <openxr/openxr.hpp>
#include <HandOfLesserCommon.h>
#include <chrono>
#include "src/hands/hand_pose.h"
#include "openxr_body.h"

using namespace HOL;

class OpenXRHand
{
public:
	~OpenXRHand();
	void init(xr::UniqueDynamicSession& session, HOL::HandSide side);
	void updateJointLocations(
		xr::UniqueDynamicSpace& space,
		XrTime time,
		OpenXRBody& bodyTracker,
		float triggerStabilizationSmoothingMS,
		const HOL::HandTrackingSample* externalSample = nullptr);

	HandPose handPose{};
	XrHandJointLocationEXT* getLastJointLocations();
	const XrHandTrackingAimStateFB* getAimState() const;

private:
	void shutdown();
	void calculateCurlSplay();

	HOL::HandSide mSide;
	XrHandTrackerEXT mHandTracker = nullptr;
	XrPath mInputSourcePath;
	XrHandJointLocationEXT mJointLocations[XR_HAND_JOINT_COUNT_EXT]{};
	XrHandJointLocationEXT mPrevJointLocations[XR_HAND_JOINT_COUNT_EXT]{};
	XrHandJointVelocityEXT mJointVelocities[XR_HAND_JOINT_COUNT_EXT]{};
	XrHandTrackingAimStateFB mAimState{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
	XrHandTrackingDataSourceStateEXT mDataSourceState{XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};

	HOL::PoseLocation mPrevRawPose{};
	HOL::PoseLocation mFilteredPalmPose{};
	HOL::PoseVelocity mFilteredPalmVelocity{};
	uint64_t mPrevExternalUpdateGeneration = 0;
	bool mPrevActive = false;
	bool mPrevPoseValid = false;
	bool mPrevPoseTracked = false;
	bool mHasPrevRawPose = false;
	bool mHasFilteredPalmPose = false;
	XrTime mDirectHandTrackingStartTime = 0;
	XrTime mPrevFilteredSampleTime = 0;
	std::chrono::steady_clock::time_point mLastPoseUpdateTime{};
};
