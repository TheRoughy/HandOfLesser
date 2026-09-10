#include "directional_pinch_gesture.h"

namespace HOL::Gesture::DirectionalPinchGesture
{
	void Gesture::setup()
	{
		this->mProximityGesture = ProximityGesture::Create();
		this->mProximityGesture->setup(this->parameters.pinchFinger,
									  this->parameters.side,
									  FingerThumb,
									  this->parameters.side,
									  this->parameters.pinchDistance);

		this->mArmingDirectionGesture = AboveBelowCurlPlaneGesture::Gesture::Create();
		this->mArmingDirectionGesture->name = "ThumbBelowPinchPlane";
		this->mArmingDirectionGesture->parameters.planeFinger = this->parameters.pinchFinger;
		this->mArmingDirectionGesture->parameters.otherFinger = FingerThumb;
		this->mArmingDirectionGesture->parameters.side = this->parameters.side;
		this->mArmingDirectionGesture->parameters.planeSide
			= AboveBelowCurlPlaneGesture::PlaneSide::Below;
		this->mArmingDirectionGesture->parameters.minimumDistanceFromPlane
			= this->parameters.directionThreshold;

		this->mDisarmingDirectionGesture = AboveBelowCurlPlaneGesture::Gesture::Create();
		this->mDisarmingDirectionGesture->name = "ThumbAbovePinchPlane";
		this->mDisarmingDirectionGesture->parameters.planeFinger = this->parameters.pinchFinger;
		this->mDisarmingDirectionGesture->parameters.otherFinger = FingerThumb;
		this->mDisarmingDirectionGesture->parameters.side = this->parameters.side;
		this->mDisarmingDirectionGesture->parameters.planeSide
			= AboveBelowCurlPlaneGesture::PlaneSide::Above;
		this->mDisarmingDirectionGesture->parameters.minimumDistanceFromPlane
			= this->parameters.directionThreshold;

		this->mSubGestures.clear();
		this->mSubGestures.push_back(this->mProximityGesture);
		this->mSubGestures.push_back(this->mArmingDirectionGesture);
		this->mSubGestures.push_back(this->mDisarmingDirectionGesture);
	}

	float Gesture::evaluateInternal(GestureData data)
	{
		if (!this->mProximityGesture || !this->mArmingDirectionGesture
			|| !this->mDisarmingDirectionGesture)
		{
			return 0.0f;
		}

		float proximity = this->mProximityGesture->evaluate(data);
		bool thumbBelowPlane = this->mArmingDirectionGesture->evaluate(data) >= 1.0f;
		bool thumbAbovePlane = this->mDisarmingDirectionGesture->evaluate(data) >= 1.0f;

		if (proximity < 1.0f)
		{
			// Crossing too far through the finger plane cancels an old approach. The neutral
			// band between the two thresholds preserves the armed state while forming a pinch.
			if (thumbAbovePlane)
			{
				this->mArmed = false;
			}
			else if (thumbBelowPlane)
			{
				this->mArmed = true;
			}
		}

		return this->mArmed ? proximity : 0.0f;
	}
} // namespace HOL::Gesture::DirectionalPinchGesture
