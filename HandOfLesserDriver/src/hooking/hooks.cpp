#include "driverlog.h"
#include "hooks.h"
#include "src/controller/controller_common.h"
#include "src/tracker/emulated_tracker_driver.h"
#include <chrono>

namespace HOL::hooks
{
	namespace TrackedDeviceActivate
	{
		std::unordered_map<void*, std::unique_ptr<Hook<TrackedDeviceActivate::Signature>>>
			FunctionHooksByTarget;

		void EnsureHookForDriver(vr::ITrackedDeviceServerDriver* driver)
		{
			if (driver == nullptr)
			{
				return;
			}

			// A C++ interface pointer points at an object whose first field is a pointer to that
			// object's vtable. We cast:
			// - driver        -> object pointer
			// - (void***)     -> "pointer to object whose first field is a void** vtable"
			// - *             -> the actual vtable pointer
			void** vtable = *((void***)driver);

			// ITrackedDeviceServerDriver::Activate is the first virtual in this interface, so the
			// implementation function we want to hook is vtable slot 0 for each concrete driver type.
			void* targetFunc = vtable[0];

			if (FunctionHooksByTarget.find(targetFunc) != FunctionHooksByTarget.end())
			{
				return;
			}

			auto hook = std::make_unique<Hook<TrackedDeviceActivate::Signature>>(
				"ITrackedDeviceServerDriver::Activate@"
				+ std::to_string((uintptr_t)targetFunc));

			if (!hook->CreateHookInObjectVTable(driver, 0, &TrackedDeviceActivate::Detour))
			{
				return;
			}

			IHook::Register(hook.get());
			FunctionHooksByTarget[targetFunc] = std::move(hook);
		}

		vr::EVRInitError Detour(vr::ITrackedDeviceServerDriver* _this, uint32_t unWhichDevice)
		{
			// Look up the hook/original function by the concrete Activate implementation backing this
			// particular device driver's vtable. Different drivers can implement the same interface
			// with different vtables, which is why a single global Activate hook was insufficient.
			void** vtable = *((void***)_this);
			void* targetFunc = vtable[0];
			auto hook = FunctionHooksByTarget.find(targetFunc);
			if (hook == FunctionHooksByTarget.end() || hook->second->originalFunc == nullptr)
			{
				DriverLog("Missing activate original function for target %p", targetFunc);
				return vr::VRInitError_Driver_Failed;
			}

			DriverLog("TrackedDeviceActivate!");
			DriverLog("Device ID: %lld", (long long)unWhichDevice);

			if (HandOfLesser::Current->isEmulatedController(_this)
				|| HandOfLesser::Current->isEmulatedTracker(_this)
				|| HandOfLesser::Current->isShadowTracker(_this))
			{
				// Do not hook our own controllers or trackers.
				DriverLog("Activate hooked on emulated device, skipping.");
				return hook->second->originalFunc(_this, unWhichDevice);
			}

			// Get the role
			auto props = vr::VRProperties();
			vr::PropertyContainerHandle_t container
				= props->TrackedDeviceToPropertyContainer(unWhichDevice);

			// Add controller, get a pointer so we can set side later.
			HookedController* controller = HOL::HandOfLesser::Current->addHookedController(
				unWhichDevice, HOL::hooks::mLastDeviceDriverHost, _this, container);

			// Let original function run. This will add all the inputs, which is why
			// we need to add the controller first.
			auto ret = hook->second->originalFunc(_this, unWhichDevice);

			// These devices should not be populated until activate has been called,
			// but for some devices they are. Weird.
			std::string serial = props->GetStringProperty(container, vr::Prop_SerialNumber_String);
			vr::ETrackedDeviceClass deviceClass = (vr::ETrackedDeviceClass)props->GetInt32Property(
				container, vr::Prop_DeviceClass_Int32);
			vr::ETrackedControllerRole role = (vr::ETrackedControllerRole)props->GetInt32Property(
				container, vr::Prop_ControllerRoleHint_Int32);

			DriverLog("Device serial: %s", serial.c_str());
			DriverLog("Device class: %d", deviceClass);
			DriverLog("Device role: %d", role);

			controller->lateInit(serial, deviceClass, role);

			// This is a good a time as any to communicate the device state
			controller->sendDeviceState();
			HOL::HandOfLesser::Current->sendDeviceInputInfo(controller);
			HOL::HandOfLesser::Current->enforceTouchSuppression(controller);

			if (deviceClass == vr::ETrackedDeviceClass::TrackedDeviceClass_Controller)
			{
				// TODO: vive wands will probably swap sides after activating
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

				if (side == HandSide_MAX)
				{
					// Not a controller. I think ViveWands are assigned a side,
					// it just may swap them dynamically.
					// We should probably remove these.
					DriverLog("Got controller but no side assigned");
				}
				else
				{
					DriverLog("Got controller, side: %s",
							  side == HandSide::LeftHand ? "Left" : "Right");
					controller->setSide(side);
				}
			}
			else
			{
				DriverLog("Some other device activated, class: %d", deviceClass);
			}

			HandOfLesser::Current->removeDuplicateDevices();

			return ret;
		}
	} // namespace TrackedDeviceActivate

