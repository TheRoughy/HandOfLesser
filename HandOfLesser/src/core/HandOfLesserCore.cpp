#include "HandOfLesserCore.h"
#include <thread>
#include "src/oculus/oculus_hacks.h"
#include "src/openxr/XrUtils.h"
#include "src/core/app_paths.h"
#include "src/core/settings_global.h"
#include "src/core/state_global.h"
#include "src/core/ui/display_global.h"
#include "src/util/hol_utils.h"
#include "src/vrchat/vrchat_osc.h"
#include "src/windows/high_resolution_timer.h"
#include "src/windows/windows_utils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <nlohmann/json.hpp>
#include <src/json/types.h>

using namespace HOL;
using namespace HOL::OpenXR;

HandOfLesserCore* HandOfLesserCore::Current = nullptr;

HandOfLesserCore::HandOfLesserCore()
{
}

HandOfLesserCore::~HandOfLesserCore()
{
	if (Current == this)
	{
		Current = nullptr;
	}
}

void HandOfLesserCore::init(int serverPort)
{
	this->Current = this;
	this->loadSettings();

	if (!Config.openxr.runtimeOverridePath.empty())
	{
		DWORD attributes = GetFileAttributesA(Config.openxr.runtimeOverridePath.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			std::cout << "Configured OpenXR runtime override is missing, ignoring: "
					  << Config.openxr.runtimeOverridePath << std::endl;
			HOL::OpenXR::setOpenXRRuntimeOverride("");
		}
		else
		{
			HOL::OpenXR::setOpenXRRuntimeOverride(Config.openxr.runtimeOverridePath);
		}
	}
	else
	{
		HOL::OpenXR::setOpenXRRuntimeOverride("");
	}

	std::string runtimePath = HOL::OpenXR::getActiveOpenXRRuntimePath(1);
	std::string runtimeName = HOL::OpenXR::getActiveOpenXRRuntimeName(1);
	std::cout << "Active OpenXR Runtime is: " << runtimePath << std::endl;

	auto& runtimeState = state::Runtime;
	auto& trackingState = state::Tracking;

	std::memset(runtimeState.runtimeName, 0, sizeof(runtimeState.runtimeName));
	std::strncpy(
		runtimeState.runtimeName, runtimeName.c_str(), sizeof(runtimeState.runtimeName) - 1);
	runtimeState.isVDXR = false;
	runtimeState.isOVR = false;
	runtimeState.isSteamVR = false;
	runtimeState.supportsBodyTracking = false;
	runtimeState.supportsHandTrackingAim = false;
	runtimeState.supportsHandTrackingDataSource = false;
	runtimeState.openxrState = HOL::OpenXR::OpenXrState::Uninitialized;
	trackingState.isMultimodalEnabled = false;
	trackingState.isHighFidelityEnabled = false;

	std::cout << "OpenXR SDK version: " << xr::Version::current().major() << "."
			  << xr::Version::current().minor() << "." << xr::Version::current().patch()
			  << std::endl;

	if (runtimeName.find("virtualdesktop") != std::string::npos)
	{
		runtimeState.isVDXR = true;
		std::cout << "Runtime is VDXR, will expect being fed lies" << std::endl;
	}

	if (runtimeName.find("oculus_openxr") != std::string::npos)
	{
		runtimeState.isOVR = true;
		std::cout << "Runtime is Oculus, will support multimodal and stuff" << std::endl;
	}

	if (runtimePath.find("steamxr_") != std::string::npos)
	{
		runtimeState.isSteamVR = true;
		std::cout << "Runtime is SteamVR, unobstructed skeletal input will be forced off. Also "
					 "don't expect body tracking."
				  << std::endl;
	}

	this->mInstanceHolder.init();
	runtimeState.openxrState = this->mInstanceHolder.getState();

	if (this->mInstanceHolder.getState() != OpenXrState::Failed)
	{
		this->mInstanceHolder.beginSession();

		// Run everything else even if we don't have valid OpenXR so we
		// can test UI and stuff
		if (this->mInstanceHolder.getState() == OpenXrState::Running)
		{
			this->mHandTracking.init(this->mInstanceHolder.mInstance,
									 this->mInstanceHolder.mSession);

			this->mBodyTracking.init(this->mInstanceHolder.mInstance,
									 this->mInstanceHolder.mSession);

			// Airlink doesn't support headless and requires hax
			// As of writing, the only other supported runtime is VDXR
			if (this->mInstanceHolder.fullForegroundMode())
			{
				std::cout << "Running in full foreground mode for testing only!" << std::endl;
			}

			if (runtimeState.isOVR)
			{
				// Hacks to make OVR actually work
				HOL::hacks::fixOvrSessionStateRestriction();
				HOL::hacks::fixOvrMultimodalSupportCheck();
			}
		}
	}

	// Initialize transport as client (connects to driver via named pipe)
	if (!this->mDriverTransport.init(PipeRole::Client, R"(\\.\pipe\HandOfLesser)"))
	{
		std::cerr << "Failed to initialize driver transport" << std::endl;
	}
	else
	{
		// Send initialization payload to notify driver we're connected
		AppInitializedPayload initPayload;
		this->mDriverTransport.sendPayload<NativePacketType::AppInitialized>(initPayload);
		std::cout << "Sent AppInitialized payload to driver" << std::endl;
	}

	// Initialize OSC transport (send to port 9000, no listening needed)
	if (!this->mOscTransport.init(9000))
	{
		std::cerr << "Failed to initialize OSC transport" << std::endl;
	}

	this->featuresManager.setInstanceHolder(&this->mInstanceHolder);
	this->featuresManager.setBodyTracking(&this->mBodyTracking);
	this->syncState();
}

