#pragma once

#include <d3d11.h>
#include <openxr/openxr.h>

#include <cstdint>

namespace HOL
{
	// OpenXR-shaped body data supplied without calling xrLocateBodyJointsFB.
	struct BodyTrackingSample
	{
		bool active = false;
		float confidence = 0.0f;
		// Providers with an update counter can expose exact staleness without comparing poses.
		bool hasUpdateGeneration = false;
		uint64_t updateGeneration = 0;
		XrBodyJointLocationFB joints[XR_BODY_JOINT_COUNT_FB]{};
	};
} // namespace HOL