	namespace PollNextEvent
	{
		Hook<PollNextEvent::Signature> FunctionHook("ITrackedDeviceServerDriver::PollNextEvent");

		static bool
		Detour(vr::IVRServerDriverHost* _this, vr::VREvent_t* pEvent, uint32_t uncbVREvent)
		{
			// DriverLog("PollNextEvent!");
			auto ret = PollNextEvent::FunctionHook.originalFunc(_this, pEvent, uncbVREvent);

			if (ret)
			{
				if (pEvent->eventType == vr::EVREventType::VREvent_TrackedDeviceRoleChanged)
				{
					DriverLog(
						"Received TrackedDeviceRoleChanged event, updating unhanded controllers");
					HandOfLesser::Current->requestEstimateControllerSide();
				}
			}

			return ret;
		}
	} // namespace PollNextEvent

	namespace TrackedDeviceAdded006
	{
		Hook<TrackedDeviceAdded006::Signature>
			FunctionHook("IVRServerDriverHost006::TrackedDeviceAdded");

		static bool Detour(vr::IVRServerDriverHost* _this,
						   const char* pchDeviceSerialNumber,
						   vr::ETrackedDeviceClass eDeviceClass,
						   vr::ITrackedDeviceServerDriver* pDriver)
		{
			DriverLog("TrackedDeviceAdded006! serial=%s",
					  pchDeviceSerialNumber != nullptr ? pchDeviceSerialNumber : "<null>");

		if (HandOfLesser::Current->isEmulatedController(pDriver)
				|| HandOfLesser::Current->isShadowTracker(pDriver))
			{
				// Do not hook our own controllers or shadow trackers
				DriverLog("Emulated device, skipping TrackedDeviceAdded hook.");
			}
			else
			{
				TrackedDeviceActivate::EnsureHookForDriver(pDriver);

				if (eDeviceClass == vr::ETrackedDeviceClass::TrackedDeviceClass_Controller)
				{
					DriverLog("Controller added! serial=%s",
							  pchDeviceSerialNumber != nullptr ? pchDeviceSerialNumber : "<null>");

					// TODO: We only have access to the driverHost here, but don't know the ID
					// or the role of the controller yet. Activate() should be called
					// immediately afterwards, so the only concern is when devices are
					// deactivated and re-activated again. Think about that later.
					HOL::hooks::mLastDeviceDriverHost = _this;
				}
				else
				{
					DriverLog("Some other device added, class: %d, serial=%s",
							  eDeviceClass,
							  pchDeviceSerialNumber != nullptr ? pchDeviceSerialNumber : "<null>");
				}
			}

			return TrackedDeviceAdded006::FunctionHook.originalFunc(
				_this, pchDeviceSerialNumber, eDeviceClass, pDriver);
		};
	} // namespace TrackedDeviceAdded006