bool HandOfLesserCore::start()
{
	HOL::HighResolutionTimer::configureProcessTiming();
	this->mActive.store(true);
	this->mShouldTerminate.store(false);
	this->mShouldRestart.store(false);
	this->mUserInterfaceThread = std::thread(&HandOfLesserCore::userInterfaceLoop, this);
	this->mReceiveThread = std::thread(&HandOfLesserCore::receiveDataThread, this);

	this->mainLoop();
	this->saveSettings();

	return this->shouldRestart();
}

void HOL::HandOfLesserCore::requestTerminate(bool restart)
{
	this->mShouldRestart.store(restart || this->mShouldRestart.load());
	this->mShouldTerminate.store(true);
}

void HOL::HandOfLesserCore::rebuildActions()
{
	this->mHandTracking.rebuildActions();
}

std::shared_ptr<BaseAction> HOL::HandOfLesserCore::getActionForBindingIndex(size_t bindingIndex) const
{
	return this->mHandTracking.getActionForBindingIndex(bindingIndex);
}

bool HOL::HandOfLesserCore::shouldTerminate() const
{
	return this->mShouldTerminate.load();
}

bool HOL::HandOfLesserCore::shouldRestart() const
{
	return this->mShouldRestart.load();
}

std::vector<const char*> HandOfLesserCore::getRequiredExtensions()
{
	return std::vector<const char*>();
}

void HOL::HandOfLesserCore::userInterfaceLoop()
{
	this->mUserInterface.init();

	while (1)
	{
		if (this->mInstanceHolder.getState() == OpenXrState::Running)
		{
			this->mHandTracking.drawHands();
			this->mBodyTracking.drawBody();
		}
		this->mUserInterface.onFrame();

		if (this->mUserInterface.shouldCloseWindow())
		{
			this->requestTerminate();
		}

		if (this->shouldTerminate())
		{
			break;
		}

		// Apparently it doesn't vsync anymore?
		std::this_thread::sleep_for(std::chrono::milliseconds(15));
	}

	this->mUserInterface.terminate();
}

