#include "src/core/hand_of_lesser.h"
#include "hooked_controller.h"
#include "controller_common.h"
#include "src/hooking/hooks.h"
#include "src/tracker/emulated_tracker_driver.h"
#include <driverlog.h>
#include <src/utils/math_utils.h>
#include <algorithm>
#include <cstring>

namespace HOL
{
	namespace
	{
		bool sameForwardedPose(const vr::DriverPose_t& left, const vr::DriverPose_t& right)
		{
			return left.poseTimeOffset == right.poseTimeOffset
				   && std::memcmp(left.vecPosition, right.vecPosition, sizeof(left.vecPosition)) == 0
				   && std::memcmp(&left.qRotation, &right.qRotation, sizeof(left.qRotation)) == 0
				   && std::memcmp(left.vecVelocity, right.vecVelocity, sizeof(left.vecVelocity)) == 0
				   && std::memcmp(left.vecAngularVelocity,
							  right.vecAngularVelocity,
							  sizeof(left.vecAngularVelocity))
						  == 0
				   && std::memcmp(left.vecWorldFromDriverTranslation,
							  right.vecWorldFromDriverTranslation,
							  sizeof(left.vecWorldFromDriverTranslation))
						  == 0
				   && std::memcmp(&left.qWorldFromDriverRotation,
							  &right.qWorldFromDriverRotation,
							  sizeof(left.qWorldFromDriverRotation))
						  == 0
				   && std::memcmp(left.vecDriverFromHeadTranslation,
							  right.vecDriverFromHeadTranslation,
							  sizeof(left.vecDriverFromHeadTranslation))
						  == 0
				   && std::memcmp(&left.qDriverFromHeadRotation,
							  &right.qDriverFromHeadRotation,
							  sizeof(left.qDriverFromHeadRotation))
						  == 0;
		}

	} // namespace

	bool HookedController::isSuppressed() const
	{
		return mSuppressed;
	}

	void HookedController::setSuppressed(bool suppressed)
	{
		if (mSuppressed == suppressed)
		{
			return;
		}

		mSuppressed = suppressed;

		if (suppressed)
		{
			mPendingDisconnectState = true;
		}
		else
		{
			mPendingDisconnectState = false;
		}
	}

	void HookedController::sendDisconnectState()
	{
		vr::DriverPose_t disconnectPose = HOL::ControllerCommon::generateDisconnectedPose();
		HOL::hooks::TrackedDevicePoseUpdated::FunctionHook.originalFunc(
			this->mHookedHost, this->mDeviceId, disconnectPose, sizeof(vr::DriverPose_t));
	}

	void HookedController::FlushDisconnectState()
	{
		if (!mPendingDisconnectState)
		{
			return;
		}

		mPendingDisconnectState = false;
		sendDisconnectState();
	}

	HookedController::HookedController(uint32_t id,
									   HandSide side,
									   vr::IVRServerDriverHost* host,
									   vr::ITrackedDeviceServerDriver* driver,
									   vr::PropertyContainerHandle_t propertyContainer)
	{
		this->mSide = side;
		this->mDeviceId = id;
		this->mHookedHost = host;
		this->mHookedDriver = driver;
		this->propertyContainer = propertyContainer;
		this->mLastStateChangeTime = std::chrono::steady_clock::now();
	}

	void HookedController::lateInit(std::string serial,
									vr::ETrackedDeviceClass deviceClass,
									vr::ETrackedControllerRole role)
	{
		this->serial = serial;
		this->mDeviceClass = deviceClass;
		this->role = role;
	}