	namespace TrackedDevicePoseUpdated
	{
		Hook<TrackedDevicePoseUpdated::Signature>
			FunctionHook("IVRServerDriverHost006::TrackedDevicePoseUpdated");

		static void Detour(vr::IVRServerDriverHost* _this,
						   uint32_t unWhichDevice,
						   const vr::DriverPose_t& newPose,
						   uint32_t unPoseStructSize)
		{
			auto& config = HOL::HandOfLesser::Current->Config;

			auto controller
				= HOL::HandOfLesser::Current->getHookedControllerByDeviceId(unWhichDevice);

			if (controller != nullptr)
			{
				bool newPoseValid = newPose.deviceIsConnected && newPose.poseIsValid
									&& newPose.result == vr::TrackingResult_Running_OK;
				if (config.steamvr.showDevicePoseDiagnostics)
				{
					uint64_t nowMs
						= (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
							  std::chrono::system_clock::now().time_since_epoch())
							  .count();
					controller->mLastOriginalPoseAgeMs
						= nowMs - controller->mLastOriginalPoseSubmitTimeMs;
					controller->mLastOriginalPoseSubmitTimeMs = nowMs;
				}

				// This submission is current before validity changes refresh cached controller
				// selections, so the selector must not see the previous frame age.
				controller->framesSinceLastPoseUpdate = 0;
				controller->setLastOriginalPoseState(newPoseValid);
				const bool forwardedHandTrackingController
					= HOL::HandOfLesser::Current->isForwardedHandTrackingController(
						controller.get());
				const bool forwardedTrackingReference
					= controller->mDeviceClass == vr::TrackedDeviceClass_HMD;
				if (HOL::HandOfLesser::Runtime.isSteamVR
					&& (forwardedHandTrackingController || forwardedTrackingReference))
				{
					// Keep the HMD alongside the hand source so the app can align SteamVR's raw
					// driver space to its OpenXR stage space. Pipe I/O remains deferred.
					if (controller->cacheForwardedPose(newPose, newPoseValid)
						&& forwardedHandTrackingController)
					{
						HOL::HandOfLesser::Current->notifySteamVRHandTracking();
					}
				}

				if (newPoseValid)
				{
					controller->lastOriginalPose = newPose;
				}

				if (config.steamvr.showDevicePoseDiagnostics)
				{
					controller->sendDeviceState();
				}

				if (!controller->isActingAsTracker()
					&& config.handPose.controllerMode == ControllerMode::HookedControllerMode)
				{
					controller->setSuppressed(
						HOL::HandOfLesser::Current->shouldSuppressHookedController(controller.get()));
				}

				if (controller->isSuppressed())
				{
					// If acting as tracker, forward pose to shadow tracker
					if (controller->isActingAsTracker())
					{
						if (auto* shadow = controller->getShadowTracker())
						{
							// Actual submission happens in HandOfLesser::runFrame().
							// VD forces itself to be loaded before all other drivers,
							// and if we try to update the pose from here we end up in 
							// a recursive deadlock.
							shadow->UpdatePose(newPose);
						}
					}

					if (config.steamvr.disableOtherControllersWhileHandTracking)
					{
						// We've sent a disconnect message,
						// prevent any future pose submits.
						return;
					}
				}

				auto controllerMode = config.handPose.controllerMode;
				if (controllerMode == ControllerMode::HookedControllerMode)
				{
					bool shouldSubmitFallbackPose
						= HOL::HandOfLesser::Current->shouldPossessPose(controller.get());

					// Just do nothing if we are possessing controllers and need to replace the
					// native pose outright.
					if (shouldSubmitFallbackPose)
					{
						// Prevent original function from being called.
						return;
					}

					if (HOL::HandOfLesser::Current->shouldPossessInput(controller.get())
						&& !shouldSubmitFallbackPose && newPoseValid)
					{
						// In fallback-only mode we keep the native pose while it is healthy and
						// only apply our configured calibration offset until we need to take over.
						HOL::ControllerCommon::offsetPose((vr::DriverPose_t&)newPose,
														  controller->getSide(),
														  config.handPose.positionOffset,
														  config.handPose.orientationOffset);
					}
				}
			}

			// Make sure to return early if we don't want to call the original function
			return TrackedDevicePoseUpdated::FunctionHook.originalFunc(
				_this, unWhichDevice, newPose, unPoseStructSize);
		};
	} // namespace TrackedDevicePoseUpdated

