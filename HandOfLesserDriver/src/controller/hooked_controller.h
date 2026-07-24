#pragma once

#include "generic_control_interface.h"
#include <cstdint>
#include <openvr_driver.h>
#include "src/input/InputCommons.h"
#include "src/steamvr/skeletal_input_joints.h"
#include <chrono>
#include <atomic>
#include <memory>
#include <optional>

namespace HOL
{
	class EmulatedTrackerDriver; // Forward declaration

	class HookedController : public GenericControllerInterface
	{
		friend class HandOfLesser;

	public:
		// Records what the send thread has already published for one hand source.
		struct ForwardedHandState
		{
			uint32_t sourceDeviceId = vr::k_unTrackedDeviceIndexInvalid;
			uint64_t poseGeneration = 0;
			uint64_t skeletonGeneration = 0;
			bool hasSentState = false;
			bool active = false;
		};

		struct ForwardedHandUpdates
		{
			std::optional<HOL::SteamVRHandBaselinePayload> baseline;
			std::optional<HOL::SteamVRHandPosePayload> pose;
			std::chrono::steady_clock::time_point staleDeadline
				= (std::chrono::steady_clock::time_point::max)();
		};

		// Records the HMD state already sent so unchanged high-frequency poses are skipped.
		struct ForwardedHmdPoseState
		{
			uint32_t sourceDeviceId = vr::k_unTrackedDeviceIndexInvalid;
			uint64_t poseGeneration = 0;
			bool hasSentState = false;
			bool active = false;
		};

		HookedController(uint32_t id,
						 HandSide side,
						 vr::IVRServerDriverHost* host,
						 vr::ITrackedDeviceServerDriver* driver,
						 vr::PropertyContainerHandle_t propertyContainer);

		void lateInit(std::string serial,
					  vr::ETrackedDeviceClass deviceClass,
					  vr::ETrackedControllerRole role);

		void UpdatePose(HOL::HandTransformPayload* payload) override;
		void UpdateBoolInput(const std::string& input, bool value) override;
		void UpdateFloatInput(const std::string& input, float value) override;
		void UpdateSkeletal(HOL::SkeletalPayload* payload) override;
		void SubmitPose() override;
		void registerSkeletonInput(vr::VRInputComponentHandle_t handle,
								   vr::EVRSkeletalTrackingLevel level,
								   const std::string& path,
								   const std::string& basePosePath);
		bool isAugmentedSkeletonActive() const;
		bool isSuppressed() const;
		void setSuppressed(bool suppressed);

		void sendDisconnectState();
		void FlushDisconnectState();

		void setSide(HandSide side);
		HandSide getSide();
		void updateSideFromRole();

		void setLastOriginalPoseState(bool valid);
		bool nativePoseHealthy() const;
		bool cacheForwardedPose(const vr::DriverPose_t& pose, bool valid);
		std::optional<vr::DriverPose_t> getForwardedPose() const;
		bool cacheForwardedSkeleton(vr::EVRSkeletalMotionRange motionRange,
									const vr::VRBoneTransform_t* transforms,
									uint32_t transformCount);
		ForwardedHandUpdates getForwardedHandUpdates(
			ForwardedHandState& state,
			bool enabled,
			bool forceResync,
			std::chrono::steady_clock::time_point now) const;
		// Returns a packet only when the HMD pose, source, or active state needs publishing.
		std::optional<HOL::SteamVRHmdPosePayload>
		getForwardedHmdPoseUpdate(
			ForwardedHmdPoseState& state,
			bool enabled,
			bool forceResync) const;
		bool isHeld();
		Eigen::Vector3f getWorldPosition();

