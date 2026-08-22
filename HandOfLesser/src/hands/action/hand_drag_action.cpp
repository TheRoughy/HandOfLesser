#include "hand_drag_action.h"

namespace HOL
{
	std::shared_ptr<HandDragAction>
	HandDragAction::setup(HandSide side, XrHandJointEXT joint, float sensitivity)
	{
		this->mHandSide = side;
		this->mTargetJoint = joint;
		this->mSensitivity = sensitivity;
		return shared_from_this();
	}

	void HandDragAction::onEvaluate(GestureData gestureData, ActionData actionData)
	{
		if (actionData.onDown || actionData.isDown)
		{
			Eigen::Vector3f currentPos = OpenXR::toEigenVector(
				gestureData.joints[this->mHandSide][this->mTargetJoint].pose.position);

			if (actionData.onDown)
			{
				this->mStartPosition = currentPos;
			}

			if (actionData.isDown)
			{
				Eigen::Vector3f diff = currentPos - this->mStartPosition;

				// Prefer body tracking for the reference frame. Fall back to hand orientation
				// when body tracking is unavailable.
				const Eigen::Quaternionf& refQuat = gestureData.ReferenceOrientation;
				if (gestureData.ReferenceOrientationValid)
				{
					// Body tracking available - use chest or head orientation as reference.
					// Zero Y to lock the locomotion plane to horizontal, avoiding pitch/roll
					// affecting locomotion direction.
					Eigen::Vector3f forward = refQuat * Eigen::Vector3f::UnitY();
					forward.y() = 0.0f;
					if (forward.squaredNorm() > 1e-6f)
					{
						forward.normalize();
					}
					else
					{
						forward = Eigen::Vector3f(0, 0, -1);
					}
					Eigen::Vector3f right = forward.cross(Eigen::Vector3f::UnitY()).normalized();

					// Project diff onto reference axes
					float forwardAmount = diff.dot(forward);
					float rightAmount = diff.dot(right);

					forwardAmount *= BaseMultiplier * mSensitivity;
					rightAmount *= BaseMultiplier * mSensitivity;

					forwardAmount = std::clamp(forwardAmount, -1.0f, 1.0f);
					rightAmount = std::clamp(rightAmount, -1.0f, 1.0f);

					this->submitInput(InputType::Touch, true);
					this->submitInput(InputType::XAxis, rightAmount);
					this->submitInput(InputType::ZAxis, forwardAmount);
				}
				else
				{
					// Body tracking unavailable - fall back to hand orientation as reference.
					Eigen::Quaternionf currentOrientation
						= OpenXR::toEigenQuaternion(
							gestureData.joints[this->mHandSide][this->mTargetJoint].pose.orientation);

					diff = currentOrientation.inverse() * diff;

					// Rotate so forward aligns with the expected joystick axis
					diff = HOL::quaternionFromEulerAnglesDegrees(0, -35, 0) * diff;

					diff.z() = -diff.z();
					diff *= BaseMultiplier * mSensitivity;
					diff.x() = std::clamp(diff.x(), -1.0f, 1.0f);
					diff.z() = std::clamp(diff.z(), -1.0f, 1.0f);

					this->submitInput(InputType::Touch, true);
					this->submitInput(InputType::XAxis, diff.x());
					this->submitInput(InputType::ZAxis, diff.z());
				}
			}
		}
		else if (actionData.onUp)
		{
			this->submitInput(InputType::Touch, false);
			this->submitInput(InputType::XAxis, 0);
			this->submitInput(InputType::ZAxis, 0);
		}
	}
} // namespace HOL
