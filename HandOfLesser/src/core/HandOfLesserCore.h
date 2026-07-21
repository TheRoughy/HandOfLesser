#pragma once

#include <atomic>
#include <memory>
#include "src/openxr/InstanceHolder.h"
#include "src/openxr/HandTracking.h"
#include "src/openxr/XrEventsInterface.h"
#include <HandOfLesserCommon.h>
#include "src/core/ui/user_interface.h"
#include "src/vrchat/vrchat_osc.h"
#include <thread>
#include "src/vrchat/vrchat_input.h"
#include "src/steamvr/steamvr_input.h"
#include "src/steamvr/skeletal_input.h"
#include "src/openxr/body_tracking.h"
#include "src/core/features_manager.h"
#include "src/core/state_global.h"

using namespace HOL;
using namespace HOL::OpenXR;
using namespace HOL::VRChat;

namespace HOL
{
	class HandOfLesserCore // : public XrEventsInterface
	{
	public:
		~HandOfLesserCore();
		HandOfLesserCore();
		void init(int serverPort);
		bool start();
		void requestTerminate(bool restart = false);
		bool shouldTerminate() const;
		bool shouldRestart() const;
		static HandOfLesserCore* Current; // Time to commit sinss

		void syncSettings();
		void syncState();
		void saveSettings();
		void loadSettings();
		void rebuildActions();
		std::shared_ptr<BaseAction> getActionForBindingIndex(size_t bindingIndex) const;
		bool isDriverConnected() const;

		virtual std::vector<const char*> getRequiredExtensions();

		FeaturesManager featuresManager;

	private:
		InstanceHolder mInstanceHolder;
		HandTracking mHandTracking;
		BodyTracking mBodyTracking;
		UserInterface mUserInterface;
		VRChatOSC mVrchatOSC;
		VRChatInput mVrchatInput;
		SteamVR::SkeletalInput mSkeletalInput;
		SteamVR::SteamVRInput mSteamVRInput;
		NamedPipeTransport mDriverTransport;
		UdpTransport mOscTransport;

		std::thread mUserInterfaceThread;
		std::thread mReceiveThread;
		std::atomic<bool> mActive = false;
		std::atomic<bool> mShouldTerminate = false;
		std::atomic<bool> mShouldRestart = false;
		std::atomic<bool> mSettingsSyncRequested = false;
		std::atomic<int64_t> mLastSettingsSyncTimeMS = 0;
		static constexpr int64_t SettingsSyncDebounceMS = 50;

		void userInterfaceLoop();
		void receiveDataThread();

		void mainLoop();
		void doOpenXRStuff(bool skeletalUpdate);
		void sendOscData();
		void sendUpdate(bool skeletalUpdate);
		void sendBodyTrackerData();
		void sendSettingsNow();
		void flushSettings(bool force = false);
		void onDriverConnected();
	};
} // namespace HOL
