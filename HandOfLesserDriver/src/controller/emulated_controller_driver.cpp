//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "emulated_controller_driver.h"

#include "driverlog.h"
#include "vrmath.h"
#include "controller_common.h"
#include "emulated_input_profile.h"
#include <HandOfLesserCommon.h>
#include <src/core/hand_of_lesser.h>

namespace HOL
{
	using namespace SteamVR;

	EmulatedControllerDriver::EmulatedControllerDriver(vr::ETrackedControllerRole role,
													   HOL::EmulatedControllerProfile profile,
													   vr::EVRSkeletalTrackingLevel trackingLevel)
	{
		// Set a member to keep track of whether we've activated yet or not
		is_active_ = false;

		// The constructor takes a role argument, that gives us information about if our controller
		// is a left or right hand. Let's store it for later use. We'll need it.
		my_controller_role_ = role;
		my_emulated_profile_ = profile;
		my_tracking_level_ = trackingLevel;
		const bool isLeftHand = my_controller_role_ == vr::TrackedControllerRole_LeftHand;

		my_controller_model_number_ = getEmulatedControllerModelName(profile, isLeftHand);
		my_controller_serial_number_ = getEmulatedControllerSerial(
			profile, trackingLevel, isLeftHand ? "HandOfLesserL" : "HandOfLesserR");

		// Here's an example of how to use our logging wrapper around IVRDriverLog
		// In SteamVR logs (SteamVR Hamburger Menu > Developer Settings > Web console) drivers have
		// a prefix of
		// "<driver_name>:". You can search this in the top search bar to find the info that you've
		// logged.
		DriverLog("HandOfLesser controller model number: %s", my_controller_model_number_.c_str());
		DriverLog("HandOfLesser controller serial number: %s",
				  my_controller_serial_number_.c_str());

		// New emulated controllers must stay disconnected until the driver decides
		// hand tracking is actually primary for that side.
		mLastPose = ControllerCommon::generateDisconnectedPose();
	}