void HOL::HandOfLesserCore::receiveDataThread()
{
	std::cout << "App receive thread started" << std::endl;

	bool wasConnected = false;
	bool printedReconnecting = false;

	while (this->mActive.load())
	{
		// Check if we need to (re)connect
		if (!this->mDriverTransport.isConnected())
		{
			if (wasConnected && Config.steamvr.closeAppOnSteamVRExit)
			{
				std::cout << "Driver disconnected, closing app" << std::endl;
				this->requestTerminate();
				break;
			}

			// Only print once when transitioning to disconnected
			if (wasConnected && !printedReconnecting)
			{
				std::cout << "Driver disconnected, attempting to reconnect..." << std::endl;
				printedReconnecting = true;
			}

			wasConnected = false;

			// Close old pipe if any
			this->mDriverTransport.shutdown();

			// Try to reconnect (silently, no spam)
			if (this->mDriverTransport.reconnect())
			{
				// Wait a bit for connection to establish
				for (int i = 0; i < 10 && !this->mDriverTransport.isConnected(); i++)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}

				if (this->mDriverTransport.isConnected())
				{
					std::cout << "Reconnected to driver!" << std::endl;
					wasConnected = true;
					printedReconnecting = false;
				}
			}

			// If still not connected, wait before retrying
			if (!this->mDriverTransport.isConnected())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				continue;
			}
		}
		else
		{
			wasConnected = true;
			printedReconnecting = false;
		}

		HOL::NativePacketView nativePacket = this->mDriverTransport.receivePacket();
		if (!nativePacket)
		{
			if (wasConnected && !this->mDriverTransport.isConnected()
				&& Config.steamvr.closeAppOnSteamVRExit)
			{
				std::cout << "Driver disconnected, closing app" << std::endl;
				this->requestTerminate();
				break;
			}

			continue; // Timeout, retry
		}

		switch (nativePacket.packetType)
		{
			case NativePacketType::DriverInitialized: {
				DriverInitializedPayload initPayload;
				if (!nativePacket.copyPayload(initPayload))
				{
					break;
				}
				std::cout << "Driver initialized: version " << initPayload.driverVersion
						  << std::endl;

				// Send runtime state first so the driver can apply runtime-specific config
				// overrides while parsing the settings payload.
				syncState();
				sendSettingsNow();
				onDriverConnected();
				break;
			}

			case NativePacketType::DriverStatus: {
				DriverStatusPayload statusPayload;
				if (!nativePacket.copyPayload(statusPayload))
				{
					break;
				}
				display::DriverStatus.emulatedControllersActive
					= statusPayload.emulatedControllersActive;
				display::DriverStatus.hasNormalControllers = statusPayload.hasNormalControllers;
				display::DriverStatus.hasHandTrackingControllers
					= statusPayload.hasHandTrackingControllers;
				display::DriverStatus.hookedControllerCount = statusPayload.hookedControllerCount;
				display::DriverStatus.emulatedTrackerCount = statusPayload.emulatedTrackerCount;
				std::cout << "Driver status: emulated controllers="
						  << statusPayload.emulatedControllersActive
						  << ", normal=" << statusPayload.hasNormalControllers
						  << ", hand-tracking=" << statusPayload.hasHandTrackingControllers
						  << ", hooked=" << statusPayload.hookedControllerCount
						  << ", trackers=" << statusPayload.emulatedTrackerCount << std::endl;
				break;
			}

			case NativePacketType::DeviceState: {
				DeviceStatePayload devicePayload;
				if (!nativePacket.copyPayload(devicePayload))
				{
					break;
				}

				std::string serial = devicePayload.serial;

				// Add or update device in map
				auto it = Config.deviceSettings.devices.find(serial);
				if (it == Config.deviceSettings.devices.end())
				{
					// New device
					settings::DeviceConfig config;
					config.serial = serial;
					config.role = devicePayload.role;
					config.trackingLevel = devicePayload.trackingLevel;
					config.nativePoseIsValid = devicePayload.nativePoseIsValid;
					config.nativeDeviceIsConnected = devicePayload.nativeDeviceIsConnected;
					config.nativeTrackingResult = devicePayload.nativeTrackingResult;
					config.nativePoseAgeMs = devicePayload.nativePoseAgeMs;
					config.activatedThisSession = true;
					Config.deviceSettings.devices[serial] = config;
					std::cout << "Device registered: " << serial << std::endl;
				}
				else
				{
					// Update existing device
					it->second.role = devicePayload.role;
					it->second.trackingLevel = devicePayload.trackingLevel;
					it->second.nativePoseIsValid = devicePayload.nativePoseIsValid;
					it->second.nativeDeviceIsConnected = devicePayload.nativeDeviceIsConnected;
					it->second.nativeTrackingResult = devicePayload.nativeTrackingResult;
					it->second.nativePoseAgeMs = devicePayload.nativePoseAgeMs;
					it->second.activatedThisSession = true;
				}

				break;
			}

			case NativePacketType::DeviceInputInfo: {
				DeviceInputInfoPayload inputPayload;
				if (!nativePacket.copyPayload(inputPayload))
				{
					break;
				}

				std::string serial = inputPayload.serial;
				auto& deviceConfig = Config.deviceSettings.devices[serial];
				deviceConfig.serial = serial;
				deviceConfig.activatedThisSession = true;
				deviceConfig.touchButtons.clear();

				const uint32_t buttonCount
					= std::min(inputPayload.buttonCount, DeviceInputInfoPayload::MaxButtonsPerDevice);
				deviceConfig.touchButtons.reserve(buttonCount);
				for (uint32_t buttonIndex = 0; buttonIndex < buttonCount; ++buttonIndex)
				{
					std::string buttonPath = inputPayload.buttonPaths[buttonIndex];
					if (buttonPath.empty())
					{
						continue;
					}

					deviceConfig.touchButtons.push_back(buttonPath);
				}

				break;
			}

			case NativePacketType::SteamVRHandBaseline: {
				SteamVRHandBaselinePayload payload;
				if (nativePacket.copyPayload(payload))
				{
					mHandTracking.updateSteamVRHandBaseline(payload);
				}
				break;
			}

			case NativePacketType::SteamVRHandPose: {
				SteamVRHandPosePayload payload;
				if (nativePacket.copyPayload(payload))
				{
					mHandTracking.updateSteamVRHandPose(payload);
				}
				break;
			}

			case NativePacketType::AppShutdownRequested: {
				if (nativePacket.payloadSize != 0)
				{
					break;
				}

				std::cout << "Driver requested app shutdown" << std::endl;
				this->requestTerminate();
				break;
			}

			default:
				std::cerr << "Unknown packet type from driver: " << (int)nativePacket.packetType
						  << std::endl;
		}
	}

	std::cout << "App receive thread stopped" << std::endl;
}

