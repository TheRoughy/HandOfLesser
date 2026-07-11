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
		void init(xr::UniqueDynamicInstance& instance, xr::UniqueDynamicSession& session);
		void updateBody(xr::UniqueDynamicSpace& space,
						XrTime time,
						const HOL::PoseLocation* hmdPose,
						const std::array<const HOL::HandPose*, HOL::HandSide_MAX>& lastHandPoses);
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