	void HookedController::registerSkeletonInput(vr::VRInputComponentHandle_t handle,
										 vr::EVRSkeletalTrackingLevel level,
										 const std::string& path,
										 const std::string& basePosePath)
	{
		mSkeletonHandle = handle;
		mSkeletonTrackingLevel = level;
		mSkeletonInputPath = path;
		mLoggedMissingSkeletonHandle = false;
		// Hooked-controller preference can depend on whether this device exposes partial or full
		// skeletal tracking, so refresh the cached choice once the level becomes known.
		HOL::HandOfLesser::Current->refreshPreferredHookedControllers();
		sendDeviceState();
		HOL::HandOfLesser::Current->sendStatus();
		DriverLog("Hooked controller %s registered skeleton input %s (base pose %s)",
				  serial.c_str(),
				  path.c_str(),
				  basePosePath.c_str());
	}

	void HookedController::UpdatePose(HOL::HandTransformPayload* payload)
	{
		this->mLastTransformPayload = *payload;

		// Do not update pose if invalid, because we want to continue submitting
		// the last valid one. Is this necessary? is there some kind of timeout?
		if (this->mLastTransformPayload.valid)
		{
			this->mLastPose = ControllerCommon::generatePose(&this->mLastTransformPayload, true);
		}
	}
	void HookedController::UpdateBoolInput(const std::string& input, bool value)
	{
		auto inputHandle = this->inputHandlesByName.find(input);
		if (inputHandle != this->inputHandlesByName.end())
		{
			hooks::UpdateBooleanComponent::FunctionHook.originalFunc(
				this->driverInput, (*inputHandle).second, value, 0.0);
		}
	}

	void HookedController::UpdateFloatInput(const std::string& input, float value)
	{
		auto inputHandle = this->inputHandlesByName.find(input);
		if (inputHandle != this->inputHandlesByName.end())
		{
			hooks::UpdateScalarComponent::FunctionHook.originalFunc(
				this->driverInput, (*inputHandle).second, value, 0.0);
		}
	}

	void HookedController::UpdateSkeletal(HOL::SkeletalPayload* payload)
	{
		if (payload == nullptr || payload->side != mSide)
		{
			return;
		}

		if (this->driverInput == nullptr)
		{
			return;
		}

		if (mSkeletonHandle == 0)
		{
			for (auto& [handle, input] : inputHandles)
			{
				if (input.type == ControllerInputType::Skeleton)
				{
					mSkeletonHandle = handle;
					mSkeletonInputPath = input.inputPath;
					break;
				}
			}
		}

		if (mSkeletonHandle == 0)
		{
			if (!mLoggedMissingSkeletonHandle)
			{
				DriverLog("Hooked controller %s missing skeleton handle", serial.c_str());
				mLoggedMissingSkeletonHandle = true;
			}
			return;
		}

		mLoggedMissingSkeletonHandle = false;

		HOL::SteamVR::buildSkeletalPoseFromPayload(*payload, mSkeletalPose);

		if (hooks::UpdateSkeletonComponent::FunctionHook.originalFunc != nullptr)
		{
			hooks::UpdateSkeletonComponent::FunctionHook.originalFunc(
				this->driverInput,
				mSkeletonHandle,
				vr::VRSkeletalMotionRange_WithoutController,
				mSkeletalPose,
				SteamVR::HandSkeletonBone::eBone_Count);
		}
		else
		{
			this->driverInput->UpdateSkeletonComponent(
				mSkeletonHandle,
				vr::VRSkeletalMotionRange_WithoutController,
				mSkeletalPose,
				SteamVR::HandSkeletonBone::eBone_Count);
		}
	}

	bool HookedController::isAugmentedSkeletonActive() const
	{
		const auto& config = HOL::HandOfLesser::Current->Config;
		if (!config.skeletal.augmentControllerSkeleton)
		{
			return false;
		}

		if (!HOL::HandOfLesser::Tracking.isMultimodalEnabled)
		{
			return false;
		}

		if (this->driverInput == nullptr || mSkeletonHandle == 0)
		{
			return false;
		}

		return true;
	}