	namespace CreateBooleanComponent
	{
		Hook<CreateBooleanComponent::Signature>
			FunctionHook("IVRDriverInput003::CreateBooleanComponent");

		static vr::EVRInputError Detour(vr::IVRDriverInput* _this,
										vr::PropertyContainerHandle_t ulContainer,
										const char* pchName,
										vr::VRInputComponentHandle_t* pHandle)
		{
			// pHandle is populated by this function, so let it run first.
			auto ret = CreateBooleanComponent::FunctionHook.originalFunc(
				_this, ulContainer, pchName, pHandle);

			auto props = vr::VRProperties();

			// We hook this to get a reference to the IVRDriverInput for each controller.
			// We can identify the controller by the PropertyContainer, because it is unique
			// to each controller.
			auto controller
				= HOL::HandOfLesser::Current->getHookedControllerByPropertyContainer(ulContainer);
			if (controller != nullptr)
			{
				controller->driverInput = _this;

				// Also store the path and handle of each input
				ControllerInputHandle input = {
					.inputPath = pchName, .type = ControllerInputType::Boolean, .handle = *pHandle};
				uint64_t key = *pHandle; // Something is fucked
				controller->inputHandles[key] = input;
				controller->inputHandlesByName[pchName] = *pHandle;
			}

			return ret;
		};
	} // namespace CreateBooleanComponent

	namespace CreateScalarComponent
	{
		Hook<CreateScalarComponent::Signature>
			FunctionHook("IVRDriverInput003::CreateScalarComponent");

		static vr::EVRInputError Detour(vr::IVRDriverInput* _this,
										vr::PropertyContainerHandle_t ulContainer,
										const char* pchName,
										vr::VRInputComponentHandle_t* pHandle,
										vr::EVRScalarType eType,
										vr::EVRScalarUnits eUnits)
		{
			// pHandle is populated by this function, so let it run first.
			auto ret = CreateScalarComponent::FunctionHook.originalFunc(
				_this, ulContainer, pchName, pHandle, eType, eUnits);

			// Get the controller by serial
			auto props = vr::VRProperties();
			std::string serial
				= props->GetStringProperty(ulContainer, vr::Prop_SerialNumber_String);

			// We hook this to get a reference to the IVRDriverInput for each controller.
			// We can identify the controller by the PropertyContainer, because it is unique
			// to each controller.
			auto controller
				= HOL::HandOfLesser::Current->getHookedControllerByPropertyContainer(ulContainer);
			if (controller != nullptr)
			{
				controller->driverInput = _this;

				// Also store the path and handle of each input
				ControllerInputHandle input = {
					.inputPath = pchName, .type = ControllerInputType::Scalar, .handle = *pHandle};
				uint64_t key = *pHandle; // Something is fucked
				controller->inputHandles[key] = input;
				controller->inputHandlesByName[pchName] = *pHandle;
			}

			return ret;
		};
	} // namespace CreateScalarComponent

	namespace CreateSkeletonComponent
	{
		Hook<CreateSkeletonComponent::Signature>
			FunctionHook("IVRDriverInput003::CreateSkeletonComponent");