void HandOfLesserCore::mainLoop()
{
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
	{
		std::cerr << "Failed to set tracking thread priority: "
				  << FormatWindowsError(GetLastError()) << std::endl;
	}

	HOL::HighResolutionTimer updateTimer;

	while (1)
	{
		this->mUserInterface.Current->getVisualizer()->clearDrawQueue();

		if (this->shouldTerminate())
		{
			break;
		}

		if (this->mInstanceHolder.getState() == OpenXrState::Running)
		{
			doOpenXRStuff();
			sendOscData();
		}
		else
		{
			if (Config.vrchat.sendDebugOsc)
			{
				sendOscData();
			}
		}

		// draw queue swapping because UI and main loop are not in sync
		this->mUserInterface.Current->getVisualizer()->swapOuterDrawQueue();

		this->flushSettings();

		updateTimer.waitForInterval(
			std::chrono::milliseconds(std::max(Config.general.updateIntervalMS, 1)));
	}

	std::cout << "Exiting loop" << std::endl;

	// Shut it down!
	this->mInstanceHolder.endSession();

	// Signal threads to stop
	this->mActive.store(false);

	// Wait for threads to finish
	if (this->mUserInterfaceThread.joinable())
	{
		this->mUserInterfaceThread.join();
	}

	if (this->mReceiveThread.joinable())
	{
		this->mReceiveThread.join();
	}
}

