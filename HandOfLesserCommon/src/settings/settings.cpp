#include "settings.h"

namespace HOL::settings
{
	std::vector<GestureBinding> defaultGestureBindings()
	{
		std::vector<GestureBinding> bindings;
		auto addBinding = [&](GestureBinding binding)
		{
			binding.modifiers |= static_cast<uint32_t>(GestureModifier::InView);
			bindings.push_back(binding);
		};

		// Joystick — Both hands, Middle finger proximity → Joystick target
		for (int i = 0; i < HandSide::HandSide_MAX; i++)
		{
			GestureBinding b;
			b.side = (HOL::HandSide)i;
			b.kind = GestureKind::Proximity;
			b.proximityFinger = HOL::FingerMiddle;
			b.target = InputTarget::Joystick;
			// The left joystick is commonly dragged from outside the visible FoV.
			if (b.side == HOL::LeftHand)
			{
				bindings.push_back(b);
			}
			else
			{
				addBinding(b);
			}
		}

		// Trigger — Both hands, standard Index pinch
		for (int i = 0; i < HandSide::HandSide_MAX; i++)
		{
			GestureBinding b;
			b.side = (HOL::HandSide)i;
			b.kind = GestureKind::Proximity;
			b.proximityFinger = HOL::FingerIndex;
			b.target = InputTarget::Trigger;
			if (b.side == HOL::LeftHand)
			{
				b.facingModifiers
					= static_cast<uint32_t>(GestureModifier::LookingAtHand)
					  | static_cast<uint32_t>(GestureModifier::PalmFacingUser);
				b.facingConditionMode = FacingConditionMode::AllowIfNot;
			}
			addBinding(b);
		}

		// Trigger — Both hands, Index finger proximity with Closed Hand modifier
		for (int i = 0; i < HandSide::HandSide_MAX; i++)
		{
			GestureBinding b;
			b.side = (HOL::HandSide)i;
			b.kind = GestureKind::Proximity;
			b.proximityFinger = HOL::FingerIndex;
			b.modifiers = static_cast<uint32_t>(GestureModifier::ClosedHand);
			b.target = InputTarget::Trigger;
			addBinding(b);
		}

		// Grip — Both hands, 3-finger curl
		for (int i = 0; i < HandSide::HandSide_MAX; i++)
		{
			GestureBinding b;
			b.side = (HOL::HandSide)i;
			b.kind = GestureKind::Grip;
			b.target = InputTarget::Grip;
			addBinding(b);
		}

		// Left System — Index tap and hold, auto release
		{
			GestureBinding b;
			b.side = HOL::LeftHand;
			b.kind = GestureKind::Proximity;
			b.proximityFinger = HOL::FingerIndex;
			b.modifiers = static_cast<uint32_t>(GestureModifier::Hold);
			b.facingModifiers = static_cast<uint32_t>(GestureModifier::LookingAtHand)
							| static_cast<uint32_t>(GestureModifier::PalmFacingUser);
			b.target = InputTarget::System;
			b.pressAndRelease = true;
			addBinding(b);
		}

		// Left X — Ring tap and hold
		{
			GestureBinding b;
			b.side = HOL::LeftHand;
			b.kind = GestureKind::Proximity;
			b.proximityFinger = HOL::FingerRing;
			b.modifiers = static_cast<uint32_t>(GestureModifier::Hold);
			b.facingModifiers = static_cast<uint32_t>(GestureModifier::LookingAtHand);
			b.target = InputTarget::X;
			b.pressAndRelease = true;
			addBinding(b);
		}

		// Left Y — Pinky tap and hold
		{
			GestureBinding b;
			b.side = HOL::LeftHand;
			b.kind = GestureKind::Proximity;
			b.proximityFinger = HOL::FingerLittle;
			b.modifiers = static_cast<uint32_t>(GestureModifier::Hold);
			b.facingModifiers = static_cast<uint32_t>(GestureModifier::LookingAtHand);
			b.target = InputTarget::Y;
			b.pressAndRelease = true;
			addBinding(b);
		}

		// Right X — Ring tap
		{
			GestureBinding b;
			b.side = HOL::RightHand;
			b.kind = GestureKind::Proximity;
			b.proximityFinger = HOL::FingerRing;
			b.target = InputTarget::X;
			addBinding(b);
		}

		// Right Y — Pinky tap and hold
		{
			GestureBinding b;
			b.side = HOL::RightHand;
			b.kind = GestureKind::Proximity;
			b.proximityFinger = HOL::FingerLittle;
			b.modifiers = static_cast<uint32_t>(GestureModifier::Hold);
			b.facingModifiers = static_cast<uint32_t>(GestureModifier::LookingAtHand);
			b.target = InputTarget::Y;
			addBinding(b);
		}

		return bindings;
	}

	std::vector<GestureBinding> defaultSteamLinkHandGestureBindings()
	{
		// The native profile exposes a fixed set of hand-specific inputs. Keep these bindings
		// separate from the user-editable controller bindings used by the other profiles.
		std::vector<GestureBinding> bindings;
		const InputTarget pinchTargets[] = {
			InputTarget::SteamLinkIndexPinch,
			InputTarget::SteamLinkMiddlePinch,
			InputTarget::SteamLinkRingPinch,
			InputTarget::SteamLinkPinkyPinch,
		};

		for (int side = 0; side < HandSide::HandSide_MAX; side++)
		{
			for (int finger = FingerIndex; finger <= FingerLittle; finger++)
			{
				GestureBinding binding;
				binding.side = static_cast<HandSide>(side);
				binding.kind = GestureKind::Proximity;
				binding.proximityFinger = static_cast<FingerType>(finger);
				binding.target = pinchTargets[finger - FingerIndex];
				if (binding.side == LeftHand && binding.proximityFinger == FingerIndex)
				{
					binding.modifiers = static_cast<uint32_t>(GestureModifier::InView);
					binding.facingModifiers
						= static_cast<uint32_t>(GestureModifier::LookingAtHand)
						  | static_cast<uint32_t>(GestureModifier::PalmFacingUser);
					binding.facingConditionMode = FacingConditionMode::AllowIfNot;
				}
				bindings.push_back(binding);
			}

			GestureBinding grip;
			grip.side = static_cast<HandSide>(side);
			grip.kind = GestureKind::Grip;
			grip.target = InputTarget::Grip;
			bindings.push_back(grip);

			GestureBinding point;
			point.side = static_cast<HandSide>(side);
			point.kind = GestureKind::IndexPoint;
			point.target = InputTarget::SteamLinkIndexPoint;
			bindings.push_back(point);
		}

		// Steam Link exposes the system input on the left hand only.
		GestureBinding system;
		system.side = LeftHand;
		system.kind = GestureKind::Proximity;
		system.proximityFinger = FingerIndex;
		system.modifiers = static_cast<uint32_t>(GestureModifier::Hold)
						   | static_cast<uint32_t>(GestureModifier::InView);
		system.facingModifiers = static_cast<uint32_t>(GestureModifier::LookingAtHand)
							 | static_cast<uint32_t>(GestureModifier::PalmFacingUser);
		system.target = InputTarget::System;
		system.pressAndRelease = true;
		bindings.push_back(system);

		return bindings;
	}
} // namespace HOL::settings
