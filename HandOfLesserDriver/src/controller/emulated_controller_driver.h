//============ Copyright (c) Valve Corporation, All rights reserved. ============
#pragma once

#include <array>
#include <string>

#include "openvr_driver.h"
#include <HandOfLesserCommon.h>
#include <atomic>
#include <thread>
#include "src/hand_simulation.h"
#include "generic_control_interface.h"

namespace HOL
{
	enum InputHandleType
	{
		a_touch,
		a_click,
		b_touch,
		b_click,
		x_touch,
		x_click,
		y_touch,
		y_click,

		joystick_x,
		joystick_y,
		joystick_touch,
		joystick_click,

		thumbstick_x,
		thumbstick_y,
		thumbstick_touch,
		thumbstick_click,

		trigger_value,
		trigger_touch,
		trigger_click,

		grip_value,
		grip_force,
		grip_touch,

		system_click,

		finger_index,
		finger_middle,
		finger_ring,
		finger_pinky,

		skeleton,

		haptic,

		MAX
	};

	inline std::array<std::string, InputHandleType::MAX> INPUT_PATHS = []
	{
		std::array<std::string, HOL::InputHandleType::MAX> arr;

		using namespace HOL::SteamVR::Input;

		arr[a_touch] = A.touch();
		arr[a_click] = A.click();
		arr[b_touch] = B.touch();
		arr[b_click] = B.click();
		arr[x_touch] = X.touch();
		arr[x_click] = X.click();
		arr[y_touch] = Y.touch();
		arr[y_click] = Y.click();

		arr[joystick_x] = Joystick.x();
		arr[joystick_y] = Joystick.y();
		arr[joystick_touch] = Joystick.touch();
		arr[joystick_click] = Joystick.click();

		arr[thumbstick_x] = Thumbstick.x();
		arr[thumbstick_y] = Thumbstick.y();
		arr[thumbstick_touch] = Thumbstick.touch();
		arr[thumbstick_click] = Thumbstick.click();
		arr[trigger_value] = Trigger.value();
		arr[trigger_touch] = Trigger.touch();
		arr[trigger_click] = Trigger.click();
		arr[grip_value] = Grip.value();
		arr[grip_force] = Grip.force();
		arr[grip_touch] = Grip.touch();
		arr[system_click] = System.click();
		arr[finger_index] = Finger.index();
		arr[finger_middle] = Finger.middle();
		arr[finger_ring] = Finger.ring();
		arr[finger_pinky] = Finger.pinky();

		return arr;
	}();

	inline std::unordered_map<std::string, InputHandleType> INPUT_TYPES = []
	{
		std::unordered_map<std::string, InputHandleType> map;

		int index = 0;
		for (auto& it : INPUT_PATHS)
		{
			map.insert(std::make_pair(it, (InputHandleType)index));
			index++;
		}

		return map;
	}();

	//-----------------------------------------------------------------------------
	// Purpose: Represents a single tracked device in the system.
	// What this device actually is (controller, hmd) depends on the
	// properties you set within the device (see implementation of Activate)
	//-----------------------------------------------------------------------------
	class EmulatedControllerDriver : public vr::ITrackedDeviceServerDriver,
									 public HOL::GenericControllerInterface
	{
	public:
		EmulatedControllerDriver(vr::ETrackedControllerRole role,
								 HOL::EmulatedControllerProfile profile,
								 vr::EVRSkeletalTrackingLevel trackingLevel);

		vr::EVRInitError Activate(uint32_t unObjectId) override;

		void EnterStandby() override;

		void* GetComponent(const char* pchComponentNameAndVersion) override;

		void DebugRequest(const char* pchRequest,
						  char* pchResponseBuffer,
						  uint32_t unResponseBufferSize) override;

		vr::DriverPose_t GetPose() override;

		void setConnectedState(bool connected);

		vr::VRInputComponentHandle_t createBooleanComponent(vr::PropertyContainerHandle_t container,
															vr::IVRDriverInput* input,
															InputHandleType type);

		vr::VRInputComponentHandle_t createScalarComponent(vr::PropertyContainerHandle_t container,
														   vr::IVRDriverInput* input,
														   InputHandleType type);

		void UpdatePose(HOL::HandTransformPayload* payload) override;
		void UpdateBoolInput(const std::string& input, bool value) override;
		void UpdateFloatInput(const std::string& input, float value) override;
		void UpdateSkeletal(HOL::SkeletalPayload* payload) override;
		void UpdateTipPose(const vr::HmdMatrix34_t& transform);
		void SubmitPose() override;
		bool isConnected() const;

		void Deactivate() override;

		// ----- Functions we declare ourselves below -----

		const std::string& MyGetSerialNumber();

		void MyRunFrame();
		void MyProcessEvent(const vr::VREvent_t& vrevent);

	private:
		void UpdateSkeleton();

		std::unique_ptr<MyHandSimulation> my_hand_simulation_;
		std::atomic<int> frame_ = 0;
		std::atomic<float> last_curl_ = 0.f;
		std::atomic<float> last_splay_ = 0.f;

		std::atomic<vr::TrackedDeviceIndex_t> my_controller_index_
			= vr::k_unTrackedDeviceIndexInvalid;

		vr::ETrackedControllerRole my_controller_role_;
		HOL::EmulatedControllerProfile my_emulated_profile_;
		vr::EVRSkeletalTrackingLevel my_tracking_level_;

		std::string my_controller_model_number_;
		std::string my_controller_serial_number_;

		std::array<vr::VRInputComponentHandle_t, InputHandleType::MAX> mInputHandles{};
		vr::VRInputComponentHandle_t mTipPoseHandle = vr::k_ulInvalidInputComponentHandle;

		std::atomic<bool> is_active_ = false;
		std::atomic<bool> mDeviceConnected = false;

		vr::VRBoneTransform_t mSkeletalPose[eBone_Count];

		vr::DriverPose_t mLastPose;

		HOL::HandTransformPayload mLastTransformPayload;
	};

} // namespace HOL