	//-----------------------------------------------------------------------------
	// Purpose: This is called by vrserver after our
	//  IServerTrackedDeviceProvider calls IVRServerDriverHost::TrackedDeviceAdded.
	//-----------------------------------------------------------------------------
	vr::EVRInitError EmulatedControllerDriver::Activate(uint32_t unObjectId)
	{
		// Let's keep track of our device index. It'll be useful later.
		my_controller_index_ = unObjectId;

		auto props = vr::VRProperties();

		// Properties are stored in containers, usually one container per device index. We need to
		// get this container to set The properties we want, so we call this to retrieve a handle to
		// it.
		vr::PropertyContainerHandle_t container
			= props->TrackedDeviceToPropertyContainer(my_controller_index_);

		props->SetStringProperty(
			container, vr::Prop_RenderModelName_String, my_controller_model_number_.c_str());
		// props->SetStringProperty(container, vr::Prop_RenderModelName_String,
		// "C:\\Users\\Roughy\\workspace\\HandOfLesser\\output\\drivers\\handoflesser\\resources\\rendermodels\\MyControllerModelNumber\\MyControllerModelNumber.obj");

		// Let's begin setting up the properties now we've got our container.
		// A list of properties available is contained in vr::ETrackedDeviceProperty.

		// First, let's set the model number.
		props->SetStringProperty(
			container, vr::Prop_ModelNumber_String, my_controller_model_number_.c_str());

		// Let's tell SteamVR our role which we received from the constructor earlier.
		props->SetInt32Property(container, vr::Prop_ControllerRoleHint_Int32, my_controller_role_);

		// Now let's set up our inputs

		// This tells the UI what to show the user for bindings for this controller,
		// As well as what default bindings should be for legacy apps.
		// Note, we can use the wildcard {<driver_name>} to match the root folder location
		// of our driver.
		const bool isLeftHand = isLeftControllerRole(my_controller_role_);
		props->SetStringProperty(container,
								 vr::Prop_ModelNumber_String,
								 getEmulatedControllerModelName(my_emulated_profile_, isLeftHand));
		props->SetStringProperty(
			container,
			vr::Prop_RenderModelName_String,
			getEmulatedControllerRenderModelName(my_emulated_profile_, isLeftHand));
		props->SetStringProperty(container,
								 vr::Prop_ResourceRoot_String,
								 getEmulatedControllerResourceRoot(my_emulated_profile_));
		props->SetStringProperty(
			container,
			vr::Prop_RegisteredDeviceType_String,
			getEmulatedControllerRegisteredType(my_emulated_profile_, isLeftHand));
		props->SetStringProperty(container,
								 vr::Prop_InputProfilePath_String,
								 getEmulatedControllerInputProfilePath(my_emulated_profile_));
		props->SetStringProperty(container,
								 vr::Prop_ControllerType_String,
								 getEmulatedControllerTypeString(my_emulated_profile_));
		setEmulatedControllerIconProperties(props, container, my_emulated_profile_, isLeftHand);
		// Let's set up handles for all of our components.
		// Even though these are also defined in our input profile,
		// We need to get handles to them to update the inputs.

		auto input = vr::VRDriverInput();

		if (my_emulated_profile_
			== EmulatedControllerProfile::EmulatedControllerProfile_SteamLinkHandNative)
		{
			createScalarComponent(container, input, InputHandleType::index_pinch_value);
			createScalarComponent(container, input, InputHandleType::middle_pinch_value);
			createScalarComponent(container, input, InputHandleType::ring_pinch_value);
			createScalarComponent(container, input, InputHandleType::pinky_pinch_value);
			createScalarComponent(container, input, InputHandleType::grip_value);
			createBooleanComponent(container, input, InputHandleType::index_point_touch);
			createScalarComponent(container, input, InputHandleType::index_point_value);
			if (isLeftHand)
			{
				createBooleanComponent(container, input, InputHandleType::system_click);
			}
		}
		else
		{
			if (usesTouchControllerLayout(my_emulated_profile_))
			{
				// Oculus layout: left has X/Y, right has A/B.
				if (isLeftHand)
				{
					createBooleanComponent(container, input, InputHandleType::x_touch);
					createBooleanComponent(container, input, InputHandleType::x_click);
					createBooleanComponent(container, input, InputHandleType::y_touch);
					createBooleanComponent(container, input, InputHandleType::y_click);
				}
				else
				{
					createBooleanComponent(container, input, InputHandleType::a_touch);
					createBooleanComponent(container, input, InputHandleType::a_click);
					createBooleanComponent(container, input, InputHandleType::b_touch);
					createBooleanComponent(container, input, InputHandleType::b_click);
				}
			}
			else
			{
				createBooleanComponent(container, input, InputHandleType::a_touch);
				createBooleanComponent(container, input, InputHandleType::a_click);
				createBooleanComponent(container, input, InputHandleType::b_touch);
				createBooleanComponent(container, input, InputHandleType::b_click);
			}

			// Stick input (profile-specific naming).
			if (usesTouchControllerLayout(my_emulated_profile_))
			{
				createScalarComponent(container, input, InputHandleType::joystick_x);
				createScalarComponent(container, input, InputHandleType::joystick_y);
				createBooleanComponent(container, input, InputHandleType::joystick_touch);
				createBooleanComponent(container, input, InputHandleType::joystick_click);
			}
			else
			{
				createScalarComponent(container, input, InputHandleType::thumbstick_x);
				createScalarComponent(container, input, InputHandleType::thumbstick_y);
				createBooleanComponent(container, input, InputHandleType::thumbstick_touch);
				createBooleanComponent(container, input, InputHandleType::thumbstick_click);
			}

			// Let's set up our trigger. We've defined it to have a value and click component.

			// CreateScalarComponent requires:
			// EVRScalarType - whether the device can give an absolute position, or just one
			// relative to where it was last. We can do it absolute. EVRScalarUnits - whether the
			// devices has two "sides", like a joystick. This makes the range of valid inputs -1
			// to 1. Otherwise, it's 0 to 1. We only have one "side", so ours is onesided.

			///////////
			// Trigger
			///////////

			createScalarComponent(container, input, InputHandleType::trigger_value);
			createBooleanComponent(container, input, InputHandleType::trigger_touch);
			createBooleanComponent(container, input, InputHandleType::trigger_click);

			//////////
			// Grip
			//////////

			createScalarComponent(container, input, InputHandleType::grip_value);
			createScalarComponent(container, input, InputHandleType::grip_force);
			createBooleanComponent(container, input, InputHandleType::grip_touch);

			//////////////////
			// Finger curl
			/////////////////

			createScalarComponent(container, input, InputHandleType::finger_index);
			createScalarComponent(container, input, InputHandleType::finger_middle);
			createScalarComponent(container, input, InputHandleType::finger_ring);
			createScalarComponent(container, input, InputHandleType::finger_pinky);

			////////////////
			// Buttons
			////////////////

			createBooleanComponent(container, input, InputHandleType::system_click);
		}

		////////////////
		// Skeleton
		////////////////

		const vr::EVRInputError err = input->CreateSkeletonComponent(
			container, // container
			my_controller_role_ == vr::TrackedControllerRole_LeftHand
				? "/input/skeleton/left"
				: "/input/skeleton/right", // The path to the skeleton for input. This is for
										   // binding to applications.
			my_controller_role_ == vr::TrackedControllerRole_LeftHand
				? "/skeleton/hand/left"
				: "/skeleton/hand/right", // Where on the body this skeleton is.
			"/pose/raw", // Where the origin for the skeleton is going to be. These pose locations
						 // are defined in the render model file.
			my_tracking_level_, // Inform the runtime about the capabilities of the device.
			nullptr, // Used for calculating curl and splay values. If this is null then defaults
					 // are used.
			0,		 // How many bones there are in the gripLimitTransforms above.
			&mInputHandles[InputHandleType::skeleton] // Bind the component to a handle.
		);

		if (usesSteamLinkHandModel(my_emulated_profile_))
		{
			const auto poseError
				= input->CreatePoseComponent(container, "/pose/tip", &mTipPoseHandle);
			if (poseError != vr::VRInputError_None)
			{
				DriverLog("Failed to create /pose/tip component: %d", poseError);
			}
			else
			{
				DriverLog("Created emulated /pose/tip component");
			}
		}

		// initialise our hand tracking simulation class
		my_hand_simulation_ = std::make_unique<MyHandSimulation>();

		// Let's create our haptic component.
		// These are global across the device, and you can only have one per device.
		input->CreateHapticComponent(
			container, "/output/haptic", &mInputHandles[InputHandleType::haptic]);

		// Set an member to keep track of whether we've activated yet or not
		is_active_ = true;

		// If the controller was added while hand tracking was unavailable,
		// reassert the disconnected pose now that we have a valid device index.
		if (!mDeviceConnected)
		{
			mLastPose = ControllerCommon::generateDisconnectedPose();
			vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
				my_controller_index_, mLastPose, sizeof(vr::DriverPose_t));
		}

