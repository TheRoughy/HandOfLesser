#pragma once

#include "src/hand/tracking_source.h"
#include "src/packet/nativepacket.h"

#include <array>
#include <atomic>
#include <memory>

namespace HOL::SteamVR
{
	// Reconstructs OpenXR-shaped hands from SteamVR controller poses and palm-relative skeletons.
	// Packet receiver methods only publish snapshots; update() owns all mutable reconstruction work.
	class SteamVRTrackingSource : public HOL::TrackingSource
	{
	public:
		void updateBaseline(const HOL::SteamVRHandBaselinePayload& payload);
		void updatePose(const HOL::SteamVRHandPosePayload& payload);
		void updateHmdPose(const HOL::SteamVRHmdPosePayload& payload);
		HOL::TrackingSourceFrame update(bool skeletalUpdate, bool applyBaseOffset) override;
		void applySourcePose(HOL::HandSide side, HOL::HandTransformPayload& payload) const override;
		bool hasTrackingReference() const override;
		void reset() override;

	private:
		// Mutable reconstruction state is confined to the application's tracking thread.
		struct SideState
		{
			HOL::HandTrackingSample sample;
			// Keeping the skeleton palm-relative lets high-frequency pose packets move it without
			// repeating the full skeletal conversion.
			HOL::PoseLocation relativeJoints[XR_HAND_JOINT_COUNT_EXT]{};
			// Retained as the native template when the processed pose returns to the driver.
			vr::DriverPose_t sourcePose{};
			uint32_t sourceDeviceId = vr::k_unTrackedDeviceIndexInvalid;
			uint64_t poseGeneration = 0;
			uint64_t skeletonGeneration = 0;
			uint64_t sampleGeneration = 0;
			bool hasBaseline = false;
			bool hasRelativeJoints = false;
			bool hasSourcePose = false;
			bool applyBaseOffset = false;
		};

		void setInactive(SideState& state);
		const HOL::HandTrackingSample*
		getSample(HOL::HandSide side, bool applyBaseOffset, bool skeletalUpdate);

		// The pipe receiver publishes immutable packets without locking the tracking loop.
		std::array<std::atomic<std::shared_ptr<const HOL::SteamVRHandBaselinePayload>>,
				   HOL::HandSide_MAX>
			mBaselines;
		std::array<std::atomic<std::shared_ptr<const HOL::SteamVRHandPosePayload>>,
				   HOL::HandSide_MAX>
			mPoses;
		std::atomic<std::shared_ptr<const HOL::SteamVRHmdPosePayload>> mHmdPose;
		// reset() can run on the pipe thread; mutable side state is cleared by update().
		std::atomic<bool> mResetRequested = false;
		std::array<SideState, HOL::HandSide_MAX> mStates;
	};
} // namespace HOL::SteamVR