	void HookedController::SubmitPose()
	{
		auto& config = HOL::HandOfLesser::Current->Config;

		if (!HOL::HandOfLesser::Current->shouldPossessPose(this))
		{
			return;
		}

		// In this state the native controller pose is no longer good enough, so call the
		// original function ourselves with our replacement pose instead.

		// If we are submitting a stale pose to lock it in place, we must jitter it
		// because vrchat is stupid and ignores all the status information steamvr provides.
		const auto& pose
			= (this->mLastTransformPayload.valid || !config.steamvr.jitterLastPoseOnTrackingLoss)
				  ? this->mLastPose
				  : HOL::ControllerCommon::addJitter(this->mLastPose);

		HOL::hooks::TrackedDevicePoseUpdated::FunctionHook.originalFunc(
			this->mHookedHost, this->mDeviceId, pose, sizeof(vr::DriverPose_t));
	}

	bool HookedController::isHeld()
	{
		const float HELD_THRESHOLD = 0.15f; // 15cm
		const float NOT_HELD_THRESHOLD_MULTIPLIER = 2.0f;

		float distance = HOL::HandOfLesser::Current->getControllerToHandDistance(this);

		if (distance > 99999) // Invalid
		{
			if (!this->mLastOriginalPoseValid && this->mLastTransformPayload.valid
				&& this->mLastTransformPayload.tracked)
			{
				mLastHeldState = false;
				mLastStateChangeTime = std::chrono::steady_clock::now();
				return false;
			}

			// Don't allow state changes when distance is invalid
			// Reset the "consistency timer" so we require 500ms of valid data
			mLastStateChangeTime = std::chrono::steady_clock::now();
			return mLastHeldState;
		}

		// Determine threshold based on current state (asymmetric)
		float threshold
			= mLastHeldState ? (HELD_THRESHOLD
								* NOT_HELD_THRESHOLD_MULTIPLIER) // Larger threshold to leave "held"
							 : HELD_THRESHOLD;					 // Normal threshold to enter "held"

		// Determine what the new state should be based on distance
		bool desiredState = distance < threshold;

		// If desired state matches current state, we're stable - reset timer
		if (desiredState == mLastHeldState)
		{
			mLastStateChangeTime = std::chrono::steady_clock::now();
			return mLastHeldState;
		}

		// Desired state differs - check if enough time has elapsed
		auto now = std::chrono::steady_clock::now();
		auto timeSinceLastChange
			= std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastStateChangeTime);

		if (timeSinceLastChange >= STATE_CHANGE_DELAY_MS)
		{
			// Enough time has passed, make the switch
			mLastHeldState = desiredState;
			mLastStateChangeTime = now;
		}

