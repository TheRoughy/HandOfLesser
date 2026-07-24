#pragma once

#include <d3d11.h> // Why do you need this??
#include <openxr/openxr_platform.h>
#include <openxr/openxr.hpp>
#include <meta_body_tracking_fidelity.h>

class HandTrackingInterface
{
public:
	static void init(xr::UniqueDynamicInstance& instance);

	// Hand
	static void createHandTracker(xr::UniqueDynamicSession& session,
								  XrHandEXT side,
								  XrHandTrackerEXT& handTrackerOut,
								  bool requestUnobstructedDataSource = false);
	static XrResult locateHandJoints(XrHandTrackerEXT& handTracker,
									 XrSpace space,
									 XrTime time,
									 XrHandJointLocationEXT* handJointLocationsOut,
									 XrHandJointVelocityEXT* handJointVelocitiesOut,
									 bool& handActiveOut,
									 XrHandTrackingAimStateFB* handAimStateOut = nullptr,
									 XrHandTrackingDataSourceStateEXT* handDataSourceStateOut = nullptr);
	static void destroyHandTracker(XrHandTrackerEXT& handTracker);

	// Body
	static void createBodyTracker(xr::UniqueDynamicSession& session,
								  XrBodyTrackerFB& bodyTrackerOut);
	static void destroyBodyTracker(XrBodyTrackerFB& bodyTracker);
	static XrResult locateBodyJoints(XrBodyTrackerFB& bodyTracker,
									 XrSpace space,
									 XrTime time,
									 XrBodyJointLocationFB* bodyJointLocationsOut,
									 float& confidenceOut);

	// Multimodal
	static void resumeMultimodal(xr::UniqueDynamicSession& session);
	static void pauseMultimodal(xr::UniqueDynamicSession& session);

	// IOBT
	static void requestBodyTrackingFidelity(XrBodyTrackerFB bodyTracker,
											XrBodyTrackingFidelityMETA fidelity);

private:
	static PFN_xrCreateHandTrackerEXT xrCreateHandTrackerEXT_;
	static PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT_;
	static PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT_;

	static PFN_xrResumeSimultaneousHandsAndControllersTrackingMETA
		xrResumEsimultaneousHandsAndControllersTrackingMETA_;
	static PFN_xrPauseSimultaneousHandsAndControllersTrackingMETA
		xrPauseSimultaneousHandsAndControllersTrackingMETA_;

	static PFN_xrRequestBodyTrackingFidelityMETA xrRequestBodyTrackingFidelityMETA_;

	static PFN_xrCreateBodyTrackerFB xrCreateBodyTrackerFB_;
	static PFN_xrDestroyBodyTrackerFB xrDestroyBodyTrackerFB_;
	static PFN_xrLocateBodyJointsFB xrLocateBodyJointsFB_;

	static void initFunctions(xr::UniqueDynamicInstance& instance);
};
