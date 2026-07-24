#pragma once

#include "body_tracking_sample.h"
#include "hand_tracking_sample.h"
#include "src/controller/controller.h"
#include "src/packet/nativepacket.h"

#include <array>
#include <optional>

namespace HOL
{
	// One normalized tracking snapshot. Sample pointers remain owned by the source and are consumed
	// synchronously before the next update.
	struct TrackingSourceFrame
	{
		// External sources use a monotonic nanosecond clock so existing blend/filter timing works.
		XrTime time = 0;
		std::optional<HOL::PoseLocation> hmdPose;
		std::array<const HOL::HandTrackingSample*, HOL::HandSide_MAX> hands{};
		const HOL::BodyTrackingSample* body = nullptr;
	};

	// Adapts non-OpenXR producers to the same OpenXR-shaped data consumed by the tracking pipeline.
	class TrackingSource
	{
	public:
		virtual ~TrackingSource() = default;

		// Full joint reconstruction may be deferred to skeletal ticks while palm poses stay current.
		virtual HOL::TrackingSourceFrame update(bool skeletalUpdate, bool applyBaseOffset) = 0;
		// Sources may preserve native pose metadata needed when the processed pose is sent back.
		virtual void applySourcePose(HOL::HandSide, HOL::HandTransformPayload&) const
		{
		}
		// Spatial gestures and synthetic body tracking require a common world-space reference.
		virtual bool hasTrackingReference() const = 0;
		// Drop provider-owned snapshots when its transport disconnects.
		virtual void reset() = 0;
	};
} // namespace HOL
