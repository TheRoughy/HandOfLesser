#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>
#include <HandOfLesserCommon.h>
#include "src/controller/emulated_controller_driver.h"
#include "src/controller/hooked_controller.h"
#include "src/tracker/emulated_tracker_driver.h"
#include "src/utils/app_launcher.h"

namespace HOL
{

	class HandOfLesser
	{
	public:
		HandOfLesser();
		void init();
		void cleanup();
		void runFrame();
		void addEmulatedControllers();
		void removeEmulatedControllers();
		void destroyEmulatedControllers();
		void addEmulatedTrackers();
		void removeEmulatedTrackers();
		void updateTrackerConnectionStates();
		HookedController* addHookedController(uint32_t id,
											  vr::IVRServerDriverHost* host,
											  vr::ITrackedDeviceServerDriver* driver,
											  vr::PropertyContainerHandle_t propertyContainer);

		void removeDuplicateDevices();

		bool shouldPossessInput(uint32_t deviceId);
		bool shouldPossessInput(HookedController* controller);
		bool shouldPossessPose(HookedController* controller);

		bool shouldEmulateControllers();
		static HandOfLesser* Current; // Time to commit sins
		static HOL::settings::HandOfLesserSettings Config;

		EmulatedControllerDriver* getEmulatedController(HOL::HandSide side);
		bool isEmulatedController(vr::ITrackedDeviceServerDriver* driver);
		bool isEmulatedTracker(vr::ITrackedDeviceServerDriver* driver);
		bool isShadowTracker(vr::ITrackedDeviceServerDriver* driver);
		std::shared_ptr<HookedController> getHookedController(HOL::HandSide side);
		std::vector<std::shared_ptr<HookedController>> getHookedControllers(HOL::HandSide side);
		std::shared_ptr<HookedController> getHookedControllerByDeviceId(uint32_t deviceId);
		std::shared_ptr<HookedController> getHookedControllerBySerial(std::string serial);
		std::shared_ptr<HookedController>
		getHookedControllerByPropertyContainer(vr::PropertyContainerHandle_t container);
		std::shared_ptr<HookedController> getHMD();
		std::shared_ptr<HookedController>
		getHookedControllerByInputHandle(vr::VRInputComponentHandle_t inputHandle);

		// Returns the active controller if we are in a mode that modifies it and it is connected
		// otherwise returns nullptr
		GenericControllerInterface* GetActiveController(
			HOL::HandSide side, std::shared_ptr<HookedController>& hookedControllerOwner);

		void requestEstimateControllerSide();

		float getControllerToHandDistance(HookedController* controller);
		bool isHandTrackingPrimary(HOL::HandSide side);
		bool shouldUseHandTracking(HookedController* controller);
		bool shouldSuppressHookedController(HookedController* controller);

		HOL::MultimodalPosePayload mLastMultimodalPosePayload;
		static HOL::state::TrackingState Tracking;
		static HOL::state::RuntimeState Runtime;

		void sendDeviceState(HookedController* device);
		void sendDeviceInputInfo(HookedController* device);
		void sendAllDeviceStates();
		void sendAllDeviceInputInfo();
		void refreshPreferredHookedControllers();
		void sendStatus();
		bool shouldSuppressTouchInput(
			const HookedController* controller, const std::string& inputPath) const;
		void enforceTouchSuppression(HookedController* controller);

		// Shadow tracker management
		void updateShadowTrackerState(HookedController* controller);
		void updateShadowTrackerStates();
		EmulatedTrackerDriver* getOrCreateShadowTracker(HookedController* controller);

	private:
		using HookedControllerList = std::vector<std::shared_ptr<HookedController>>;

		void refreshPreferredHookedController(HOL::HandSide side);
		void refreshRecoveryHookedController(HOL::HandSide side);
		std::string getPreferredHookedControllerSerial(HOL::HandSide side) const;
		std::shared_ptr<HookedController> getRecoveryHookedController(HOL::HandSide side) const;
		vr::EVRSkeletalTrackingLevel getRequestedSkeletalTrackingLevel() const;
		int getHookedControllerSelectionScore(HookedController* controller) const;
		void persistAutoLaunchSetting();
		void disableAppDrivenState();
		void ReceiveDataThread();
		void estimateControllerSide();

		std::atomic<bool> mActive = false;
		int mControllerSideEstimationAttemptCount = 0;

		bool mEstimateControllerSideWhenPositionValid = false;

		void handleConfigurationChange(HOL::settings::HandOfLesserSettings& newConfig);
		void updateControllerConnectionStates(bool forceUpdate = false);

		std::thread my_pose_update_thread_;
		AppLauncher mAppLauncher;
		HOL::NamedPipeTransport mTransport;

		std::array<EmulatedControllerDriver*, HOL::HandSide_MAX> mEmulatedControllers{};
		std::array<std::array<std::unique_ptr<EmulatedControllerDriver>, HOL::HandSide_MAX>,
				   HOL::EmulatedControllerVariant_MAX>
			mAllEmulatedControllers;
		std::atomic<std::shared_ptr<const HookedControllerList>> mHookedControllers;
		std::unordered_map<HOL::BodyTrackerRole, std::unique_ptr<EmulatedTrackerDriver>>
			mEmulatedTrackers;
		std::unordered_map<std::string, std::unique_ptr<EmulatedTrackerDriver>>
			mShadowTrackers; // Keyed by source device serial
		bool mHasConfiguredShadowTrackers = false;
		std::array<std::atomic<std::shared_ptr<HookedController>>, HOL::HandSide_MAX>
			mPreferredHookedControllers{};
		std::array<std::atomic<std::shared_ptr<HookedController>>, HOL::HandSide_MAX>
			mRecoveryHookedControllers{};
		HOL::HandTransformPayload mLastHandTransforms[HOL::HandSide_MAX]{};
		bool mHasHandTransform[HOL::HandSide_MAX]{false, false};
	};
} // namespace HOL