void HandOfLesserCore::doOpenXRStuff()
{
	XrTime time = this->mInstanceHolder.getTime();
	time += 1000000LL * (XrTime)Config.general.motionPredictionMS;

	this->mInstanceHolder.pollEvent();

	if (this->mInstanceHolder.fullForegroundMode())
	{
		this->mInstanceHolder.foregroundRender();
	}

	HOL::PoseLocation hmdPose;
	const bool hmdPoseValid
		= this->mInstanceHolder.getHmdPose(this->mInstanceHolder.mStageSpace.get(), time, hmdPose);
	const std::array<const HOL::HandPose*, HOL::HandSide_MAX> lastHandPoses = {
		&this->mHandTracking.getHandPose(HOL::HandSide::LeftHand),
		&this->mHandTracking.getHandPose(HOL::HandSide::RightHand),
	};
	this->mBodyTracking.updateBody(
		this->mInstanceHolder.mStageSpace,
		time,
		hmdPoseValid ? &hmdPose : nullptr,
		lastHandPoses);
	this->mHandTracking.updateHands(
		this->mInstanceHolder.mStageSpace,
		time,
		this->mBodyTracking.getBodyTracker(),
		hmdPoseValid ? &hmdPose : nullptr);
	this->mHandTracking.updateInputs();

	// Periodic check for tracking features
	this->featuresManager.performPeriodicCheck();

	this->sendUpdate();

	return;
}

void HOL::HandOfLesserCore::sendOscData()
{
	if (!Config.vrchat.sendOsc)
	{
		return;
	}

	if (Config.vrchat.sendDebugOsc)
	{
		this->mVrchatOSC.generateOscTestOutput();
	}
	else
	{
		// Generates the full data which is shared by alternating.
		// Packed requires full to be generated first, and is generated on-demand below.
		this->mVrchatOSC.generateOscOutputFull(
			this->mHandTracking.getHandPose(HandSide::LeftHand),
			this->mHandTracking.getHandPose(HandSide::RightHand));
	}

	size_t size = 0;

	// Always send full, expect when testing remote stuff locally because it will break things
	if (Config.vrchat.sendFull)
	{
		size = this->mVrchatOSC.generateOscBundleFull();
		this->mOscTransport.send(this->mVrchatOSC.getPacketBuffer(), size);
	}

	if (Config.vrchat.sendAlternating)
	{
		size = this->mVrchatOSC.generateOscBundleAlternating();
		this->mOscTransport.send(this->mVrchatOSC.getPacketBuffer(), size);
	}

	if (Config.vrchat.sendPacked)
	{
		// Keep track of values between updates so we don't miss quick movement
		this->mVrchatOSC.handleInbetweenPacked();

		// Wait for 100ms to have passed since last
		if (this->mVrchatOSC.shouldSendPacked())
		{
			this->mVrchatOSC.generateOscOutputPacked();
			size = this->mVrchatOSC.generateOscBundlePacked();
			this->mOscTransport.send(this->mVrchatOSC.getPacketBuffer(), size);
		}
	}

	// VRChat input goes here for now
	// Finalizing also resets the input OSC packet, which will otherwise overflow.
	// Better to control here than having every input check the setting.
	auto [packetPointer, packetSize] = this->mVrchatInput.finalizeInputBundle();
	if (Config.input.sendOscInput)
	{
		this->mOscTransport.send(packetPointer, packetSize);
	}
}

void HandOfLesserCore::sendUpdate()
{

	if (Config.handPose.controllerMode != ControllerMode::NoControllerMode)
	{
		for (int i = 0; i < HandSide_MAX; i++)
		{
			HandPose hand = this->mHandTracking.getHandPose((HandSide)i);

			// No point in sending any new data if the data is the same as last time.
			if (!hand.poseStale)
			{
				HOL::HandTransformPayload transformPayload
					= this->mHandTracking.getTransformPayload((HandSide)i);
				this->mDriverTransport.sendPayload<NativePacketType::HandTransform>(
					transformPayload);
			}
		}
	}

	if (Config.steamvr.sendSteamVRInput)
	{
		// SteamVR inputs are submitted to a global
		for (auto& payload : SteamVR::SteamVRInput::Current->floatInputs)
		{
			this->mDriverTransport.sendPayload<NativePacketType::FloatInput>(payload);
		}
		for (auto& payload : SteamVR::SteamVRInput::Current->boolInputs)
		{
			this->mDriverTransport.sendPayload<NativePacketType::BoolInput>(payload);
		}
	}

	for (int i = 0; i < HandSide_MAX; i++)
	{
		OpenXRHand* hand = this->mHandTracking.getHand((HandSide)i);
		if (hand->handPose.poseValid) // Only update if valid
		{
			SkeletalPayload& payload = this->mSkeletalInput.getSkeletalPayload(hand, (HandSide)i);
			this->mDriverTransport.sendPayload<NativePacketType::SkeletalInput>(payload);
		}
	}

	// Send body tracking hand positions for controller detection in driver
	if (state::Tracking.isMultimodalEnabled)
	{
		MultimodalPosePayload bodyPosePayload = this->mBodyTracking.getMultimodalPosePayload();
		this->mDriverTransport.sendPayload<NativePacketType::MultimodalPose>(bodyPosePayload);
	}

	// Send body tracker poses
	if (Config.bodyTrackers.enableBodyTrackers)
	{
		sendBodyTrackerData();
	}

	SteamVR::SteamVRInput::Current->clear();
}