		static vr::EVRInputError Detour(vr::IVRDriverInput* _this,
										vr::PropertyContainerHandle_t ulContainer,
										const char* pchName,
										const char* pchSkeletonPath,
										const char* pchBasePosePath,
										vr::EVRSkeletalTrackingLevel eSkeletalTrackingLevel,
										const vr::VRBoneTransform_t* pGripLimitTransforms,
										uint32_t unGripLimitTransformCount,
										vr::VRInputComponentHandle_t* pHandle)
		{
			auto ret = CreateSkeletonComponent::FunctionHook.originalFunc(_this,
																		  ulContainer,
																		  pchName,
																		  pchSkeletonPath,
																		  pchBasePosePath,
																		  eSkeletalTrackingLevel,
																		  pGripLimitTransforms,
																		  unGripLimitTransformCount,
																		  pHandle);

			auto controller
				= HOL::HandOfLesser::Current->getHookedControllerByPropertyContainer(ulContainer);
			if (controller != nullptr)
			{
				controller->driverInput = _this;

				controller->registerSkeletonInput(
					*pHandle, eSkeletalTrackingLevel, pchName, pchBasePosePath);

				ControllerInputHandle input = {.inputPath = pchName,
											   .type = ControllerInputType::Skeleton,
											   .handle = *pHandle};
				controller->inputHandles[*pHandle] = input;
				controller->inputHandlesByName[pchName] = *pHandle;
			}

			return ret;
		};
	} // namespace CreateSkeletonComponent

	namespace UpdateBooleanComponent
	{
		Hook<UpdateBooleanComponent::Signature>
			FunctionHook("IVRDriverInput003::UpdateBooleanComponent");

		static vr::EVRInputError Detour(vr::IVRDriverInput* _this,
										vr::VRInputComponentHandle_t ulComponent,
										bool bNewValue,
										double fTimeOffset)
		{
			auto& config = HOL::HandOfLesser::Current->Config;
			auto controller
				= HandOfLesser::Current->getHookedControllerByInputHandle(ulComponent);

			// TODO we shouldn't do anything here if we're not... doing stuff.
			//  Kinda wasteful.

			if (controller != nullptr)
			{
				auto& inputHandle = controller->inputHandles[ulComponent];
				if (HOL::HandOfLesser::Current->shouldSuppressTouchInput(
						controller.get(), inputHandle.inputPath))
				{
					return UpdateBooleanComponent::FunctionHook.originalFunc(
						_this, ulComponent, false, fTimeOffset);
				}


				/*
				DriverLog("Controller: %s, Button: %s, value: %s",
				controller->serial.c_str(),
				inputHandle.inputPath.c_str(),
				bNewValue ? "True" : "False");
				*/

				// Block input when acting as tracker
				if (controller->isActingAsTracker())
				{
					if (config.steamvr.blockControllerInputWhileHandTracking)
					{
						return vr::EVRInputError::VRInputError_None;
					}
				}

				if (HOL::HandOfLesser::Current->shouldSuppressHookedController(controller.get()))
				{
					if (config.steamvr.blockControllerInputWhileHandTracking)
					{
						return vr::EVRInputError::VRInputError_None;
					}
				}

				if (HOL::HandOfLesser::Current->shouldPossessInput(controller.get()))
				{
					if (config.steamvr.blockControllerInputWhileHandTracking)
					{
						// DriverLog("Blocked!");
						return vr::EVRInputError::VRInputError_None;
					}
				}

			}
			else
			{
				// DriverLog("Button on unknown controller!");
			}

			// Make sure to return early if we don't want to call the original function
			return UpdateBooleanComponent::FunctionHook.originalFunc(
				_this, ulComponent, bNewValue, fTimeOffset);
		};
	} // namespace UpdateBooleanComponent

	namespace UpdateScalarComponent
	{
		Hook<UpdateScalarComponent::Signature>
			FunctionHook("IVRDriverInput003::UpdateScalarComponent");

