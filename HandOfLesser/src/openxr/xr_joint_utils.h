#pragma once

#include <d3d11.h>
#include <openxr/openxr_platform.h>
#include "openxr/openxr_structs.hpp"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <HandOfLesserCommon.h>

namespace HOL::OpenXR
{
	XrBodyJointLocationFB toXrBodyJointLocation(const HOL::PoseLocation& pose);
	bool reconstructPalmJoint(XrHandJointLocationEXT jointLocations[],
							  XrHandJointVelocityEXT jointVelocities[]);
	bool reconstructPalmJoint(XrBodyJointLocationFB jointLocations[], HOL::HandSide side);

	XrHandJointLocationEXT& getJoint(XrHandJointLocationEXT leftHandJoints[],
									 XrHandJointLocationEXT rightHandJoints[],
									 XrHandJointEXT joint,
									 HOL::HandSide side);

	XrHandJointLocationEXT& getJoint(XrHandJointLocationEXT leftHandJoints[], XrHandJointEXT joint);

	Eigen::Vector3f getJointPosition(XrHandJointLocationEXT leftHandJoints[],
									 XrHandJointLocationEXT rightHandJoints[],
									 XrHandJointEXT joint,
									 HOL::HandSide side);

	Eigen::Vector3f getJointPosition(XrHandJointLocationEXT leftHandJoints[], XrHandJointEXT joint);

	Eigen::Vector3f getJointPosition(XrHandJointLocationEXT joint);

	Eigen::Quaternionf getJointOrientation(XrHandJointLocationEXT leftHandJoints[],
										   XrHandJointLocationEXT rightHandJoints[],
										   XrHandJointEXT joint,
										   HOL::HandSide side);

	Eigen::Quaternionf getJointOrientation(XrHandJointLocationEXT leftHandJoints[],
										   XrHandJointEXT joint);

	Eigen::Quaternionf getJointOrientation(XrHandJointLocationEXT leftHandJoints);

	// Returns the metacarpal for all fingers but the thumb, where the wrist is returned instead.
	// The returned value +4 will give you the tip of each finger, even for the thumb.
	XrHandJointEXT getRootJoint(HOL::FingerType fingerType);

	// The first joint that actually moves, so excluding metacarpal for everything but thumb.
	XrHandJointEXT getFirstFingerJoint(HOL::FingerType fingerType);
	XrHandJointEXT getSecondFingerJoint(HOL::FingerType fingerType);

	XrHandJointEXT getFingerTip(HOL::FingerType fingerType);

} // namespace HOL::OpenXR