void HOL::HandOfLesserCore::sendBodyTrackerData()
{
	// Get body tracker payloads from BodyTracking (includes position calculation and visualization)
	auto payloads = mBodyTracking.getBodyTrackerPayloads();

	// Send each payload
	for (const auto& payload : payloads)
	{
		this->mDriverTransport.sendPayload<NativePacketType::BodyTrackerPose>(payload);
	}
}

void HOL::HandOfLesserCore::syncSettings()
{
	this->mSettingsSyncRequested.store(true);
}

void HOL::HandOfLesserCore::sendSettingsNow()
{
	nlohmann::json j = HOL::Config;
	std::string jsonStr = j.dump();

	this->mDriverTransport.sendPayloadBytes(
		NativePacketType::Settings, jsonStr.data(), jsonStr.size());

	this->mLastSettingsSyncTimeMS.store(steadyNowMS());
	this->mSettingsSyncRequested.store(false);
}

void HOL::HandOfLesserCore::flushSettings(bool force)
{
	if (!force && !this->mSettingsSyncRequested.load())
	{
		return;
	}

	const int64_t now = steadyNowMS();
	if (!force
		&& now - this->mLastSettingsSyncTimeMS.load() < this->SettingsSyncDebounceMS)
	{
		return;
	}

	this->sendSettingsNow();
}

void HOL::HandOfLesserCore::syncState()
{
	state::Runtime.openxrState = this->mInstanceHolder.getState();

	HOL::StatePayload payload;
	payload.tracking = state::Tracking;
	payload.runtime = state::Runtime;
	this->mDriverTransport.sendPayload<NativePacketType::State>(payload);
}

void HOL::HandOfLesserCore::saveSettings()
{
	std::ofstream file(HOL::Paths::getSettingsFilePath());
	nlohmann::json j = Config;
	file << j.dump(4); // indented
}

void HOL::HandOfLesserCore::loadSettings()
{
	std::filesystem::path settingsFilePath = HOL::Paths::getSettingsFilePath();
	std::ifstream file(settingsFilePath);
	if (file.is_open())
	{
		try
		{
			nlohmann::json j;
			file >> j;
			Config = j.get<HOL::settings::HandOfLesserSettings>();

			// Reset runtime-only flag for all devices after loading from JSON
			for (auto& [serial, device] : Config.deviceSettings.devices)
			{
				device.activatedThisSession = false;
			}

			// This is a runtime-only diagnostics toggle. Always start with it disabled even if
			// a previous session saved it as true.
			Config.steamvr.showDevicePoseDiagnostics = false;
		}
		catch (const std::exception& ex)
		{
			std::cerr << "Failed to parse settings.json: " << ex.what() << std::endl;
		}
		catch (...)
		{
			std::cerr << "Failed to parse settings.json due to an unknown error." << std::endl;
		}
		file.close();
	}
	else
	{
		std::cout << "No settings.json found at " << settingsFilePath.string()
				  << ", using defaults." << std::endl;
	}
}

bool HOL::HandOfLesserCore::isDriverConnected() const
{
	return this->mDriverTransport.isConnected();
}

void HOL::HandOfLesserCore::onDriverConnected()
{
	featuresManager.applyTrackingFeatures(Config.trackingFeatures.enableUpperBodyTracking,
										  Config.trackingFeatures.enableSimultaneousTracking);
}