		// We've activated everything successfully!
		// Let's tell SteamVR that by saying we don't have any errors.
		return vr::VRInitError_None;
	}

	//-----------------------------------------------------------------------------
	// Purpose: If you're an HMD, this is where you would return an implementation
	// of vr::IVRDisplayComponent, vr::IVRVirtualDisplay or vr::IVRDirectModeComponent.
	//
	// But this a simple example to demo for a controller, so we'll just return nullptr here.
	//-----------------------------------------------------------------------------
	void* EmulatedControllerDriver::GetComponent(const char* pchComponentNameAndVersion)
	{
		return nullptr;
	}

	//-----------------------------------------------------------------------------
	// Purpose: This is called by vrserver when a debug request has been made from an application to
	// the driver. What is in the response and request is up to the application and driver to figure
	// out themselves.
	//-----------------------------------------------------------------------------
	void EmulatedControllerDriver::DebugRequest(const char* pchRequest,
												char* pchResponseBuffer,
												uint32_t unResponseBufferSize)
	{
		if (unResponseBufferSize >= 1)
			pchResponseBuffer[0] = 0;
	}

	//-----------------------------------------------------------------------------
	// Purpose: This is never called by vrserver in recent OpenVR versions,
	// but is useful for giving data to vr::VRServerDriverHost::TrackedDevicePoseUpdated.
	//-----------------------------------------------------------------------------
	vr::DriverPose_t EmulatedControllerDriver::GetPose()
	{
		return this->mLastPose;
	}

	void EmulatedControllerDriver::setConnectedState(bool connected)
	{
		if (mDeviceConnected == connected)
		{
			return;
		}

		mDeviceConnected = connected;
		if (!connected)
		{
			mLastPose = ControllerCommon::generateDisconnectedPose();
			if (is_active_ && my_controller_index_ != vr::k_unTrackedDeviceIndexInvalid)
			{
				vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
					my_controller_index_, mLastPose, sizeof(vr::DriverPose_t));
			}
		}
	}

	bool HOL::EmulatedControllerDriver::isConnected() const
	{
		return mDeviceConnected.load();
	}

	vr::VRInputComponentHandle_t
	EmulatedControllerDriver::createBooleanComponent(vr::PropertyContainerHandle_t container,
													 vr::IVRDriverInput* input,
													 InputHandleType type)
	{
		input->CreateBooleanComponent(container, INPUT_PATHS[type].c_str(), &mInputHandles[type]);
		return mInputHandles[type];
	}

	vr::VRInputComponentHandle_t
	EmulatedControllerDriver::createScalarComponent(vr::PropertyContainerHandle_t container,
													vr::IVRDriverInput* input,
													InputHandleType type)
	{
		vr::EVRScalarUnits units = vr::VRScalarUnits_NormalizedOneSided;
		// Stick X/Y are centered axes (-1..1); trigger, grip, and finger values are one-sided
		// (0..1).
		if (type == InputHandleType::thumbstick_x || type == InputHandleType::thumbstick_y
			|| type == InputHandleType::joystick_x || type == InputHandleType::joystick_y)
		{
			units = vr::VRScalarUnits_NormalizedTwoSided;
		}

		input->CreateScalarComponent(container,
									 INPUT_PATHS[type].c_str(),
									 &mInputHandles[type],
									 vr::VRScalarType_Absolute,
									 units);
		return mInputHandles[type];
	}

	void EmulatedControllerDriver::UpdatePose(HOL::HandTransformPayload* payload)
	{
		this->mLastTransformPayload = *payload;

		// Keep updating as long as pose is valid
		if (this->mLastTransformPayload.valid)
		{
			this->mLastPose = ControllerCommon::generatePose(&this->mLastTransformPayload, true);
		}
	}

	void EmulatedControllerDriver::UpdateBoolInput(const std::string& input, bool value)
	{
		if (!mDeviceConnected)
			return;

		auto driverInput = vr::VRDriverInput();
		HandSide side = my_controller_role_ == vr::TrackedControllerRole_LeftHand
							? HandSide::LeftHand
							: HandSide::RightHand;
		std::string inputName = mapEmulatedInputPath(my_emulated_profile_, side, input);

		auto inputType = INPUT_TYPES.find(inputName);
		if (inputType != INPUT_TYPES.end())
		{
			const auto handle = mInputHandles[inputType->second];
			if (handle != vr::k_ulInvalidInputComponentHandle)
			{
				driverInput->UpdateBooleanComponent(handle, value, 0);
			}
		}
	}

	void EmulatedControllerDriver::UpdateFloatInput(const std::string& input, float value)
	{
		if (!mDeviceConnected)
			return;

		auto driverInput = vr::VRDriverInput();
		HandSide side = my_controller_role_ == vr::TrackedControllerRole_LeftHand
							? HandSide::LeftHand
							: HandSide::RightHand;
		std::string inputName = mapEmulatedInputPath(my_emulated_profile_, side, input);

		auto inputType = INPUT_TYPES.find(inputName);
		if (inputType != INPUT_TYPES.end())
		{
			const auto handle = mInputHandles[inputType->second];
			if (handle != vr::k_ulInvalidInputComponentHandle)
			{
				driverInput->UpdateScalarComponent(handle, value, 0);
			}
		}
	}

	void EmulatedControllerDriver::UpdateSkeletal(HOL::SkeletalPayload* payload)
	{
		if (!mDeviceConnected)
		{
			return;
		}

		SteamVR::buildSkeletalPoseFromPayload(*payload, mSkeletalPose);

		vr::VRDriverInput()->UpdateSkeletonComponent(mInputHandles[InputHandleType::skeleton],
													 vr::VRSkeletalMotionRange_WithoutController,
													 mSkeletalPose,
													 eBone_Count);
	}

	void EmulatedControllerDriver::UpdateTipPose(const vr::HmdMatrix34_t& transform)
	{
		if (!usesSteamLinkHandModel(my_emulated_profile_) || !is_active_ || !mDeviceConnected
			|| mTipPoseHandle == vr::k_ulInvalidInputComponentHandle)
		{
			return;
		}

		vr::VRDriverInput()->UpdatePoseComponent(mTipPoseHandle, &transform, 0.0);
	}

	void EmulatedControllerDriver::SubmitPose()
	{
		if (this->is_active_ && mDeviceConnected)
		{
			vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
				my_controller_index_, GetPose(), sizeof(vr::DriverPose_t));
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose: This is called by vrserver when the device should enter standby mode.
	// The device should be put into whatever low power mode it has.
	// We don't really have anything to do here, so let's just log something.
	//-----------------------------------------------------------------------------
	void EmulatedControllerDriver::EnterStandby()
	{
		DriverLog("%s hand has been put on standby",
				  vr::TrackedControllerRole_LeftHand ? "Left" : "Right");
	}

	//-----------------------------------------------------------------------------
	// Purpose: This is called by vrserver when the device should deactivate.
	// This is typically at the end of a session
	// The device should free any resources it has allocated here.
	//-----------------------------------------------------------------------------
	void EmulatedControllerDriver::Deactivate()
	{
		// unassign our controller index (we don't want to be calling vrserver anymore after
		// Deactivate() has been called
		my_controller_index_ = vr::k_unTrackedDeviceIndexInvalid;
	}

	//-----------------------------------------------------------------------------
	// Purpose: This is called by our IServerTrackedDeviceProvider when its RunFrame() method gets
	// called. It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
	//-----------------------------------------------------------------------------
	void EmulatedControllerDriver::MyRunFrame()
	{
		if (!mDeviceConnected)
			return;
	}

	//-----------------------------------------------------------------------------
	// Purpose: This is called by our IServerTrackedDeviceProvider when it pops an event off the
	// event queue. It's not part of the ITrackedDeviceServerDriver interface, we created it
	// ourselves.
	//-----------------------------------------------------------------------------
	void EmulatedControllerDriver::MyProcessEvent(const vr::VREvent_t& vrevent)
	{
		switch (vrevent.eventType)
		{
			// Listen for haptic events
			case vr::VREvent_Input_HapticVibration: {
				// We now need to make sure that the event was intended for this device.
				// So let's compare handles of the event and our haptic component
				const auto& haptic = vrevent.data.hapticVibration;
				if (haptic.componentHandle == mInputHandles[InputHandleType::haptic])
				{
					// The event was intended for us!
					// To convert the data to a pulse, see the docs.
					// For this driver, we'll just print the values.

					const float duration = haptic.fDurationSeconds;
					const float frequency = haptic.fFrequency;
					const float amplitude = haptic.fAmplitude;

					DriverLog("Haptic event triggered for %s hand. Duration: %.2f, Frequency: "
							  "%.2f, Amplitude: "
							  "%.2f",
							  my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "left"
																						: "right",
							  duration,
							  frequency,
							  amplitude);
				}
				break;
			}
			default:
				break;
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose: Our IServerTrackedDeviceProvider needs our serial number to add us to vrserver.
	// It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
	//-----------------------------------------------------------------------------
	const std::string& EmulatedControllerDriver::MyGetSerialNumber()
	{
		return my_controller_serial_number_;
	}

} // namespace HOL
