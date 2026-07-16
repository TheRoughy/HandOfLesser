#pragma once

#include "src/hand/hand_tracking_sample.h"
#include "src/packet/nativepacket.h"

#include <array>
#include <atomic>
#include <memory>

namespace HOL::SteamVR
{
	class SteamVRHandTrackingSource
	{
	public:
		void updateBaseline(const HOL::SteamVRHandBaselinePayload& payload);
		void updatePose(const HOL::SteamVRHandPosePayload& payload);
		const HOL::HandTrackingSample*
		getSample(HOL::HandSide side, const HOL::PoseLocation* openXRHmdPose, bool applyBaseOffset);
		void applySourcePose(HOL::HandSide side, HOL::HandTransformPayload& payload) const;

	private:
		// Mutable reconstruction state is confined to the application's tracking thread.
		struct SideState
		{
			HOL::HandTrackingSample sample;
			HOL::PoseLocation relativeJoints[XR_HAND_JOINT_COUNT_EXT]{};
			HOL::PoseLocation stageFromSteamVR{};
			vr::DriverPose_t sourcePose{};
			uint32_t sourceDeviceId = vr::k_unTrackedDeviceIndexInvalid;
			uint64_t poseGeneration = 0;
			uint64_t skeletonGeneration = 0;
			uint64_t sampleGeneration = 0;
			bool hasBaseline = false;
			bool hasRelativeJoints = false;
			bool hasStageFromSteamVR = false;
			bool hasSourcePose = false;
			bool applyBaseOffset = false;
		};

		void setInactive(SideState& state);

		// The pipe receiver publishes immutable packets without locking the tracking loop.
		std::array<std::atomic<std::shared_ptr<const HOL::SteamVRHandBaselinePayload>>,
				   HOL::HandSide_MAX>
			mBaselines;
		std::array<std::atomic<std::shared_ptr<const HOL::SteamVRHandPosePayload>>,
				   HOL::HandSide_MAX>
			mPoses;
		std::array<SideState, HOL::HandSide_MAX> mStates;
	};
} // namespace HOL::SteamVR
