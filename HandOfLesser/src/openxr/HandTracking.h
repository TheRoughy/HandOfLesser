#pragma once

#include <d3d11.h> // Why do you need this??
#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include "openxr_hand.h"
#include "openxr_body.h"
#include <HandOfLesserCommon.h>
#include "src/hands/gesture/open_hand_pinch_gesture.h"
#include "src/hands/action/hand_drag_action.h"

#include "src/vrchat/vrchat_input.h"

#include "src/hands/gesture/combo_gesture.h"

namespace HOL::OpenXR
{
	class HandTracking
	{
	public:
		// Common processing and gestures exist even when no OpenXR instance is created.
		void init();
		// OpenXR tracker handles are initialized only for the direct OpenXR provider.
		void initOpenXR(xr::UniqueDynamicInstance& instance, xr::UniqueDynamicSession& session);
		void updateHands(
			XrSpace space,
			XrTime time,
			OpenXRBody& bodyTracker,
			bool skeletalUpdate,
			const std::array<const HOL::HandTrackingSample*, HOL::HandSide_MAX>& externalSamples
			= {});
		void updateInputs();
		HOL::HandTransformPayload getTransformPayload(HOL::HandSide side);
		HOL::HandPose& getHandPose(HOL::HandSide side);
		void updateSteamVRHandBaseline(const HOL::SteamVRHandBaselinePayload& payload);
		void updateSteamVRHandPose(const HOL::SteamVRHandPosePayload& payload);
		void updateSteamVRHmdPose(const HOL::SteamVRHmdPosePayload& payload);
		HOL::SteamVR::SteamVRTrackingSource& getSteamVRTrackingSource();
		void resetSteamVRTrackingSource();
		void drawHands();
		OpenXRHand* getHand(HOL::HandSide side);

		void rebuildActions();
		std::shared_ptr<BaseAction> getActionForBindingIndex(size_t bindingIndex) const;

	private:
		struct ActionSet
		{
			static constexpr size_t InputTargetCount
				= static_cast<size_t>(HOL::settings::InputTarget::InputTarget_MAX);

			std::vector<std::shared_ptr<BaseAction>> actions;
			std::vector<HOL::settings::GestureBinding> bindings;
			std::vector<std::shared_ptr<BaseAction>> actionsByBindingIndex;
			std::array<std::vector<size_t>, InputTargetCount> bindingIndicesByTarget;

			const std::vector<size_t>&
			getBindingIndicesForTarget(HOL::settings::InputTarget target) const
			{
				return this->bindingIndicesByTarget[static_cast<size_t>(target)];
			}
		};

		void initOpenXRHands(xr::UniqueDynamicSession& session);
		std::shared_ptr<const ActionSet>
		buildActionSet(const std::vector<HOL::settings::GestureBinding>& bindings) const;
		void submitLegacyFingerCurl();
		void updateSimpleGestures();
		void updateTriggerStabilizationState(const ActionSet& actionSet);
		float getTriggerStabilizationSmoothingMS(HOL::HandSide side,
												 std::chrono::steady_clock::time_point now) const;
		OpenXRHand mLeftHand;
		OpenXRHand mRightHand;
		HOL::SteamVR::SteamVRTrackingSource mSteamVRTrackingSource;

		std::atomic<std::shared_ptr<const ActionSet>> mConfiguredActionSet
			= std::make_shared<ActionSet>();
		std::atomic<std::shared_ptr<const ActionSet>> mSteamLinkNativeActionSet
			= std::make_shared<ActionSet>();
		std::array<bool, HOL::HandSide_MAX> mTriggerStabilizationHeld = {false, false};
		std::array<std::chrono::steady_clock::time_point, HOL::HandSide_MAX>
			mLastTriggerStabilizationTime = {};
	};
} // namespace HOL::OpenXR
