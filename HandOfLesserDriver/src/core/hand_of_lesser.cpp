#include "hand_of_lesser.h"
#include "HandOfLesserCommon.h"
#include <driverlog.h>
#include "src/controller/emulated_controller_driver.h"
#include "src/controller/generic_control_interface.h"
#include "src/hooking/hooks.h"
#include "src/steamvr/hand_tip_pose.h"
#include "src/steamvr/input_wrapper.h"
#include "src/utils/math_utils.h"
#include <nlohmann/json.hpp>
#include <src/json/types.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <set>

namespace HOL
{
	HandOfLesser* HandOfLesser::Current = nullptr;
	HOL::settings::HandOfLesserSettings HandOfLesser::Config;
	HOL::state::TrackingState HandOfLesser::Tracking;
	HOL::state::RuntimeState HandOfLesser::Runtime;

	static void EnforceRuntimeRestrictions(settings::HandOfLesserSettings& settings)
	{
		if (!HandOfLesser::Runtime.isSteamVR)
		{
			return;
		}

		// SteamVR hand-tracking controllers are the source for forwarded hand data. Possessing
		// those devices would also expose their nonstandard input profile, so use separate
		// emulated controllers instead.
		if (settings.handPose.controllerMode == ControllerMode::HookedControllerMode)
		{
			settings.handPose.controllerMode = ControllerMode::NoControllerMode;
		}
	}

	HandOfLesser::HandOfLesser()
	{
		mHookedControllers.store(std::make_shared<const HookedControllerList>());
	}

	void HandOfLesser::init()
	{
		this->mActive.store(true);
		HandOfLesser::Current = this;

		// Initialize transport as server (creates named pipe and waits for client)
		this->mTransport.init(PipeRole::Server, R"(\\.\pipe\HandOfLesser)");

		mSteamVRHandTrackingThread = std::thread(&HandOfLesser::steamVRHandTrackingThread, this);
		my_pose_update_thread_ = std::thread(&HandOfLesser::ReceiveDataThread, this);
		mAppLauncher.start();
	}

	void HandOfLesser::ReceiveDataThread()
	{
		DriverLog("ReceiveDataThread() start, active: %s",
				  this->mActive.load() ? "true" : "false");

		// Wait for client connection (app to connect to our pipe)
		DriverLog("Waiting for app to connect...");
		while (this->mActive.load() && !this->mTransport.isConnected())
		{
				if (this->mTransport.waitForConnection(1000))
				{
					DriverLog("App connected!");

					// Send initialization message
					DriverInitializedPayload initPayload;
					this->mTransport.sendPayload<NativePacketType::DriverInitialized>(initPayload);
					requestSteamVRHandTrackingResync();
				DriverLog("Sent DriverInitialized payload");
				break;
			}
		}

		while (this->mActive.load())
		{
			HOL::NativePacketView nativePacket = this->mTransport.receivePacket();
			if (!nativePacket)
			{
				// Check if disconnected
				if (!this->mTransport.isConnected())
				{
					disableAppDrivenState();
					DriverLog("App disconnected, waiting for reconnection...");
						if (this->mTransport.waitForConnection(1000))
						{
							DriverLog("App reconnected!");

							// Send initialization message again
							DriverInitializedPayload initPayload;
							this->mTransport.sendPayload<NativePacketType::DriverInitialized>(
								initPayload);
							requestSteamVRHandTrackingResync();
						DriverLog("Sent DriverInitialized payload");
					}
				}
				continue;
			}
			switch (nativePacket.packetType)
			{
				case HOL::NativePacketType::HandTransform: {
					HOL::HandTransformPayload payload;
					if (!nativePacket.copyPayload(payload))
					{
						break;
					}

					// Ehhh something might send garbage data and get lucky
					if (payload.side != HOL::HandSide::LeftHand
						&& payload.side != HOL::HandSide::RightHand)
					{
						break;
					}

					const int sideIndex = static_cast<int>(payload.side);
					mLastHandTransforms[sideIndex] = payload;
					mHasHandTransform[sideIndex] = payload.valid;

					if (Config.handPose.controllerMode != ControllerMode::HookedControllerMode)
					{
						if (auto hooked = getHookedController(payload.side))
						{
							hooked->UpdatePose(&payload);
						}
					}

					std::shared_ptr<HookedController> hookedControllerOwner;
					GenericControllerInterface* controller
						= this->GetActiveController(payload.side, hookedControllerOwner);
					if (controller != nullptr)
					{
						controller->UpdatePose(&payload);
						if (payload.valid)
						{
							updateHandTipPose(payload.side, payload.location);
						}
						controller->SubmitPose();
					}

					updateControllerConnectionStates();

					break;
				}

				case HOL::NativePacketType::Settings: {
					if (nativePacket.payload == nullptr || nativePacket.payloadSize == 0)
					{
						break;
					}

					try
					{
						HOL::settings::HandOfLesserSettings oldSettings = Config;

						// Parse JSON from payload
						nlohmann::json j = nlohmann::json::parse(
							nativePacket.payload, nativePacket.payload + nativePacket.payloadSize);
						HandOfLesser::Config = j.get<HOL::settings::HandOfLesserSettings>();
						persistAutoLaunchSetting();
						EnforceRuntimeRestrictions(HandOfLesser::Config);

						// Handle any configuration changes
						handleConfigurationChange(oldSettings);
					}
					catch (const std::exception& ex)
					{
						DriverLog("Failed to parse settings JSON: %s", ex.what());
					}

					break;
				}

				case HOL::NativePacketType::FloatInput: {
					HOL::FloatInputPayload payload;
					if (!nativePacket.copyPayload(payload))
					{
						break;
					}
					std::shared_ptr<HookedController> hookedControllerOwner;
					auto controller
						= this->GetActiveController(payload.side, hookedControllerOwner);
					if (controller != nullptr)
					{
						controller->UpdateFloatInput(payload.inputName, payload.value);
					}

					break;
				}

				case HOL::NativePacketType::BoolInput: {

					HOL::BoolInputPayload payload;
					if (!nativePacket.copyPayload(payload))
					{
						break;
					}
					std::shared_ptr<HookedController> hookedControllerOwner;
					auto controller
						= this->GetActiveController(payload.side, hookedControllerOwner);
					if (controller != nullptr)
					{
						controller->UpdateBoolInput(payload.inputName, payload.value);
					}

					break;
				}

				case HOL::NativePacketType::SkeletalInput: {
					HOL::SkeletalPayload payload;
					if (!nativePacket.copyPayload(payload))
					{
						break;
					}

					// Send to active controller (for normal skeletal input)
					std::shared_ptr<HookedController> hookedControllerOwner;
					auto activeController
						= this->GetActiveController(payload.side, hookedControllerOwner);
					if (activeController != nullptr)
					{
						activeController->UpdateSkeletal(&payload);
					}
					else
					{
						// send to hooked controller when augmentation enabled
						if (Config.skeletal.augmentControllerSkeleton
							&& Tracking.isMultimodalEnabled)
						{
							auto hookedController = getHookedController(payload.side);
							if (hookedController != nullptr)
							{
								hookedController->UpdateSkeletal(&payload);
							}
						}
					}

					break;
				}

				case HOL::NativePacketType::MultimodalPose: {
					if (!nativePacket.copyPayload(mLastMultimodalPosePayload))
					{
						break;
					}

					break;
				}

				case HOL::NativePacketType::BodyTrackerPose: {
					HOL::BodyTrackerPosePayload payload;
					if (!nativePacket.copyPayload(payload))
					{
						break;
					}

					// Find the tracker for this role
					auto it = mEmulatedTrackers.find(payload.role);
					if (it != mEmulatedTrackers.end())
					{
						it->second->UpdatePose(payload);
						it->second->SubmitPose();
					}

					break;
				}

				case HOL::NativePacketType::State: {
					HOL::StatePayload payload;
					if (!nativePacket.copyPayload(payload))
					{
						break;
					}

					const bool steamVRRuntimeChanged
						= Runtime.isSteamVR != payload.runtime.isSteamVR;
					Tracking = payload.tracking;
					Runtime = payload.runtime;
					if (steamVRRuntimeChanged)
					{
						refreshPreferredHookedControllers();
						requestSteamVRHandTrackingResync();
					}

					updateControllerConnectionStates();
					break;
				}

				case HOL::NativePacketType::AppInitialized: {
					HOL::AppInitializedPayload payload;
					if (!nativePacket.copyPayload(payload))
					{
						break;
					}

					DriverLog("App initialized, sending current device state");
					requestSteamVRHandTrackingResync();
					sendAllDeviceStates();
					sendAllDeviceInputInfo();
					sendStatus();
					break;
				}

				default: {
					// Invalid packet type!
				}
			}
		}
	}

