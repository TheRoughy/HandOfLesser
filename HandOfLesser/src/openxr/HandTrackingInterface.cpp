#include "HandTrackingInterface.h"
#include "XrUtils.h"
#include "src/core/state_global.h"
#include <cstring>

// Static members need to be initialized
PFN_xrCreateHandTrackerEXT HandTrackingInterface::xrCreateHandTrackerEXT_ = nullptr;
PFN_xrDestroyHandTrackerEXT HandTrackingInterface::xrDestroyHandTrackerEXT_ = nullptr;
PFN_xrLocateHandJointsEXT HandTrackingInterface::xrLocateHandJointsEXT_ = nullptr;

PFN_xrResumeSimultaneousHandsAndControllersTrackingMETA
	HandTrackingInterface::xrResumEsimultaneousHandsAndControllersTrackingMETA_
	= nullptr;

PFN_xrPauseSimultaneousHandsAndControllersTrackingMETA
	HandTrackingInterface::xrPauseSimultaneousHandsAndControllersTrackingMETA_
	= nullptr;

PFN_xrRequestBodyTrackingFidelityMETA HandTrackingInterface::xrRequestBodyTrackingFidelityMETA_
	= nullptr;

XrPath LeftHandInteractionPath;
XrPath RightHandInteractionPath;

PFN_xrCreateBodyTrackerFB HandTrackingInterface::xrCreateBodyTrackerFB_ = nullptr;
PFN_xrDestroyBodyTrackerFB HandTrackingInterface::xrDestroyBodyTrackerFB_ = nullptr;
PFN_xrLocateBodyJointsFB HandTrackingInterface::xrLocateBodyJointsFB_ = nullptr;

using namespace HOL::OpenXR;

void HandTrackingInterface::init(xr::UniqueDynamicInstance& instance)
{
	initFunctions(instance);
}

void HandTrackingInterface::initFunctions(xr::UniqueDynamicInstance& instance)
{
	XrInstance inst = instance.get();

	// Hand
	handleXR("xrCreateHandTrackerEXT get",
			 xrGetInstanceProcAddr(
				 inst, "xrCreateHandTrackerEXT", (PFN_xrVoidFunction*)(&xrCreateHandTrackerEXT_)));

	handleXR("xrDestroyHandTrackerEXT get",
			 xrGetInstanceProcAddr(inst,
								   "xrDestroyHandTrackerEXT",
								   (PFN_xrVoidFunction*)(&xrDestroyHandTrackerEXT_)));

	handleXR("xrLocateHandJointsEXT get",
			 xrGetInstanceProcAddr(
				 inst, "xrLocateHandJointsEXT", (PFN_xrVoidFunction*)(&xrLocateHandJointsEXT_)));

	// Body
	if (HOL::state::Runtime.supportsBodyTracking)
	{
		handleXR("xrCreateBodyTrackerFB get",
				 xrGetInstanceProcAddr(inst,
									   "xrCreateBodyTrackerFB",
									   (PFN_xrVoidFunction*)(&xrCreateBodyTrackerFB_)));

		handleXR("xrDestroyBodyTrackerFB get",
				 xrGetInstanceProcAddr(inst,
									   "xrDestroyBodyTrackerFB",
									   (PFN_xrVoidFunction*)(&xrDestroyBodyTrackerFB_)));

		handleXR("xrLocateBodyJointsFB get",
				 xrGetInstanceProcAddr(inst,
									   "xrLocateBodyJointsFB",
									   (PFN_xrVoidFunction*)(&xrLocateBodyJointsFB_)));
	}

	// Multimodal
	handleXR("xrResumEsimultaneousHandsAndControllersTrackingMETA get",
			 xrGetInstanceProcAddr(
				 inst,
				 "xrResumeSimultaneousHandsAndControllersTrackingMETA",
				 (PFN_xrVoidFunction*)(&xrResumEsimultaneousHandsAndControllersTrackingMETA_)));

	handleXR("xrPauseSimultaneousHandsAndControllersTrackingMETA get",
			 xrGetInstanceProcAddr(
				 inst,
				 "xrPauseSimultaneousHandsAndControllersTrackingMETA",
				 (PFN_xrVoidFunction*)(&xrPauseSimultaneousHandsAndControllersTrackingMETA_)));

	// IOBT
	handleXR("xrRequestBodyTrackingFidelityMETA get",
			 xrGetInstanceProcAddr(inst,
								   "xrRequestBodyTrackingFidelityMETA",
								   (PFN_xrVoidFunction*)(&xrRequestBodyTrackingFidelityMETA_)));
}