		static vr::EVRInputError Detour(vr::IVRDriverInput* _this,
										vr::VRInputComponentHandle_t ulComponent,
										float fNewValue,
										double fTimeOffset)
		{
			auto& config = HOL::HandOfLesser::Current->Config;
			auto controller
				= HandOfLesser::Current->getHookedControllerByInputHandle(ulComponent);

			// TODO we shouldn't do anything here if we're not... doing stuff.
			//  Kinda wasteful.

			if (controller != nullptr)
			{
				/*
				auto& inputHandle = controller->inputHandles[ulComponent];

				DriverLog("Controller: %s, Button: %s, value: %.3f",
							controller->serial.c_str(),
							inputHandle.inputPath.c_str(),
							bNewValue);
							*/
				// Block input when acting as tracker
				if (controller->isActingAsTracker())
				{
					if (config.steamvr.blockControllerInputWhileHandTracking)
					{
						return vr::EVRInputError::VRInputError_None;
					}
				}

				if (HOL::HandOfLesser::Current->shouldSuppressHookedController(controller.get()))
				{
					if (config.steamvr.blockControllerInputWhileHandTracking)
					{
						return vr::EVRInputError::VRInputError_None;
					}
				}

				if (HOL::HandOfLesser::Current->shouldPossessInput(controller.get()))
				{
					if (config.steamvr.blockControllerInputWhileHandTracking)
					{
						// DriverLog("Blocked!");
						return vr::EVRInputError::VRInputError_None;
					}
				}
			}
			else
			{
				// DriverLog("Scalar on unknown controller!");
			}

			// Make sure to return early if we don't want to call the original function
			return UpdateScalarComponent::FunctionHook.originalFunc(
				_this, ulComponent, fNewValue, fTimeOffset);
		};
	} // namespace UpdateScalarComponent

	namespace UpdateSkeletonComponent
	{
		Hook<UpdateSkeletonComponent::Signature>
			FunctionHook("IVRDriverInput003::UpdateSkeletonComponent");

		static vr::EVRInputError Detour(vr::IVRDriverInput* _this,
										vr::VRInputComponentHandle_t ulComponent,
										vr::EVRSkeletalMotionRange eMotionRange,
										const vr::VRBoneTransform_t* pTransforms,
										uint32_t unTransformCount)
		{
			auto controller
				= HandOfLesser::Current->getHookedControllerByInputHandle(ulComponent);

			if (controller != nullptr)
			{
				if (HandOfLesser::Runtime.isSteamVR
					&& HandOfLesser::Current->isForwardedHandTrackingController(
						controller.get()))
				{
					// Skeletal and pose updates can arrive independently, so publish this
					// snapshot without waiting for the next pose callback.
					if (controller->cacheForwardedSkeleton(
							eMotionRange, pTransforms, unTransformCount))
					{
						HandOfLesser::Current->notifySteamVRHandTracking();
					}
				}

				if (controller->isAugmentedSkeletonActive())
				{
					return vr::VRInputError_None;
				}

				// SteamVR-runtime skeletal data is forwarded back to separate emulated controllers,
				// so the native source controller's own updates must still pass through unchanged.
				bool submittingSkeletalInput = !HandOfLesser::Runtime.isSteamVR;

				// While possessing a hooked controller, the app is already submitting the skeletal
				// data we want SteamVR to see. Swallow the native controller's own skeleton updates
				// so they cannot overwrite the possessed hand-tracking skeleton on the same handle.
				if (submittingSkeletalInput
					&& HandOfLesser::Current->shouldPossessInput(controller.get()))
				{
					return vr::VRInputError_None;
				}
			}

			return UpdateSkeletonComponent::FunctionHook.originalFunc(
				_this, ulComponent, eMotionRange, pTransforms, unTransformCount);
		};
	} // namespace UpdateSkeletonComponent

	namespace GetGenericInterface
	{
		Hook<GetGenericInterface::Signature> FunctionHook("IVRDriverContext::GetGenericInterface");