		vr::PropertyContainerHandle_t propertyContainer = 0;
		vr::ETrackedControllerRole role = vr::ETrackedControllerRole::TrackedControllerRole_Max;
		vr::ETrackedDeviceClass mDeviceClass = vr::ETrackedDeviceClass::TrackedDeviceClass_Max;
		std::string serial;
		vr::IVRDriverInput* driverInput;
		std::unordered_map<vr::VRInputComponentHandle_t, ControllerInputHandle> inputHandles;
		std::unordered_map<std::string, vr::VRInputComponentHandle_t> inputHandlesByName;

		uint32_t getDeviceId();
		void sendDeviceState();

		vr::DriverPose_t lastOriginalPose;
		bool mLastOriginalPoseValid = false;
		uint64_t mLastOriginalPoseSubmitTimeMs = 0;
		uint64_t mLastOriginalPoseAgeMs = 0;

		// Frames since last updated by original driver.
		// Used to detect runtimes that stop submitting poses instead of explicitly going invalid.
		int32_t framesSinceLastPoseUpdate = 0;

		// Shadow tracker support
		void setShadowTracker(EmulatedTrackerDriver* tracker);
		EmulatedTrackerDriver* getShadowTracker()
		{
			return mShadowTracker;
		}
		bool shouldActAsTracker();
		bool isActingAsTracker() const
		{
			return mActingAsTracker;
		}
		void setActingAsTracker(bool acting);

	private:
		// Hook callbacks replace immutable snapshots; the forwarding thread reads them lock-free.
		struct ForwardedPoseSnapshot
		{
			vr::DriverPose_t pose{};
			bool valid = false;
			uint64_t generation = 0;
			std::chrono::steady_clock::time_point lastChange{};
		};

		struct ForwardedSkeletonSnapshot
		{
			vr::VRBoneTransform_t transforms[SteamVR::HandSkeletonBone::eBone_Count]{};
			uint64_t generation = 0;
		};

		static constexpr int PoseStaleThresholdFrames = 2;
		static constexpr int HandTrackingDebounceTime = 50;
		static constexpr std::chrono::milliseconds ForwardedTrackingStaleTime{100};

		vr::DriverPose_t mLastPose;
		bool mLastHeldState
			= true; // Controllers not added until held, so should be true on activate
		std::chrono::steady_clock::time_point mLastStateChangeTime;
		const std::chrono::milliseconds STATE_CHANGE_DELAY_MS{500};

		HOL::HandTransformPayload mLastTransformPayload;
		HOL::ControllerInputPayload mLastInputPayload;

		bool mValidWhileOriginalInvalid = false;
		bool mHasHadValidOriginalPose = false;
		bool mHandTrackingTargetState = false;
		bool mHandTrackingState = false;
		int32_t mHandTrackingDebounceFrames = HandTrackingDebounceTime;

		HandSide mSide;
		vr::IVRServerDriverHost* mHookedHost;
		vr::ITrackedDeviceServerDriver* mHookedDriver;
		uint32_t mDeviceId;

		// State tracking for logging
		int32_t mFramesSinceLastDistanceLog = 0;
		vr::VRInputComponentHandle_t mSkeletonHandle = 0;
		vr::EVRSkeletalTrackingLevel mSkeletonTrackingLevel = vr::VRSkeletalTracking_Estimated;
		std::string mSkeletonInputPath;
		bool mLoggedMissingSkeletonHandle = false;
		vr::VRBoneTransform_t mSkeletalPose[SteamVR::HandSkeletonBone::eBone_Count]{};
		bool mSuppressed = false;
		bool mPendingDisconnectState = false;
		std::atomic<std::shared_ptr<const ForwardedPoseSnapshot>> mForwardedPose;
		std::atomic<std::shared_ptr<const ForwardedSkeletonSnapshot>> mForwardedSkeleton;
		std::atomic<uint64_t> mForwardedPoseGeneration = 0;
		std::atomic<uint64_t> mForwardedSkeletonGeneration = 0;

		// Shadow tracker support
		EmulatedTrackerDriver* mShadowTracker = nullptr;
		bool mActingAsTracker = false;
	};
} // namespace HOL