void HandTrackingInterface::createHandTracker(xr::UniqueDynamicSession& session,
											  XrHandEXT side,
											  XrHandTrackerEXT& handTrackerOut,
											  bool requestUnobstructedDataSource)
{
	XrHandTrackerCreateInfoEXT createInfo{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
	createInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
	createInfo.hand = side;

	XrHandTrackingDataSourceEXT requestedDataSource = XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT;
	XrHandTrackingDataSourceInfoEXT dataSourceInfo{XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT};
	if (requestUnobstructedDataSource)
	{
		dataSourceInfo.requestedDataSourceCount = 1;
		dataSourceInfo.requestedDataSources = &requestedDataSource;
		createInfo.next = &dataSourceInfo;
	}

	handleXR("xrCreateHandTrackerEXT_ call",
			 xrCreateHandTrackerEXT_(session.get(), &createInfo, &handTrackerOut));
}

XrResult HandTrackingInterface::locateHandJoints(XrHandTrackerEXT& handTracker,
												 XrSpace space,
												 XrTime time,
												 XrHandJointLocationEXT* handJointLocationsOut,
												 XrHandJointVelocityEXT* handJointVelocitiesOut,
												 bool& handActiveOut,
												 XrHandTrackingAimStateFB* aimStateOut,
												 XrHandTrackingDataSourceStateEXT* dataSourceStateOut)
{
	handActiveOut = false;

	XrHandJointVelocitiesEXT velocities{XR_TYPE_HAND_JOINT_VELOCITIES_EXT};
	velocities.next = nullptr;
	velocities.jointCount = XR_HAND_JOINT_COUNT_EXT;
	velocities.jointVelocities = handJointVelocitiesOut;

	XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
	void* next = &velocities;

	if (dataSourceStateOut != nullptr)
	{
		dataSourceStateOut->next = next;
		next = dataSourceStateOut;
	}

	if (aimStateOut != nullptr)
	{
		aimStateOut->next = next;
		next = aimStateOut;
	}

	locations.next = next;
	locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
	locations.jointLocations = handJointLocationsOut;

	XrHandJointsLocateInfoEXT locateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
	locateInfo.baseSpace = space;
	locateInfo.time = time;

	XrResult result = xrLocateHandJointsEXT_(handTracker, &locateInfo, &locations);
	if (!handleXR("xrLocateHandJointsEXT_ call", result))
	{
		std::memset(handJointLocationsOut, 0, sizeof(XrHandJointLocationEXT) * XR_HAND_JOINT_COUNT_EXT);
		std::memset(handJointVelocitiesOut,
					0,
					sizeof(XrHandJointVelocityEXT) * XR_HAND_JOINT_COUNT_EXT);
		if (aimStateOut != nullptr)
		{
			*aimStateOut = {XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
		}
		if (dataSourceStateOut != nullptr)
		{
			*dataSourceStateOut = {XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
		}
		
		return result;
	}

	handActiveOut = locations.isActive;
	return result;
}

void HandTrackingInterface::destroyHandTracker(XrHandTrackerEXT& handTracker)
{
	if (handTracker == nullptr || xrDestroyHandTrackerEXT_ == nullptr)
	{
		return;
	}

	handleXR("xrDestroyHandTrackerEXT call", xrDestroyHandTrackerEXT_(handTracker));
	handTracker = nullptr;
}

void HandTrackingInterface::createBodyTracker(xr::UniqueDynamicSession& session,
											  XrBodyTrackerFB& bodyTrackerOut)
{
	XrBodyTrackerCreateInfoFB createInfo{XR_TYPE_BODY_TRACKER_CREATE_INFO_FB};
	createInfo.bodyJointSet = XR_BODY_JOINT_SET_DEFAULT_FB;
	createInfo.next = nullptr;
	handleXR("xrCreateBodyTrackerFB_ call",
			 xrCreateBodyTrackerFB_(session.get(), &createInfo, &bodyTrackerOut));
}

void HandTrackingInterface::destroyBodyTracker(XrBodyTrackerFB& bodyTracker)
{
	if (bodyTracker == nullptr || xrDestroyBodyTrackerFB_ == nullptr)
	{
		return;
	}

	handleXR("xrDestroyBodyTrackerFB call", xrDestroyBodyTrackerFB_(bodyTracker));
	bodyTracker = nullptr;
}

XrResult HandTrackingInterface::locateBodyJoints(XrBodyTrackerFB& bodyTracker,
												 XrSpace space,
												 XrTime time,
												 XrBodyJointLocationFB* bodyJointLocationsOut,
												 float& confidenceOut)
{
	confidenceOut = 0.0f;

	XrBodyJointLocationsFB locations{XR_TYPE_BODY_JOINT_LOCATIONS_FB};
	locations.next = NULL;
	locations.jointCount = XR_BODY_JOINT_COUNT_FB;
	locations.jointLocations = bodyJointLocationsOut;

	XrBodyJointsLocateInfoFB locateInfo{XR_TYPE_BODY_JOINTS_LOCATE_INFO_FB};
	locateInfo.baseSpace = space;
	locateInfo.time = time;

	XrResult result = xrLocateBodyJointsFB_(bodyTracker, &locateInfo, &locations);
	if (!handleXR("xrLocateBodyJointsFB_ call", result))
	{
		std::memset(bodyJointLocationsOut, 0, sizeof(XrBodyJointLocationFB) * XR_BODY_JOINT_COUNT_FB);
		return result;
	}

	confidenceOut = locations.confidence;
	return result;
}

void HandTrackingInterface::resumeMultimodal(xr::UniqueDynamicSession& session)
{
	XrSimultaneousHandsAndControllersTrackingResumeInfoMETA resumeInfo{
		XR_TYPE_SIMULTANEOUS_HANDS_AND_CONTROLLERS_TRACKING_RESUME_INFO_META};

	resumeInfo.next = nullptr;

	handleXR("XrSystemSimultaneousHandsAndControllersPropertiesMETA_ call",
			 xrResumEsimultaneousHandsAndControllersTrackingMETA_(session.get(), &resumeInfo));
}

void HandTrackingInterface::pauseMultimodal(xr::UniqueDynamicSession& session)
{
	XrSimultaneousHandsAndControllersTrackingPauseInfoMETA pauseInfo{
		XR_TYPE_SIMULTANEOUS_HANDS_AND_CONTROLLERS_TRACKING_PAUSE_INFO_META};

	pauseInfo.next = nullptr;

	handleXR("xrPauseSimultaneousHandsAndControllersTrackingMETA call",
			 xrPauseSimultaneousHandsAndControllersTrackingMETA_(session.get(), &pauseInfo));
}

void HandTrackingInterface::requestBodyTrackingFidelity(XrBodyTrackerFB bodyTracker,
														XrBodyTrackingFidelityMETA fidelity)
{
	handleXR("xrRequestBodyTrackingFidelityMETA call",
			 xrRequestBodyTrackingFidelityMETA_(bodyTracker, fidelity));
}