		static void* Detour(vr::IVRDriverContext* _this,
							const char* pchInterfaceVersion,
							vr::EVRInitError* peError)
		{
			void* originalInterface = GetGenericInterface::FunctionHook.originalFunc(
				_this, pchInterfaceVersion, peError);

			std::string iface(pchInterfaceVersion);
			if (iface == "IVRServerDriverHost_006")
			{
				DriverLog("Interface version: %s", iface.c_str());

				if (!IHook::Exists(TrackedDeviceAdded006::FunctionHook.name))
				{
					DriverLog("Adding added hook");

					TrackedDeviceAdded006::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 0, &TrackedDeviceAdded006::Detour);
					IHook::Register(&TrackedDeviceAdded006::FunctionHook);
				}

				if (!IHook::Exists(TrackedDevicePoseUpdated::FunctionHook.name))
				{
					DriverLog("Adding poseupdated hook");

					TrackedDevicePoseUpdated::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 1, &TrackedDevicePoseUpdated::Detour);
					IHook::Register(&TrackedDevicePoseUpdated::FunctionHook);
				}

				if (!IHook::Exists(PollNextEvent::FunctionHook.name))
				{
					DriverLog("Adding PollNextEvent hook");

					PollNextEvent::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 5, &PollNextEvent::Detour);
					IHook::Register(&PollNextEvent::FunctionHook);
				}
			}

			// There is also a IVRDriverInputInternal_XXX or something that needs to be avoided.
			if (iface.find("IVRDriverInput_") != std::string::npos)
			{
				DriverLog("Found Input interface: %s", iface.c_str());

				if (!IHook::Exists(UpdateBooleanComponent::FunctionHook.name))
				{
					DriverLog("Adding UpdateBooleanComponent hook");

					UpdateBooleanComponent::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 1, &UpdateBooleanComponent::Detour);
					IHook::Register(&UpdateBooleanComponent::FunctionHook);
				}

				if (!IHook::Exists(UpdateScalarComponent::FunctionHook.name))
				{
					DriverLog("Adding UpdateScalarComponent hook");

					UpdateScalarComponent::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 3, &UpdateScalarComponent::Detour);
					IHook::Register(&UpdateScalarComponent::FunctionHook);
				}

				if (!IHook::Exists(UpdateSkeletonComponent::FunctionHook.name))
				{
					DriverLog("Adding UpdateSkeletonComponent hook");

					UpdateSkeletonComponent::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 6, &UpdateSkeletonComponent::Detour);
					IHook::Register(&UpdateSkeletonComponent::FunctionHook);
				}

				if (!IHook::Exists(CreateBooleanComponent::FunctionHook.name))
				{
					DriverLog("Adding CreateBooleanComponent hook");

					CreateBooleanComponent::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 0, &CreateBooleanComponent::Detour);
					IHook::Register(&CreateBooleanComponent::FunctionHook);
				}

				if (!IHook::Exists(CreateScalarComponent::FunctionHook.name))
				{
					DriverLog("Adding CreateScalarComponent hook");

					CreateScalarComponent::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 2, &CreateScalarComponent::Detour);
					IHook::Register(&CreateScalarComponent::FunctionHook);
				}

				if (!IHook::Exists(CreateSkeletonComponent::FunctionHook.name))
				{
					DriverLog("Adding CreateSkeletonComponent hook");

					CreateSkeletonComponent::FunctionHook.CreateHookInObjectVTable(
						originalInterface, 5, &CreateSkeletonComponent::Detour);
					IHook::Register(&CreateSkeletonComponent::FunctionHook);
				}
			}

			return originalInterface;
		};
	} // namespace GetGenericInterface

	vr::IVRServerDriverHost* mLastDeviceDriverHost = nullptr;

	void InjectHooks(vr::IVRDriverContext* pDriverContext)
	{
		auto err = MH_Initialize();
		if (err == MH_OK)
		{
			GetGenericInterface::FunctionHook.CreateHookInObjectVTable(
				pDriverContext, 0, &GetGenericInterface::Detour);
			IHook::Register(&GetGenericInterface::FunctionHook);
		}
		else
		{
			DriverLog("MH_Initialize error: %s", MH_StatusToString(err));
		}
	}

	void DisableHooks()
	{
		IHook::DestroyAll();
		TrackedDeviceActivate::FunctionHooksByTarget.clear();
		MH_Uninitialize();
	}

} // namespace HOL::hooks
