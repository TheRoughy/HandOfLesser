#pragma once

#include <array>
#include <d3d11.h> // Why do you need this??
#include <memory>
#include "openxr_body.h" // replace with body!
#include <HandOfLesserCommon.h>

namespace HOL::OpenXR
{
	class BodyTracking
	{
	public:
		// Common caches are needed by both OpenXR and externally supplied body samples.
		void init();
		// The native body tracker is optional when another provider supplies normalized samples.
		void initOpenXR(xr::UniqueDynamicSession& session);
		void updateBody(XrSpace space,
						XrTime time,
						const HOL::PoseLocation* hmdPose,
						const std::array<const HOL::HandPose*, HOL::HandSide_MAX>& lastHandPoses,
						const HOL::BodyTrackingSample* externalSample = nullptr);
		void drawBody();
		OpenXRBody& getBodyTracker();
		HOL::MultimodalPosePayload getMultimodalPosePayload();
		std::vector<HOL::BodyTrackerPosePayload> getBodyTrackerPayloads();

	private:
		OpenXRBody mBodyTracker;
		std::array<HOL::PoseLocation, static_cast<int>(HOL::BodyTrackerRole::TrackerRole_MAX)>
			mLastBodyTrackerLocations;
	};
} // namespace HOL::OpenXR
