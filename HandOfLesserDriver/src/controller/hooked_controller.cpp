#include "src/core/hand_of_lesser.h"
#include "hooked_controller.h"
#include "controller_common.h"
#include "src/hooking/hooks.h"
#include "src/tracker/emulated_tracker_driver.h"
#include <driverlog.h>
#include <src/utils/math_utils.h>

namespace HOL
{
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
												 const std::string& path)
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
		DriverLog(
			"Hooked controller %s registered skeleton input %s", serial.c_str(), path.c_str());
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

		HOL::ControllerCommon::buildSkeletalPoseFromPayload(*payload, mSkeletalPose);

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

	// Can, not should.
	bool HookedController::canPossess()
	{
		// Only controllers should ever be possessed - never HMD or other tracked devices
		if (mDeviceClass != vr::TrackedDeviceClass_Controller)
		{
			return false;
		}

		// Whether or not the pose is valid pretty much.
		return this->mLastTransformPayload.valid;
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
		this->mLastOriginalPoseValid = valid;
		if (valid)
		{
			this->mHasHadValidOriginalPose = true;
		}
	}

	bool HookedController::nativePoseHealthy() const
	{
		return this->mLastOriginalPoseValid
			   && this->framesSinceLastPoseUpdate <= PoseStaleThresholdFrames;
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
