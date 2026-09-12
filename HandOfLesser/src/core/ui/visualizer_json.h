#pragma once

#include "visualizer.h"

#include <nlohmann/json.hpp>

// OpenXR's C structs and Eigen's quaternion do not provide JSON conversion themselves. Defining
// their leaf conversions here lets nlohmann recursively handle the snapshot arrays and objects.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrVector3f, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrQuaternionf, x, y, z, w)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrPosef, orientation, position)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrHandJointLocationEXT, locationFlags, pose, radius)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(XrBodyJointLocationFB, locationFlags, pose)

namespace nlohmann
{
	template <> struct adl_serializer<Eigen::Quaternionf>
	{
		static void to_json(json& j, const Eigen::Quaternionf& quaternion)
		{
			j = {{"x", quaternion.x()},
				 {"y", quaternion.y()},
				 {"z", quaternion.z()},
				 {"w", quaternion.w()}};
		}

		static void from_json(const json& j, Eigen::Quaternionf& quaternion)
		{
			quaternion = Eigen::Quaternionf(j.at("w").get<float>(),
										 j.at("x").get<float>(),
										 j.at("y").get<float>(),
										 j.at("z").get<float>());
		}
	};
} // namespace nlohmann

namespace HOL
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PoseLocation, position, orientation)
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