		return mLastHeldState;
	}

	// Assuming other external conditions also say it should.
	void HookedController::setSide(HandSide side)
	{
		this->mSide = side;
		HOL::HandOfLesser::Current->refreshPreferredHookedControllers();
	}

	HandSide HookedController::getSide()
	{
		return mSide;
	}

	void HookedController::updateSideFromRole()
	{
		// Devices that do not have a pre-determined left/right role
		// also have no way of telling which side they've been assigned to.
		// Best you can do is ask SteamVR what the current controller for whichever side
		// is, and check if that corresponds to any given controller.

		// Get the role
		auto props = vr::VRProperties();
		vr::PropertyContainerHandle_t container
			= props->TrackedDeviceToPropertyContainer(this->mDeviceId);

		// Now we can get the role ( except for vive wands maybe )
		vr::ETrackedControllerRole role = (vr::ETrackedControllerRole)props->GetInt32Property(
			container, vr::Prop_ControllerRoleHint_Int32);

		HandSide side = HandSide::HandSide_MAX;
		switch (role)
		{
			case vr::ETrackedControllerRole::TrackedControllerRole_LeftHand:
				side = HandSide::LeftHand;
				break;
			case vr::ETrackedControllerRole::TrackedControllerRole_RightHand:
				side = HandSide::RightHand;
				break;
			default:
				side = HandSide::HandSide_MAX;
		}

		if (mSide != side)
		{
			if (side == HandSide_MAX)
			{
				DriverLog("Controller unassigned side somehow.");
			}
			else
			{
				DriverLog("Controller assigned new side: %s",
						  side == HandSide::LeftHand ? "Left" : "Right");
			}
		}
	}

	void HookedController::setLastOriginalPoseState(bool valid)
	{
		const bool validityChanged = this->mLastOriginalPoseValid != valid;
		const bool firstValidPose = valid && !this->mHasHadValidOriginalPose;
		this->mLastOriginalPoseValid = valid;
		if (valid)
		{
			this->mHasHadValidOriginalPose = true;
		}

		if (firstValidPose || (HOL::HandOfLesser::Runtime.isSteamVR && validityChanged))
		{
			HOL::HandOfLesser::Current->refreshPreferredHookedControllers();
		}
	}

	bool HookedController::nativePoseHealthy() const
	{
		return this->mLastOriginalPoseValid
			   && this->framesSinceLastPoseUpdate <= PoseStaleThresholdFrames;
	}

	bool HookedController::cacheForwardedPose(const vr::DriverPose_t& pose, bool valid)
	{
		auto previous = mForwardedPose.load();
		const bool changed
			= !previous || previous->valid != valid || !sameForwardedPose(previous->pose, pose);
		if (!changed)
		{
			// SteamVR drivers may repeatedly submit an identical frozen pose after tracking is lost.
			// Do not refresh lastChange, because the forwarding thread uses it to detect that case.
			return false;
		}

		auto snapshot = std::make_shared<ForwardedPoseSnapshot>();
		snapshot->pose = pose;
		snapshot->valid = valid;
		snapshot->lastChange = std::chrono::steady_clock::now();
		snapshot->generation = mForwardedPoseGeneration.fetch_add(1) + 1;
		mForwardedPose.store(std::move(snapshot));
		return true;
	}

	std::optional<vr::DriverPose_t> HookedController::getForwardedPose() const
	{
		auto snapshot = mForwardedPose.load();
		if (!snapshot || !snapshot->valid)
		{
			return std::nullopt;
		}

		return snapshot->pose;
	}

	bool HookedController::cacheForwardedSkeleton(vr::EVRSkeletalMotionRange motionRange,
										 const vr::VRBoneTransform_t* transforms,
										 uint32_t transformCount)
	{
		if (mSkeletonTrackingLevel != vr::VRSkeletalTracking_Full
			|| motionRange != vr::VRSkeletalMotionRange_WithoutController
			|| transforms == nullptr
			|| transformCount != SteamVR::HandSkeletonBone::eBone_Count)
		{
			return false;
		}

		auto previous = mForwardedSkeleton.load();
		const size_t transformBytes
			= sizeof(vr::VRBoneTransform_t) * SteamVR::HandSkeletonBone::eBone_Count;
		const bool changed
			= !previous || std::memcmp(previous->transforms, transforms, transformBytes) != 0;
		if (!changed)
		{
			// Skeletons can update less often than controller poses. Their cadence is not used to
			// decide tracking loss; a changed skeleton only triggers a new full baseline.
			return false;
		}

		auto snapshot = std::make_shared<ForwardedSkeletonSnapshot>();
		std::copy(transforms,
				  transforms + SteamVR::HandSkeletonBone::eBone_Count,
				  snapshot->transforms);
		snapshot->generation = mForwardedSkeletonGeneration.fetch_add(1) + 1;
		mForwardedSkeleton.store(std::move(snapshot));
		return true;
	}

	HookedController::ForwardedHandUpdates HookedController::getForwardedHandUpdates(
		ForwardedHandState& state,
		bool enabled,
		bool forceResync,
		std::chrono::steady_clock::time_point now) const
	{
		if (forceResync)
		{
			state = {};
		}

		ForwardedHandUpdates updates;
		auto poseSnapshot = mForwardedPose.load();
		auto skeletonSnapshot = mForwardedSkeleton.load();
		// Full skeletal input identifies an actual hand source, while pose freshness determines
		// whether that source is still tracked.
		const bool active = enabled && poseSnapshot && skeletonSnapshot && poseSnapshot->valid
			&& now - poseSnapshot->lastChange <= ForwardedTrackingStaleTime;
		if (!active)
		{
			if (state.active || !state.hasSentState)
			{
				SteamVRHandBaselinePayload payload;
				payload.side = mSide;
				payload.sourceDeviceId = state.sourceDeviceId;
				payload.poseGeneration = state.poseGeneration;
				payload.skeletonGeneration = state.skeletonGeneration;
				updates.baseline = payload;
				state.hasSentState = true;
				state.active = false;
			}
			return updates;
		}

		updates.staleDeadline = poseSnapshot->lastChange + ForwardedTrackingStaleTime;
		const bool sourceChanged = state.sourceDeviceId != mDeviceId;
		// A source or skeleton change invalidates pose-only deltas, so establish a new baseline.
		if (!state.active || sourceChanged
			|| state.skeletonGeneration != skeletonSnapshot->generation)
		{
			SteamVRHandBaselinePayload payload;
			payload.side = mSide;
			payload.active = true;
			payload.sourceDeviceId = mDeviceId;
			payload.poseGeneration = poseSnapshot->generation;
			payload.skeletonGeneration = skeletonSnapshot->generation;
			payload.pose = poseSnapshot->pose;
			std::copy(std::begin(skeletonSnapshot->transforms),
					  std::end(skeletonSnapshot->transforms),
					  std::begin(payload.transforms));
			updates.baseline = payload;
			state.sourceDeviceId = payload.sourceDeviceId;
			state.poseGeneration = payload.poseGeneration;
			state.skeletonGeneration = payload.skeletonGeneration;
			state.hasSentState = true;
			state.active = true;
			return updates;
		}

		if (state.poseGeneration != poseSnapshot->generation)
		{
			SteamVRHandPosePayload payload;
			payload.side = mSide;
			payload.active = true;
			payload.sourceDeviceId = mDeviceId;
			payload.poseGeneration = poseSnapshot->generation;
			payload.pose = poseSnapshot->pose;
			updates.pose = payload;
			state.poseGeneration = payload.poseGeneration;
		}

		return updates;
	}

	Eigen::Vector3f HookedController::getWorldPosition()
	{
		// The position in the pose is before driver offsets have been applied.
		// Apply them to get the world position so we can more easily compare it.
		Eigen::Vector3f position = HOL::ovrVectorToEigen(lastOriginalPose.vecPosition);
		Eigen::Quaternionf rotation = HOL::ovrQuaternionToEigen(lastOriginalPose.qRotation);

		// TODO: Check pose validity?
		ControllerCommon::applyDriverOffset(position, rotation, lastOriginalPose);

		return position;
	}

	uint32_t HookedController::getDeviceId()
	{
		return this->mDeviceId;
	}

	void HookedController::sendDeviceState()
	{
		HOL::HandOfLesser::Current->sendDeviceState(this);
	}

	void HookedController::setShadowTracker(EmulatedTrackerDriver* tracker)
	{
		mShadowTracker = tracker;
	}

	void HookedController::setActingAsTracker(bool acting)
	{
		mActingAsTracker = acting;
	}

	bool HookedController::shouldActAsTracker()
	{
		// Look up device config by serial
		auto it = HOL::HandOfLesser::Config.deviceSettings.devices.find(serial);
		if (it == HOL::HandOfLesser::Config.deviceSettings.devices.end())
			return false;

		const auto& deviceConfig = it->second;
		if (!deviceConfig.actAsTracker)
			return false;

		// If "also when held" is set, always act as tracker
		if (deviceConfig.alsoWhenHeld)
			return true;

		// Default: only act as tracker when NOT held (requires multimodal)
		if (!HOL::HandOfLesser::Tracking.isMultimodalEnabled)
			return true; // No multimodal = can't detect held, so just act as tracker

		return !isHeld(); // Act as tracker only when NOT held
	}

} // namespace HOL
