#pragma once

#include "visualizer.h"

#include <nlohmann/json.hpp>

// OpenXR's C structs do not provide JSON conversion themselves. Defining their leaf conversions
// here lets nlohmann recursively handle the snapshot arrays and objects.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrVector3f, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrQuaternionf, x, y, z, w)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrPosef, orientation, position)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrHandJointLocationEXT, locationFlags, pose, radius)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrBodyJointLocationFB, locationFlags, pose)

namespace HOL
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(VisualizerHandState, active, valid, tracked, joints)
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(VisualizerControllerState, active, valid, tracked, pose)
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TrackingVisualizationFrame,
									   valid,
									   runtimeName,
									   provider,
									   hands,
									   bodyAvailable,
									   bodyActive,
									   bodyConfidence,
									   bodyJoints,
									   controllers,
									   bodyTrackerLocations)
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TrackingVisualizationSnapshot, capturedAt, frame)
} // namespace HOL