	void HandOfLesser::disableAppDrivenState()
	{
		// A reconnecting app may start with no valid hand pose and therefore have no transition
		// packet to send. Clear the previous app's tracking decisions here instead of carrying them
		// into the next connection.
		for (int side = 0; side < HOL::HandSide_MAX; ++side)
		{
			mLastHandTransforms[side] = {};
			mHasHandTransform[side] = false;
		}
		mLastMultimodalPosePayload = {};
		Tracking = {};

		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			if (controller->mDeviceClass != vr::TrackedDeviceClass_Controller)
			{
				continue;
			}

			controller->mValidWhileOriginalInvalid = false;
			controller->mHandTrackingTargetState = false;
			controller->mHandTrackingState = false;
			controller->mHandTrackingDebounceFrames = HookedController::HandTrackingDebounceTime;
			if (!controller->isActingAsTracker())
			{
				controller->setSuppressed(false);
			}
		}

		if (Config.handPose.controllerMode == ControllerMode::NoControllerMode
			&& !Config.skeletal.augmentControllerSkeleton
			&& !Config.bodyTrackers.enableBodyTrackers)
		{
			return;
		}

		HOL::settings::HandOfLesserSettings oldConfig = Config;
		Config.handPose.controllerMode = ControllerMode::NoControllerMode;
		Config.skeletal.augmentControllerSkeleton = false;
		Config.bodyTrackers.enableBodyTrackers = false;
		handleConfigurationChange(oldConfig);
	}

	void HandOfLesser::persistAutoLaunchSetting()
	{
		vr::EVRSettingsError error = vr::VRSettingsError_None;
		vr::VRSettings()->SetBool("driver_00handoflesser",
								  "autoLaunchApp",
								  Config.steamvr.autoLaunchApp,
								  &error);
		if (error != vr::VRSettingsError_None)
		{
			DriverLog("Failed to persist autoLaunchApp setting. value=%d, settingsError=%d",
					  Config.steamvr.autoLaunchApp,
					  error);
			return;
		}

		DriverLog("Persisted autoLaunchApp setting: %d", Config.steamvr.autoLaunchApp);
	}

	void HandOfLesser::handleConfigurationChange(HOL::settings::HandOfLesserSettings& oldConfig)
	{
		bool devicesChanged = false;
		mHasConfiguredShadowTrackers = false;
		for (const auto& [serial, device] : Config.deviceSettings.devices)
		{
			if (device.actAsTracker)
			{
				mHasConfiguredShadowTrackers = true;
				break;
			}
		}

		// Add or remove ( or enable/disable ) emulated controllers on controller mode change
		if (Config.handPose.controllerMode != oldConfig.handPose.controllerMode)
		{
			devicesChanged = true;
			if (Config.handPose.controllerMode == ControllerMode::EmulateControllerMode)
			{
				addEmulatedControllers();
			}
			else if (oldConfig.handPose.controllerMode == ControllerMode::EmulateControllerMode)
			{
				removeEmulatedControllers();
			}
		}

		if ((Config.handPose.emulatedControllerProfile
				 != oldConfig.handPose.emulatedControllerProfile
			 || Config.skeletal.trackingLevel != oldConfig.skeletal.trackingLevel)
			&& Config.handPose.controllerMode == ControllerMode::EmulateControllerMode)
		{
			devicesChanged = true;
			removeEmulatedControllers();
			addEmulatedControllers();
		}

		// Handle body tracker changes
		bool trackersEnabledChanged
			= Config.bodyTrackers.enableBodyTrackers != oldConfig.bodyTrackers.enableBodyTrackers;
		bool anyTrackerSettingChanged = Config.bodyTrackers.enabled != oldConfig.bodyTrackers.enabled;

		if (trackersEnabledChanged || anyTrackerSettingChanged)
		{
			devicesChanged = true;
			if (Config.bodyTrackers.enableBodyTrackers)
			{
				addEmulatedTrackers();
			}
			else
			{
				removeEmulatedTrackers();
			}
		}

		refreshPreferredHookedControllers();

		if (Config.steamvr.showDevicePoseDiagnostics && !oldConfig.steamvr.showDevicePoseDiagnostics)
		{
			sendAllDeviceStates();
		}

		// Update logic wouldn't normally run unless in certain modes, so force upon config change.
		updateControllerConnectionStates(true);
		updateTrackerConnectionStates();
		updateShadowTrackerStates();
		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			enforceTouchSuppression(controller.get());
		}

		// Only notify app if we actually changed the device list
		if (devicesChanged)
		{
			sendStatus();
		}
	}

	void HandOfLesser::addEmulatedControllers()
	{
		HOL::EmulatedControllerProfile profile = Config.handPose.emulatedControllerProfile;
		vr::EVRSkeletalTrackingLevel trackingLevel = getRequestedSkeletalTrackingLevel();
		HOL::EmulatedControllerVariant variant
			= HOL::getEmulatedControllerVariant(profile, trackingLevel);

		for (int i = 0; i < HOL::HandSide_MAX; i++)
		{
			// Existing controllers reomved on change, so if already there then should be right.
			if (this->mEmulatedControllers[i])
			{
				continue;
			}

			// Emulated controllers are cached by controller variant so the driver can switch between
			// profile/tracking-level combinations without recreating devices each time.
			auto& controllerSlot = this->mAllEmulatedControllers[variant][i];
			if (!controllerSlot)
			{
				controllerSlot = std::make_unique<EmulatedControllerDriver>(
					i == HOL::HandSide::LeftHand ? vr::TrackedControllerRole_LeftHand
												 : vr::TrackedControllerRole_RightHand,
					profile,
					trackingLevel);
				EmulatedControllerDriver* controller = controllerSlot.get();

				if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
						controller->MyGetSerialNumber().c_str(),
						vr::TrackedDeviceClass_Controller,
						controller))
				{
					DriverLog("Failed to create %s controller device!",
							  (i == 0 ? "left" : "right"));
					controllerSlot.reset();
					continue;
				}
			}

			this->mEmulatedControllers[i] = controllerSlot.get();
		}

		updateControllerConnectionStates();
	}

	// Removes from active slots and disconnects
	void HandOfLesser::removeEmulatedControllers()
	{
		for (int i = 0; i < HOL::HandSide_MAX; i++)
		{
			if (!this->mEmulatedControllers[i])
			{
				continue;
			}

			this->mEmulatedControllers[i]->setConnectedState(false);
			this->mEmulatedControllers[i] = nullptr;
		}
	}

	// Actually nukes
	void HandOfLesser::destroyEmulatedControllers()
	{
		removeEmulatedControllers();

		for (int variant = 0; variant < HOL::EmulatedControllerVariant_MAX; variant++)
		{
			for (int side = 0; side < HOL::HandSide_MAX; side++)
			{
				if (this->mAllEmulatedControllers[variant][side])
				{
					this->mAllEmulatedControllers[variant][side]->setConnectedState(false);
					this->mAllEmulatedControllers[variant][side].reset();
				}
			}
		}
	}

	void HandOfLesser::addEmulatedTrackers()
	{
		// Add trackers for each enabled role
		for (int i = 0; i < static_cast<int>(BodyTrackerRole::TrackerRole_MAX); i++)
		{
			BodyTrackerRole role = static_cast<BodyTrackerRole>(i);

			// Check if this tracker should be enabled (master switch + individual enable)
			bool shouldBeEnabled = Config.bodyTrackers.enableBodyTrackers
								   && Config.bodyTrackers.enabled[static_cast<int>(role)];

			if (!shouldBeEnabled)
				continue;

			// Check if tracker already exists
			if (mEmulatedTrackers.find(role) != mEmulatedTrackers.end())
			{
				// Tracker already exists, just ensure it's connected
				mEmulatedTrackers[role]->setConnectedState(true);
			}
			else
			{
				// Create new tracker
				auto tracker = std::make_unique<EmulatedTrackerDriver>(role);

				// Register with SteamVR
				if (vr::VRServerDriverHost()->TrackedDeviceAdded(
						tracker->MyGetSerialNumber().c_str(),
						vr::TrackedDeviceClass_GenericTracker,
						tracker.get()))
				{
					mEmulatedTrackers[role] = std::move(tracker);
					DriverLog("Added body tracker: %s", bodyTrackerRoleToString(role));
				}
				else
				{
					DriverLog("Failed to add body tracker: %s", bodyTrackerRoleToString(role));
				}
			}
		}

		updateTrackerConnectionStates();
	}

	void HandOfLesser::removeEmulatedTrackers()
	{
		// Disconnect all trackers
		for (auto& pair : mEmulatedTrackers)
		{
			pair.second->setConnectedState(false);
		}
	}

	void HandOfLesser::updateTrackerConnectionStates()
	{
		// Update each tracker's connection state based on settings
		for (auto& pair : mEmulatedTrackers)
		{
			BodyTrackerRole role = pair.first;
			bool shouldBeConnected = Config.bodyTrackers.enableBodyTrackers
									 && Config.bodyTrackers.enabled[static_cast<int>(role)];

			pair.second->setConnectedState(shouldBeConnected);
		}
	}

	void HandOfLesser::updateControllerConnectionStates(bool forceUpdate)
	{
		const bool emulationMode
			= Config.handPose.controllerMode == ControllerMode::EmulateControllerMode;
		const bool hookedMode
			= Config.handPose.controllerMode == ControllerMode::HookedControllerMode;

		if (hookedMode && forceUpdate)
		{
			auto hookedControllers = mHookedControllers.load();
			for (const auto& hooked : *hookedControllers)
			{
				if (hooked->mDeviceClass != vr::TrackedDeviceClass_Controller)
				{
					continue;
				}

				if (hooked->isActingAsTracker())
				{
					continue;
				}

				// We don't want to spam disconnect signals, but we do want to force the hooked
				// controllers back through a clean reconnect path after a mode/settings change.
				// If the controllers are still active their next real pose submission will bring
				// them back immediately, so this is safe as a one-off reset.
				if (forceUpdate)
				{
					hooked->sendDisconnectState();
				}
			}
		}

		for (int i = 0; i < HOL::HandSide_MAX; ++i)
		{
			auto side = static_cast<HOL::HandSide>(i);

			if (forceUpdate || emulationMode)
			{
				const bool emulatedHandTrackingPrimary = emulationMode && isHandTrackingPrimary(side);

				for (const auto& hooked : getHookedControllers(side))
				{
					if (!hooked->isActingAsTracker())
					{
						const bool suppress = emulationMode ? emulatedHandTrackingPrimary
														   : shouldSuppressHookedController(
																 hooked.get());
						const bool wasSuppressed = hooked->isSuppressed();
						hooked->setSuppressed(suppress);
						if (emulationMode && forceUpdate && suppress && wasSuppressed
							&& Config.steamvr.disableOtherControllersWhileHandTracking)
						{
							hooked->sendDisconnectState();
						}
					}
				}

				// Just sets an internal bool to enabled so it accepts data,
				// and we can tell whether or not it was active when we disable it.
				if (auto emulated = getEmulatedController(side))
				{
					emulated->setConnectedState(emulatedHandTrackingPrimary);
				}


				// Ensure inactive controllers are disconnected
				HOL::EmulatedControllerVariant activeVariant = HOL::getEmulatedControllerVariant(
					Config.handPose.emulatedControllerProfile, getRequestedSkeletalTrackingLevel());
				for (int variant = 0; variant < HOL::EmulatedControllerVariant_MAX; variant++)
				{
					if (variant == activeVariant)
					{
						continue;
					}

					auto& inactive = mAllEmulatedControllers[variant][i];
					if (inactive)
					{
						inactive->setConnectedState(false);
					}
				}
			}
		}
	}

	HookedController*
	HandOfLesser::addHookedController(uint32_t id,
									  vr::IVRServerDriverHost* host,
									  vr::ITrackedDeviceServerDriver* driver,
									  vr::PropertyContainerHandle_t propertyContainer)
	{
		auto controller = std::make_shared<HookedController>(
			id, HandSide::HandSide_MAX, host, driver, propertyContainer);
		HookedController* newController = controller.get();

		// SteamVR serializes device activation, making this collection single-writer.
		auto updatedControllers
			= std::make_shared<HookedControllerList>(*mHookedControllers.load());
		updatedControllers->push_back(std::move(controller));
		mHookedControllers.store(std::move(updatedControllers));
		return newController;
	}

	// We don't bother removing devices when they're deactivated at the moment, since we
	// may went to continue controlling them. This may mean they can be activated again.
	// We can't identify them until they have been fully activated, so once they have been
	// fully activated and populated, remove older duplicates from the published device list.
	void HandOfLesser::removeDuplicateDevices()
	{
		std::unordered_map<std::string, int> existingSerials;
		auto updatedControllers
			= std::make_shared<HookedControllerList>(*mHookedControllers.load());

		// count duplicates
		for (const auto& controller : *updatedControllers)
		{
			existingSerials[controller->serial]++;
		}

		// Starting from oldest, delete while duplicate count > 1
		auto it = updatedControllers->begin();
		while (it != updatedControllers->end())
		{
			if (existingSerials[(*it)->serial] > 1)
			{
				std::string serial = (*it)->serial;
				DriverLog("Removing duplicate device with serial: %s", serial.c_str());

				existingSerials[serial]--;
				it = updatedControllers->erase(it);
			}
			else
			{
				it++;
			}
		}

		mHookedControllers.store(std::move(updatedControllers));
		refreshPreferredHookedControllers();
	}

	bool HandOfLesser::shouldPossessInput(uint32_t deviceId)
	{
		if (HandOfLesser::Current->Config.handPose.controllerMode
			== ControllerMode::HookedControllerMode)
		{
			return shouldPossessInput(getHookedControllerByDeviceId(deviceId).get());
		}

		return false;
	}

	bool HandOfLesser::shouldPossessInput(HookedController* controller)
	{
		if (HandOfLesser::Current->Config.handPose.controllerMode
			!= ControllerMode::HookedControllerMode)
		{
			return false;
		}

		// Only the selected controller for a side should ever be possessed.
		if (controller == nullptr
			|| controller != getHookedController(controller->getSide()).get())
		{
			return false;
		}

		if (!shouldUseHandTracking(controller))
		{
			return false;
		}

		if (Runtime.isSteamVR && !controller->nativePoseHealthy())
		{
			return false;
		}

		return true;
	}

	bool HandOfLesser::shouldPossessPose(HookedController* controller)
	{
		if (!shouldPossessInput(controller))
		{
			return false;
		}

		const bool fallbackOnlyActive
			= Config.handPose.possessionBehavior == PossessionBehavior_Fallback;
		return !fallbackOnlyActive || !controller->nativePoseHealthy();
	}

	bool HandOfLesser::shouldEmulateControllers()
	{
		return HandOfLesser::Current->Config.handPose.controllerMode
			   == ControllerMode::EmulateControllerMode;
	}

	bool HandOfLesser::shouldUseHandTracking(HookedController* controller)
	{
		if (controller == nullptr)
		{
			return false;
		}

		if (controller->mDeviceClass != vr::TrackedDeviceClass_Controller)
		{
			return false;
		}

		const auto& trackingState = HOL::HandOfLesser::Tracking;

		if (trackingState.isMultimodalEnabled)
		{
			if (Config.trackingFeatures.forceMultimodalHandPrimary)
			{
				return true;
			}

			// If controller is configured to always act as tracker (alsoWhenHeld),
			// the real controller will never come back, so keep using hand tracking
			auto it = Config.deviceSettings.devices.find(controller->serial);
			if (it != Config.deviceSettings.devices.end() && it->second.actAsTracker
				&& it->second.alsoWhenHeld)
			{
				return true; // Always use hand tracking
			}

			bool shouldPossess = !controller->isHeld();
			return shouldPossess;
		}

		// Recovery controller is the first pair of real controllers we encountered.
		// If a driver provides hand-tracking controllers we may be hooking 
		// those instead of the real controllers, but still want to use the real
		// controls to determine whether or not we should stop hand tracking entirely.
		auto recoveryControllerOwner = getRecoveryHookedController(controller->getSide());
		HookedController* recoveryController
			= recoveryControllerOwner ? recoveryControllerOwner.get() : controller;

		HandSide side = controller->getSide();
		const bool hasCurrentHandPose = side >= 0 && side < HOL::HandSide_MAX
										&& mHasHandTransform[side]
										&& mLastHandTransforms[side].valid;
		const bool currentHandTracked
			= hasCurrentHandPose && mLastHandTransforms[side].tracked;
		bool recoveryPoseValid = recoveryController->mLastOriginalPoseValid;
		bool recoveryPoseFresh = recoveryPoseValid
								 && recoveryController->framesSinceLastPoseUpdate
										<= HookedController::PoseStaleThresholdFrames;
		if (!recoveryPoseFresh && hasCurrentHandPose)
		{
			controller->mValidWhileOriginalInvalid = true;
		}

		if (recoveryPoseFresh)
		{
			controller->mValidWhileOriginalInvalid = false;
		}

		bool shouldUseHandTracking = currentHandTracked
									 || (hasCurrentHandPose && !recoveryPoseFresh)
									 || controller->mValidWhileOriginalInvalid;

		// Must remain in new target state for x frames before actually changing
		if (shouldUseHandTracking == controller->mHandTrackingTargetState)
		{
			controller->mHandTrackingDebounceFrames++;
			if (controller->mHandTrackingDebounceFrames
				> HookedController::HandTrackingDebounceTime)
			{
				controller->mHandTrackingDebounceFrames
					= HookedController::HandTrackingDebounceTime;
			}
		}
		else
		{
			controller->mHandTrackingTargetState = shouldUseHandTracking;
			controller->mHandTrackingDebounceFrames = 0;
		}

		if (controller->mHandTrackingDebounceFrames
			>= HookedController::HandTrackingDebounceTime)
		{
			controller->mHandTrackingState = controller->mHandTrackingTargetState;
		}

		return controller->mHandTrackingState;
	}

	bool HandOfLesser::shouldSuppressHookedController(HookedController* controller)
	{
		if (controller == nullptr)
		{
			return false;
		}

		if (Config.handPose.controllerMode != ControllerMode::HookedControllerMode)
		{
			return false;
		}

		if (controller->mDeviceClass != vr::TrackedDeviceClass_Controller)
		{
			return false;
		}

		if (controller->isActingAsTracker())
		{
			return false;
		}

		HandSide side = controller->getSide();
		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return false;
		}

		if (!isHandTrackingPrimary(side))
		{
			return false;
		}

		// Once a side is hand-tracking-primary, only the selected hooked controller for that
		// side stays live. Every other same-side controller should be hidden until native
		// control takes priority again.
		return controller != getHookedController(side).get();
	}

	bool HandOfLesser::isHandTrackingPrimary(HOL::HandSide side)
	{
		auto hooked = getHookedController(side);
		if (hooked != nullptr)
		{
			return shouldUseHandTracking(hooked.get());
		}

		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return false;
		}

		if (!mHasHandTransform[side])
		{
			return false;
		}

		const HOL::HandTransformPayload& payload = mLastHandTransforms[side];
		return payload.valid;
	}

	EmulatedControllerDriver* HandOfLesser::getEmulatedController(HOL::HandSide side)
	{
		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return nullptr;
		}

		return this->mEmulatedControllers[(int)side];
	}

	bool HandOfLesser::isEmulatedController(vr::ITrackedDeviceServerDriver* driver)
	{
		for (auto& variantControllers : mAllEmulatedControllers)
		{
			for (auto& controllerContainer : variantControllers)
			{
				EmulatedControllerDriver* controller = controllerContainer.get();
				if (controller == driver)
				{
					return true;
				}
			}
		}

		return false;
	}

	bool HandOfLesser::isEmulatedTracker(vr::ITrackedDeviceServerDriver* driver)
	{
		for (auto& tracker : mEmulatedTrackers)
		{
			EmulatedTrackerDriver* controller = tracker.second.get();
			if (controller == driver)
			{
				return true;
			}
		}

		return false;
	}

	bool HandOfLesser::isShadowTracker(vr::ITrackedDeviceServerDriver* driver)
	{
		for (auto& pair : mShadowTrackers)
		{
			if (pair.second.get() == driver)
			{
				return true;
			}
		}
		return false;
	}

	EmulatedTrackerDriver* HandOfLesser::getOrCreateShadowTracker(HookedController* controller)
	{
		const std::string& serial = controller->serial;

		auto it = mShadowTrackers.find(serial);
		if (it != mShadowTrackers.end())
		{
			// Ensure the controller has the pointer (may have been cleared)
			controller->setShadowTracker(it->second.get());
			return it->second.get();
		}

		// Create new shadow tracker
		auto tracker = std::make_unique<EmulatedTrackerDriver>(serial);

		// Register with SteamVR
		if (vr::VRServerDriverHost()->TrackedDeviceAdded(tracker->MyGetSerialNumber().c_str(),
														 vr::TrackedDeviceClass_GenericTracker,
														 tracker.get()))
		{
			EmulatedTrackerDriver* ptr = tracker.get();
			mShadowTrackers[serial] = std::move(tracker);
			controller->setShadowTracker(ptr);
			DriverLog("Created shadow tracker for: %s", serial.c_str());
			return ptr;
		}

		DriverLog("Failed to create shadow tracker for: %s", serial.c_str());
		return nullptr;
	}

	void HandOfLesser::updateShadowTrackerState(HookedController* controller)
	{
		bool shouldAct = controller->shouldActAsTracker();
		bool isActing = controller->isActingAsTracker();

		if (shouldAct && !isActing)
		{
			// Transition: controller → tracker
			auto* shadow = getOrCreateShadowTracker(controller);
			if (shadow)
			{
				shadow->setConnectedState(true);
				controller->setSuppressed(true);
				controller->setActingAsTracker(true);
				DriverLog("Controller %s now acting as tracker", controller->serial.c_str());
			}
		}
		else if (!shouldAct && isActing)
		{
			// Transition: tracker → controller
			// Try controller pointer first, fall back to map lookup
			EmulatedTrackerDriver* shadow = controller->getShadowTracker();
			if (!shadow)
			{
				auto it = mShadowTrackers.find(controller->serial);
				if (it != mShadowTrackers.end())
				{
					shadow = it->second.get();
				}
			}
			if (shadow)
			{
				shadow->setConnectedState(false);
			}
			controller->setSuppressed(false);
			controller->setActingAsTracker(false);
			DriverLog("Controller %s no longer acting as tracker", controller->serial.c_str());
		}
	}

	void HandOfLesser::updateShadowTrackerStates()
	{
		if (!mHasConfiguredShadowTrackers && mShadowTrackers.empty())
		{
			return;
		}

		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			updateShadowTrackerState(controller.get());
		}
	}

	// Only works if a side has been assigned
	// usually is, just not right after being activated.
	std::shared_ptr<HookedController> HandOfLesser::getHookedController(HOL::HandSide side)
	{
		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return nullptr;
		}

		return mPreferredHookedControllers[side].load();
	}

	std::vector<std::shared_ptr<HookedController>>
	HandOfLesser::getHookedControllers(HOL::HandSide side) const
	{
		std::vector<std::shared_ptr<HookedController>> controllers;

		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return controllers;
		}

		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			if (controller->mDeviceClass != vr::TrackedDeviceClass_Controller)
			{
				continue;
			}

			if (controller->getSide() != side)
			{
				continue;
			}

			controllers.push_back(controller);
		}

		return controllers;
	}

	void HandOfLesser::refreshPreferredHookedControllers()
	{
		// Controller selections only change when device availability, side assignment, or settings
		// change, so cache them together instead of rescanning the full hooked list every frame.
		for (int i = 0; i < HOL::HandSide_MAX; ++i)
		{
			HOL::HandSide side = static_cast<HOL::HandSide>(i);
			refreshPreferredHookedController(side);
			refreshRecoveryHookedController(side);
			refreshForwardedHandTrackingController(side);
		}
	}

	void HandOfLesser::refreshPreferredHookedController(HOL::HandSide side)
	{
		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return;
		}

		std::string preferredSerial
			= Runtime.isSteamVR ? "" : getPreferredHookedControllerSerial(side);
		auto bestController = findBestHookedController(
			side, getRequestedSkeletalTrackingLevel(), preferredSerial, false);

		auto previousController = mPreferredHookedControllers[side].load();
		if (previousController != bestController)
		{
			const char* sideName = side == HandSide::LeftHand ? "left" : "right";
			DriverLog("Preferred hooked %s controller changed: %s -> %s (tracking level %d)",
					  sideName,
					  previousController ? previousController->serial.c_str() : "(none)",
					  bestController ? bestController->serial.c_str() : "(none)",
					  bestController ? static_cast<int>(bestController->mSkeletonTrackingLevel)
									 : -1);
		}

		mPreferredHookedControllers[side].store(bestController);
	}

	void HandOfLesser::refreshForwardedHandTrackingController(HOL::HandSide side)
	{
		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return;
		}

		// Full skeletal tracking distinguishes native hand-tracking devices from normal controllers.
		// Only SteamVR runtime sessions need their data forwarded back to the application.
		auto bestController = Runtime.isSteamVR
			? findBestHookedController(side, vr::VRSkeletalTracking_Full, "", true)
			: nullptr;
		auto previousController = mForwardedHandTrackingControllers[side].load();
		mForwardedHandTrackingControllers[side].store(bestController);
		if (previousController != bestController)
		{
			const char* sideName = side == HandSide::LeftHand ? "left" : "right";
			DriverLog("Forwarded SteamVR %s hand source changed: %s -> %s",
					  sideName,
					  previousController ? previousController->serial.c_str() : "(none)",
					  bestController ? bestController->serial.c_str() : "(none)");
			notifySteamVRHandTracking();
		}
	}

	bool HandOfLesser::isForwardedHandTrackingController(
		const HookedController* controller) const
	{
		if (controller == nullptr || controller->mSide < HandSide::LeftHand
			|| controller->mSide >= HandSide::HandSide_MAX)
		{
			return false;
		}

		return mForwardedHandTrackingControllers[controller->mSide].load().get() == controller;
	}

	std::shared_ptr<HookedController> HandOfLesser::findBestHookedController(
		HOL::HandSide side,
		vr::EVRSkeletalTrackingLevel requestedTrackingLevel,
		const std::string& preferredSerial,
		bool requireExactTrackingLevel) const
	{
		auto controllers = getHookedControllers(side);
		if (!preferredSerial.empty())
		{
			for (const auto& controller : controllers)
			{
				if (controller->serial == preferredSerial)
				{
					return controller;
				}
			}
		}

		std::shared_ptr<HookedController> bestController;
		int bestScore = std::numeric_limits<int>::lowest();
		for (const auto& controller : controllers)
		{
			if (requireExactTrackingLevel
				&& controller->mSkeletonTrackingLevel != requestedTrackingLevel)
			{
				continue;
			}

			const int score
				= getHookedControllerSelectionScore(controller.get(), requestedTrackingLevel);
			if (score == std::numeric_limits<int>::lowest())
			{
				continue;
			}

			if (bestController == nullptr || score > bestScore
				|| (score == bestScore && controller->serial < bestController->serial))
			{
				bestController = controller;
				bestScore = score;
			}
		}

		return bestController;
	}

	void HandOfLesser::refreshRecoveryHookedController(HOL::HandSide side)
	{
		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return;
		}

		std::shared_ptr<HookedController> recoveryController;
		for (const auto& controller : getHookedControllers(side))
		{
			// Full skeletal tracking indicates the dedicated hand-tracking controller pair, which is
			// not useful as the signal that native controller tracking has returned.
			if (controller->mSkeletonTrackingLevel == vr::VRSkeletalTracking_Full)
			{
				continue;
			}

			auto configIt = Config.deviceSettings.devices.find(controller->serial);
			if (configIt != Config.deviceSettings.devices.end() && configIt->second.actAsTracker)
			{
				continue;
			}

			recoveryController = controller;
			break;
		}

		mRecoveryHookedControllers[side].store(std::move(recoveryController));
	}

	std::string HandOfLesser::getPreferredHookedControllerSerial(HOL::HandSide side) const
	{
		switch (side)
		{
			case HandSide::LeftHand:
				return Config.deviceSettings.preferredLeftControllerSerial;
			case HandSide::RightHand:
				return Config.deviceSettings.preferredRightControllerSerial;
			default:
				return "";
		}
	}

	std::shared_ptr<HookedController>
	HandOfLesser::getRecoveryHookedController(HOL::HandSide side) const
	{
		if (side < 0 || side >= HOL::HandSide_MAX)
		{
			return nullptr;
		}

		return mRecoveryHookedControllers[side].load();
	}

	vr::EVRSkeletalTrackingLevel HandOfLesser::getRequestedSkeletalTrackingLevel() const
	{
		return Config.skeletal.trackingLevel;
	}

	int HandOfLesser::getHookedControllerSelectionScore(
		HookedController* controller,
		vr::EVRSkeletalTrackingLevel requestedTrackingLevel) const
	{
		if (controller == nullptr)
		{
			return std::numeric_limits<int>::lowest();
		}

		int score = 0;

		if (Runtime.isSteamVR)
		{
			// SteamVR-runtime sources must be actively submitting a healthy native pose. Full skeletal
			// devices are preferred because they are the native hand-tracking controller pair.
			if (!controller->nativePoseHealthy())
			{
				return std::numeric_limits<int>::lowest();
			}

			if (controller->mSkeletonTrackingLevel == vr::VRSkeletalTracking_Full)
			{
				score += 1000;
			}
		}

		// Controllers that the user turned into trackers should not also become the primary
		// possessed controller.
		auto configIt = Config.deviceSettings.devices.find(controller->serial);
		if (configIt != Config.deviceSettings.devices.end() && configIt->second.actAsTracker)
		{
			score -= 1000;
		}

		bool wantsFullTracking = requestedTrackingLevel == vr::VRSkeletalTracking_Full;
		bool controllerProvidesFullTracking
			= controller->mSkeletonTrackingLevel == vr::VRSkeletalTracking_Full;

		// When both controller pairs exist, prefer the device whose native skeletal tracking
		// level best matches the current request.
		if (controllerProvidesFullTracking)
		{
			score += wantsFullTracking ? 100 : -100;
		}

		// If we have ever seen a real pose from this controller, prefer it slightly over an
		// otherwise-equal candidate that has never produced one.
		if (controller->mHasHadValidOriginalPose)
		{
			score += 10;
		}

		return score;
	}

	std::shared_ptr<HookedController>
	HandOfLesser::getHookedControllerByDeviceId(uint32_t deviceId)
	{
		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			if (controller->getDeviceId() == deviceId)
			{
				return controller;
			}
		}

		return nullptr;
	}

	std::shared_ptr<HookedController>
	HandOfLesser::getHookedControllerBySerial(std::string serial)
	{
		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			if (controller->serial == serial)
			{
				return controller;
			}
		}

		return nullptr;
	}

	std::shared_ptr<HookedController>
	HandOfLesser::getHookedControllerByPropertyContainer(vr::PropertyContainerHandle_t container)
	{
		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			if (controller->propertyContainer == container)
			{
				return controller;
			}
		}

		return nullptr;
	}

	std::shared_ptr<HookedController> HandOfLesser::getHMD()
	{
		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			if (controller->mDeviceClass == vr::ETrackedDeviceClass::TrackedDeviceClass_HMD)
			{
				return controller;
			}
		}

		return nullptr;
	}

	std::shared_ptr<HookedController>
	HandOfLesser::getHookedControllerByInputHandle(vr::VRInputComponentHandle_t inputHandle)
	{
		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			if (controller->inputHandles.contains(inputHandle))
			{
				return controller;
			}
		}

		return nullptr;
	}

	GenericControllerInterface* HandOfLesser::GetActiveController(
		HOL::HandSide side, std::shared_ptr<HookedController>& hookedControllerOwner)
	{
		hookedControllerOwner.reset();

		switch (HandOfLesser::Current->Config.handPose.controllerMode)
		{
			case ControllerMode::EmulateControllerMode: {
				EmulatedControllerDriver* emulated = getEmulatedController(side);
				if (emulated == nullptr)
				{
					return nullptr;
				}
				// If not connected then it is not the primary controller
				if (!emulated->isConnected())
				{
					return nullptr;
				}
				return emulated;
			}
			case ControllerMode::HookedControllerMode: {
				hookedControllerOwner = getHookedController(side);
				if (hookedControllerOwner == nullptr)
				{
					return nullptr;
				}

				// Not active if HandOfLesser should not own the hooked controller's input.
				if (!shouldPossessInput(hookedControllerOwner.get()))
				{
					return nullptr;
				}

				return hookedControllerOwner.get();
			}
			default: {
				return nullptr;
			}
		}
	}

	void HandOfLesser::requestEstimateControllerSide()
	{
		// Ideally we would just wait for the controller-has-been-assigned-a-side
		// event and immediately decide on what side each controller is.
		// However, OVR will send this event before the controller has
		// submitted a valid pose, resulting in it failing.
		// As a result of this we need to wait for all poses to be valid before
		// we run our estimation.
		this->mEstimateControllerSideWhenPositionValid = true;
	}

	void HandOfLesser::estimateControllerSide()
	{
		mControllerSideEstimationAttemptCount++;

		if (mControllerSideEstimationAttemptCount > 500)
		{
			this->mEstimateControllerSideWhenPositionValid = false;
			this->mControllerSideEstimationAttemptCount = 0;
			DriverLog("Waited 500 frames HDM and controllers to have valid positions, giving up on "
					  "deciding sides.");
			return;
		}

		// Vive wands don't know what side they are until later,
		// and we can't query what side they're assigned without
		// being a client, so instead we listen for the event
		// that says they've been assigned a side, and... guess.
		std::vector<HookedController*> unsidedControllers;
		auto hookedControllers = mHookedControllers.load();

		for (const auto& controller : *hookedControllers)
		{
			// Controller with no left/right role hint.
			// Only applies to Vive wand, and they use the invalid role.
			if (controller->mDeviceClass == vr::TrackedDeviceClass_Controller
				&& controller->role == vr::TrackedControllerRole_Invalid)
			{
				if (!controller->mLastOriginalPoseValid)
				{
					// DriverLog("Sided controller without valid position, skipping side
					// estimation");
					return;
				}

				unsidedControllers.push_back(controller.get());
			}
		}

		if (unsidedControllers.empty())
		{
			// uhhhhhh
			DriverLog("No unsided controllers, not peforming side estimation");
			this->mEstimateControllerSideWhenPositionValid = false;
			this->mControllerSideEstimationAttemptCount = 0;
		}

		if (unsidedControllers.size() > 2)
		{
			// uhhhhhh
			DriverLog("More than two unsided controller, so I give up. Count: %d",
					  unsidedControllers.size());
		}

		auto hmd = getHMD();
		if (hmd == nullptr)
		{
			DriverLog("Could not get HMD to do handedness math, so I give up.");
			return;
		}

		if (!hmd->mLastOriginalPoseValid)
		{
			DriverLog("HMD position not valid, will not estimate controller side");
			return;
		}

		// Cross forward with 0,1,0 to get x axis on flat plane
		// This will be our plane up
		Eigen::Vector3f hmdPos = HOL::ovrVectorToEigen(hmd->lastOriginalPose.vecPosition);
		Eigen::Quaternionf hmdRot = HOL::ovrQuaternionToEigen(hmd->lastOriginalPose.qRotation);

		Eigen::Quaternionf hmdHead
			= HOL::ovrQuaternionToEigen(hmd->lastOriginalPose.qWorldFromDriverRotation);
		hmdRot = hmdRot * hmdHead;

		Eigen::Vector3f hmdForward = hmdRot * Eigen::Vector3f(0, 0, 1);
		Eigen::Vector3f hmdSide = hmdForward.cross(Eigen::Vector3f(0, 1, 0));

		if (unsidedControllers.size() == 2)
		{
			// remove y component, get angle between hmd forward and hmd->controller.
			// lower of two values is left, higher is right.

			Eigen::Vector3f controller1Pos
				= HOL::ovrVectorToEigen(unsidedControllers[0]->lastOriginalPose.vecPosition);
			Eigen::Vector3f controller2Pos
				= HOL::ovrVectorToEigen(unsidedControllers[1]->lastOriginalPose.vecPosition);

			Eigen::Vector3f controller1Vector = controller1Pos - hmdPos;
			Eigen::Vector3f controller2Vector = controller2Pos - hmdPos;

			float controller1Distance = hmdSide.dot(controller1Vector);
			float controller2Distance = hmdSide.dot(controller2Vector);

			// The more above the plane the more right the controller is.
			// Higher number is right controller, other is left.
			// OR IT SHOULD BE BUT ITS OPPOSITE AND I GIVE UP
			// I just inverted the if/else
			if (controller1Distance > controller2Distance)
			{
				unsidedControllers[0]->setSide(HandSide::LeftHand);
				unsidedControllers[1]->setSide(HandSide::RightHand);

				DriverLog("Asssigned controller %s to left", unsidedControllers[0]->serial.c_str());
				DriverLog("Asssigned controller %s to right",
						  unsidedControllers[1]->serial.c_str());
			}
			else
			{
				unsidedControllers[0]->setSide(HandSide::RightHand);
				unsidedControllers[1]->setSide(HandSide::LeftHand);

				DriverLog("Asssigned controller %s to right",
						  unsidedControllers[0]->serial.c_str());
				DriverLog("Asssigned controller %s to left", unsidedControllers[1]->serial.c_str());
			}
		}
		else if (unsidedControllers.size() == 1)
		{
			// Same general idea, but anything to right of HMD will be right and vice versa
			Eigen::Vector3f controller1Pos
				= HOL::ovrVectorToEigen(unsidedControllers[0]->lastOriginalPose.vecPosition);

			Eigen::Vector3f controller1Vector = controller1Pos - hmdPos;

			float controller1Distance = hmdSide.dot(controller1Vector);

			// right if > 0, otherwise left
			// OR IT SHOULD BE BUT ITS OPPOSITE AND I GIVE UP
			// I just inverted the if/else
			if (controller1Distance > 0)
			{
				unsidedControllers[0]->setSide(HandSide::LeftHand);
				DriverLog("Asssigned controller %s to left", unsidedControllers[0]->serial.c_str());
			}
			else
			{

				unsidedControllers[0]->setSide(HandSide::RightHand);
				DriverLog("Asssigned controller %s to right",
						  unsidedControllers[0]->serial.c_str());
			}
		}

		this->mControllerSideEstimationAttemptCount = 0;
		this->mEstimateControllerSideWhenPositionValid = false;
	}

	void HandOfLesser::runFrame()
	{
		// As of writing only emulated controllers need to do anything here
		if (HandOfLesser::Current->Config.handPose.controllerMode
			== ControllerMode::EmulateControllerMode)
		{
			// Hand pose packets stop when tracking becomes stale. Advance the hand/controller
			// transition debounce here so the final tracked=false packet can still take effect.
			updateControllerConnectionStates();

			// TODO: only run if using index controller
			EmulatedControllerDriver* leftController = this->getEmulatedController(HandSide::LeftHand);
			EmulatedControllerDriver* rightController
				= this->getEmulatedController(HandSide::RightHand);

			// We always create both
			if (leftController != nullptr && rightController != nullptr)
			{
				// TODO: check if activate, if we do silly things like swap between controllers

				// call our devices to run a frame
				leftController->MyRunFrame();
				rightController->MyRunFrame();

				// Now, process events that were submitted for this frame.
				vr::VREvent_t vrevent{};
				while (vr::VRServerDriverHost()->PollNextEvent(&vrevent, sizeof(vr::VREvent_t)))
				{
					leftController->MyProcessEvent(vrevent);
					rightController->MyProcessEvent(vrevent);
				}
			}
		}
		updateShadowTrackerStates();

		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			controller->FlushDisconnectState();
		}

		for (auto& tracker : mShadowTrackers)
		{
			tracker.second->FlushPoseUpdate();
		}

		for (auto& tracker : mEmulatedTrackers)
		{
			tracker.second->FlushPoseUpdate();
		}

		if (this->mEstimateControllerSideWhenPositionValid)
		{
			this->estimateControllerSide();
		}

		if (Config.steamvr.showDevicePoseDiagnostics)
		{
			uint64_t nowMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::system_clock::now().time_since_epoch())
							 .count();
			for (const auto& controller : *hookedControllers)
			{
				if (controller->mLastOriginalPoseSubmitTimeMs == 0)
				{
					controller->mLastOriginalPoseAgeMs = 0;
				}
				else
				{
					controller->mLastOriginalPoseAgeMs
						= nowMs - controller->mLastOriginalPoseSubmitTimeMs;
				}
			}

			sendAllDeviceStates();
		}

		// iterate frame counter for all controller
		for (const auto& controller : *hookedControllers)
		{
			controller->framesSinceLastPoseUpdate++;
		}
	}

	void HandOfLesser::notifySteamVRHandTracking()
	{
		mSteamVRHandTrackingPending.store(true);
		mSteamVRHandTrackingCondition.notify_one();
	}

	void HandOfLesser::requestSteamVRHandTrackingResync()
	{
		mSteamVRHandTrackingResync.store(true);
		mSteamVRHandTrackingPending.store(true);
		mSteamVRHandTrackingCondition.notify_one();
	}

	void HandOfLesser::updateHandTipPose(HOL::HandSide side,
									 const HOL::PoseLocation& palmPose)
	{
		if (Config.handPose.controllerMode != ControllerMode::EmulateControllerMode
			|| Config.handPose.emulatedControllerProfile
				   != EmulatedControllerProfile::EmulatedControllerProfile_SteamLinkHand)
		{
			return;
		}

		auto* controller = getEmulatedController(side);
		const auto hmd = getHMD();
		const auto hmdPose = hmd ? hmd->getForwardedPose() : std::nullopt;
		if (controller == nullptr || !hmdPose)
		{
			return;
		}

		const auto tipPose
			= SteamVR::HandTipPoseGenerator::generate(
				side, palmPose, controller->GetPose(), *hmdPose);
		if (tipPose)
		{
			controller->UpdateTipPose(*tipPose);
		}
	}

	// Hooks only publish snapshots and wake this thread. Conversion and pipe I/O must not run on
	// another driver's pose/skeleton callback thread.
	void HandOfLesser::steamVRHandTrackingThread()
	{
		std::array<HookedController::ForwardedHandState, HandSide::HandSide_MAX>
			forwardingStates{};
		std::array<std::shared_ptr<HookedController>, HandSide::HandSide_MAX> sources{};
		auto nextWakeTime = (std::chrono::steady_clock::time_point::min)();
		while (mActive.load())
		{
			if (nextWakeTime != (std::chrono::steady_clock::time_point::min)())
			{
				std::unique_lock lock(mSteamVRHandTrackingMutex);
				const auto shouldWake = [this]() {
					return !mActive.load() || mSteamVRHandTrackingPending.load()
						|| mSteamVRHandTrackingResync.load();
				};
				if (nextWakeTime == (std::chrono::steady_clock::time_point::max)())
				{
					mSteamVRHandTrackingCondition.wait(lock, shouldWake);
				}
				else
				{
					mSteamVRHandTrackingCondition.wait_until(lock, nextWakeTime, shouldWake);
				}
			}

			if (!mActive.load())
			{
				break;
			}

			// Send a full baseline when the skeleton changes, then smaller pose-only updates. Pose
			// callbacks can run much faster than skeletal callbacks, so keeping them separate avoids
			// repeatedly transmitting and converting the complete skeleton.
			mSteamVRHandTrackingPending.exchange(false);
			const bool forceResync = mSteamVRHandTrackingResync.exchange(false);
			const auto now = std::chrono::steady_clock::now();
			const auto hmd = getHMD();
			const auto hmdPose = hmd ? hmd->getForwardedPose() : std::nullopt;
			nextWakeTime = (std::chrono::steady_clock::time_point::max)();
			for (int sideIndex = 0; sideIndex < HandSide::HandSide_MAX; sideIndex++)
			{
				auto selectedSource = mForwardedHandTrackingControllers[sideIndex].load();
				if (selectedSource)
				{
					sources[sideIndex] = selectedSource;
				}

				auto& source = sources[sideIndex];
				if (!source)
				{
					continue;
				}

				auto updates = source->getForwardedHandUpdates(
					forwardingStates[sideIndex],
					Runtime.isSteamVR && selectedSource == source,
					forceResync,
					now);
				nextWakeTime = (std::min)(nextWakeTime, updates.staleDeadline);

				if (updates.baseline)
				{
					if (hmdPose)
					{
						updates.baseline->hasHmdPose = true;
						updates.baseline->hmdPose = *hmdPose;
					}
					mTransport.sendPayload<NativePacketType::SteamVRHandBaseline>(
						*updates.baseline);
				}

				if (updates.pose)
				{
					if (hmdPose)
					{
						updates.pose->hasHmdPose = true;
						updates.pose->hmdPose = *hmdPose;
					}
					mTransport.sendPayload<NativePacketType::SteamVRHandPose>(*updates.pose);
				}

				// Retain a removed source until it emits one inactive baseline. Otherwise the app
				// would keep using the last active sample indefinitely.
				if (!selectedSource && !forwardingStates[sideIndex].active)
				{
					source.reset();
					forwardingStates[sideIndex] = {};
				}
			}
		}
	}

	void HandOfLesser::cleanup()
	{
		mAppLauncher.stop();

		if (Config.steamvr.closeAppOnSteamVRExit && this->mTransport.isConnected())
		{
			this->mTransport.sendPacket<NativePacketType::AppShutdownRequested>();
			DriverLog("Sent app shutdown request");
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		// Signal the receive thread to stop before waiting for it to exit.
		this->mActive.store(false);
		mSteamVRHandTrackingCondition.notify_all();
		if (my_pose_update_thread_.joinable())
		{
			my_pose_update_thread_.join();
		}
		if (mSteamVRHandTrackingThread.joinable())
		{
			mSteamVRHandTrackingThread.join();
		}

		// Our controller devices will have already deactivated. Let's now destroy them.
		this->destroyEmulatedControllers();
	}

	float HandOfLesser::getControllerToHandDistance(HookedController* controller)
	{
		if (controller == nullptr)
		{
			return (std::numeric_limits<float>::max)();
		}

		if (!Tracking.isMultimodalEnabled)
		{
			return (std::numeric_limits<float>::max)();
		}

		// Get controller side
		HandSide side = controller->getSide();
		if (side == HandSide::HandSide_MAX)
		{
			return (std::numeric_limits<float>::max)();
		}

		auto& multimodal = HOL::HandOfLesser::Current->mLastMultimodalPosePayload;

		// You would think upper-body tracking would be used to augment controller tracker
		// when it gives up tracking the controller, but no. Likewise, Controller rotation, which
		// remains available, is not used to augment the body tracking.
		// For this reason the two go out of sync when this happens, but at least they
		// mark the hand as not being tracked anymore.
		bool handValid = (side == HandSide::LeftHand) ? multimodal.leftHandTracked
													  : multimodal.rightHandTracked;
		if (!handValid)
		{
			return (std::numeric_limits<float>::max)();
		}

		// Check if controller pose is valid
		if (!controller->mLastOriginalPoseValid)
		{
			return (std::numeric_limits<float>::max)();
		}

		// Get body tracking hand pose
		HOL::PoseLocation& bodyHandPose
			= (side == HandSide::LeftHand) ? multimodal.leftHandPose : multimodal.rightHandPose;

		// Calculate and return distance
		return (controller->getWorldPosition() - bodyHandPose.position).norm();
	}

	void HandOfLesser::sendStatus()
	{
		DriverStatusPayload payload;
		payload.emulatedControllersActive = false;
		for (auto& controller : mEmulatedControllers)
		{
			if (controller != nullptr)
			{
				payload.emulatedControllersActive = true;
				break;
			}
		}

		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			if (controller->mDeviceClass != vr::TrackedDeviceClass_Controller
				|| controller->isActingAsTracker())
			{
				continue;
			}

			if (controller->mSkeletonTrackingLevel == vr::VRSkeletalTracking_Full)
			{
				payload.hasHandTrackingControllers = true;
			}
			else
			{
				payload.hasNormalControllers = true;
			}
		}

		payload.hookedControllerCount = (int)hookedControllers->size();
		payload.emulatedTrackerCount = (int)mEmulatedTrackers.size();

		this->mTransport.sendPayload<NativePacketType::DriverStatus>(payload);
		DriverLog("Sent driver status: emulated=%d, normal=%d, hand=%d, hooked=%d, trackers=%d",
				  payload.emulatedControllersActive,
				  payload.hasNormalControllers,
				  payload.hasHandTrackingControllers,
				  payload.hookedControllerCount,
				  payload.emulatedTrackerCount);
	}

	void HandOfLesser::sendDeviceState(HookedController* device)
	{
		DeviceStatePayload payload;
		strncpy_s(payload.serial, sizeof(payload.serial), device->serial.c_str(), _TRUNCATE);
		payload.role = device->mDeviceClass;
		payload.trackingLevel = device->mSkeletonTrackingLevel;
		payload.nativePoseIsValid = device->mLastOriginalPoseValid;
		payload.nativeDeviceIsConnected = device->lastOriginalPose.deviceIsConnected;
		payload.nativeTrackingResult = device->lastOriginalPose.result;
		payload.nativePoseAgeMs = device->mLastOriginalPoseAgeMs;

		this->mTransport.sendPayload<NativePacketType::DeviceState>(payload);
	}

	void HandOfLesser::sendAllDeviceStates()
	{
		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			sendDeviceState(controller.get());
		}
	}

	void HandOfLesser::sendDeviceInputInfo(HookedController* device)
	{
		DeviceInputInfoPayload payload;
		if (device == nullptr || device->serial.empty())
		{
			return;
		}

		std::set<std::string> logicalButtons;
		for (const auto& [handle, input] : device->inputHandles)
		{
			if (input.type != ControllerInputType::Boolean
				|| !HOL::SteamVR::isTouchInputPath(input.inputPath))
			{
				continue;
			}

			logicalButtons.insert(HOL::SteamVR::getLogicalButtonPath(input.inputPath));
		}

		if (logicalButtons.empty())
		{
			return;
		}

		strncpy_s(payload.serial, sizeof(payload.serial), device->serial.c_str(), _TRUNCATE);

		DriverLog("Sending input metadata for serial=%s touchButtons=%zu",
				  device->serial.c_str(),
				  logicalButtons.size());

		for (const std::string& buttonPath : logicalButtons)
		{
			if (payload.buttonCount >= DeviceInputInfoPayload::MaxButtonsPerDevice)
			{
				break;
			}

			strncpy_s(payload.buttonPaths[payload.buttonCount],
					  sizeof(payload.buttonPaths[payload.buttonCount]),
					  buttonPath.c_str(),
					  _TRUNCATE);
			DriverLog("  button: %s", buttonPath.c_str());
			payload.buttonCount++;
		}

		if (payload.buttonCount > 0)
		{
			this->mTransport.sendPayload<NativePacketType::DeviceInputInfo>(payload);
		}
	}

	void HandOfLesser::sendAllDeviceInputInfo()
	{
		auto hookedControllers = mHookedControllers.load();
		for (const auto& controller : *hookedControllers)
		{
			sendDeviceInputInfo(controller.get());
		}
	}

	bool HandOfLesser::shouldSuppressTouchInput(
		const HookedController* controller, const std::string& inputPath) const
	{
		if (controller == nullptr || controller->serial.empty())
		{
			return false;
		}

		auto deviceIt = Config.deviceSettings.devices.find(controller->serial);
		if (deviceIt == Config.deviceSettings.devices.end()
			|| deviceIt->second.inputOverrides.empty())
		{
			return false;
		}

		if (!HOL::SteamVR::isTouchInputPath(inputPath))
		{
			return false;
		}

		const std::string buttonPath = HOL::SteamVR::getLogicalButtonPath(inputPath);
		for (const auto& buttonOverride : deviceIt->second.inputOverrides)
		{
			if (buttonOverride.buttonPath == buttonPath)
			{
				return buttonOverride.suppressTouch;
			}
		}

		return false;
	}

	void HandOfLesser::enforceTouchSuppression(HookedController* controller)
	{
		if (controller == nullptr || controller->driverInput == nullptr)
		{
			return;
		}

		for (const auto& [handle, input] : controller->inputHandles)
		{
			if (input.type != ControllerInputType::Boolean)
			{
				continue;
			}

			if (!shouldSuppressTouchInput(controller, input.inputPath))
			{
				continue;
			}

			hooks::UpdateBooleanComponent::FunctionHook.originalFunc(
				controller->driverInput, handle, false, 0.0);
		}
	}
} // namespace HOL
