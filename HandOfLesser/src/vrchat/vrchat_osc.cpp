#include "vrchat_osc.h"
#include "src/core/settings_global.h"
#include "src/core/ui/display_global.h"
#include <oscpp/client.hpp>
#include "src/util/hol_utils.h"

using namespace std::chrono_literals;

namespace HOL::VRChat
{
	float VRChatOSC::HUMAN_RIG_RANGE[SINGLE_HAND_JOINT_COUNT];
	float VRChatOSC::HUMAN_RIG_CENTER[SINGLE_HAND_JOINT_COUNT];

	std::string VRChatOSC::OSC_PARAMETER_NAMES_FULL[VRChat::BOTH_HAND_JOINT_COUNT];
	std::string VRChatOSC::OSC_PARAMETER_NAMES_ALTERNATING[VRChat::SINGLE_HAND_JOINT_COUNT];
	std::string VRChatOSC::OSC_PARAMETER_NAMES_PACKED[VRChat::SINGLE_HAND_JOINT_COUNT];

	VRChatOSC::VRChatOSC()
	{
		this->mNextNextTransmitSide = HandSide::LeftHand;
		initParameters();
		initParameterNames();
	}

	void HOL::VRChat::VRChatOSC::initParameters()
	{
		// clang-format off
	// 
	// Unity's humanoid rig expects value in the range 0 - 1 
	// This range represents the "normal" range of motion of the given joint.
	// In order to translate our raw rotation into this range, we need:
	// A: The range of motion ( e.g. around 90 degrees for finger curls )
	// B: The orientation that corresponds to the center of this range
	// The former we can pull directly from unity, the latter requires eyeballing and adjustment.

	// These values are pulled from unity and should not need to be modified.
	setHumanRigRange(FingerType::FingerIndex,	100, 90, 90, 40);
	setHumanRigRange(FingerType::FingerMiddle,	100, 90, 90, 15);
	setHumanRigRange(FingerType::FingerRing,	100, 90, 90, 15);
	setHumanRigRange(FingerType::FingerLittle,	100, 90, 90, 40);

	setHumanRigRange(FingerType::FingerThumb,	40, 75, 75, 50);

		// clang-format on
	}

	void HOL::VRChat::VRChatOSC::initParameterNames()
	{
		// Initialize all the osc parmater names so we don't have to generate them for every packet
		// The paramNames must match what we're using on the unity side, which is mostly based on
		// what the humanoid rig uses, but we lower-case everything but the HOL/ prefix.

		std::string sideParamNames[2];
		std::string fingerParamName[5];
		std::string bendParamName[4];

		std::string prefix = VRChat::OSC_PREFIX + VRChat::NAMESPACE_PREFIX;

		sideParamNames[HandSide::LeftHand] = "lefthand_";
		sideParamNames[HandSide::RightHand] = "righthand_";

		fingerParamName[FingerType::FingerIndex] = "index_";
		fingerParamName[FingerType::FingerMiddle] = "middle_";
		fingerParamName[FingerType::FingerRing] = "ring_";
		fingerParamName[FingerType::FingerLittle] = "little_";
		fingerParamName[FingerType::FingerThumb] = "thumb_";

		bendParamName[FingerBendType::CurlFirst] = "1_curl";
		bendParamName[FingerBendType::CurlSecond] = "2_curl";
		bendParamName[FingerBendType::CurlThird] = "3_curl";
		bendParamName[FingerBendType::Splay] = "splay";

		// Full
		for (int side = 0; side < HandSide_MAX; side++)
		{
			for (int i = 0; i < FingerType::FingerType_MAX; i++)
			{
				for (int j = 0; j < FingerBendType::FingerBendType_MAX; j++)
				{
					// Note that this includes the side as well
					int index = getParameterIndex((HandSide)side, (FingerType)i, (FingerBendType)j);
					VRChatOSC::OSC_PARAMETER_NAMES_FULL[index]
						= prefix + VRChat::OSC_FULL_PREFIX + sideParamNames[side]
						  + fingerParamName[i] + bendParamName[j];
				}
			}
		}

		// Alternating and packed
		for (int i = 0; i < FingerType::FingerType_MAX; i++)
		{
			for (int j = 0; j < FingerBendType::FingerBendType_MAX; j++)
			{
				// Note we are not including a side
				int index = getParameterIndex((FingerType)i, (FingerBendType)j);
				VRChatOSC::OSC_PARAMETER_NAMES_ALTERNATING[index]
					= prefix + VRChat::OSC_ALTERNATING_PREFIX + fingerParamName[i]
					  + bendParamName[j];
				VRChatOSC::OSC_PARAMETER_NAMES_PACKED[index]
					= prefix + VRChat::OSC_PACKED_PREFIX + fingerParamName[i] + bendParamName[j];
			}
		}
	}

	void HOL::VRChat::VRChatOSC::setHumanRigRange(HOL::FingerType finger,
												  float first,
												  float second,
												  float third,
												  float splay)
	{
		// Indieces are in sequence
		int index = VRChatOSC::getParameterIndex(finger, HOL::FingerBendType::CurlFirst);

		VRChatOSC::HUMAN_RIG_RANGE[index] = HOL::degreesToRadians(first);
		VRChatOSC::HUMAN_RIG_RANGE[index + 1] = HOL::degreesToRadians(second);
		VRChatOSC::HUMAN_RIG_RANGE[index + 2] = HOL::degreesToRadians(third);
		VRChatOSC::HUMAN_RIG_RANGE[index + 3] = HOL::degreesToRadians(splay);
	}

	int HOL::VRChat::VRChatOSC::getParameterIndex(HOL::FingerType finger, HOL::FingerBendType joint)
	{
		// There are 2 total parameters, 3 splay and 1 curl for each finger
		// joint denotes the curl as 1,2,3, with 4 being used for splay
		// All these values are internal and arbitrary
		return (finger * 4) + joint;
	}

	int HOL::VRChat::VRChatOSC::getParameterIndex(HOL::HandSide side,
												  HOL::FingerType finger,
												  HOL::FingerBendType joint)
	{
		// See the other getParameterIndex() function.
		// Basically add SINGLE_HAND_JOINT_COUNT if right hand
		return (finger * 4) + joint + (SINGLE_HAND_JOINT_COUNT * side);
	}

	std::pair<float,MotionRange> VRChatOSC::getRangeParameters(HOL::HandSide side,
										HOL::FingerType finger,
										HOL::FingerBendType joint)
	{

		float center;
		// Hardcode for now, need to match stuff generated in unity
		MotionRange jointRange;

		// Complicated humanoid rig stuff, and simple skeletal stuff
		if (Config.vrchat.useUnityHumanoidSplay)
		{
			int index = getParameterIndex(finger, joint);

			// Unity humanoid rig curl goes from -1 closed to 1 open.
			// The rotation between these two values is listed in VRChatOSC::HUMAN_RIG_RANGE.
			// We need to rotate a bit more than this in some cases, so we can extend the range.
			// The values used must be mirrored by the animations created in unity.

			float center;
			if (joint == HOL::FingerBendType::Splay)
			{
				// Splay is always inwards towards the center of the hand,
				// so it will be different for left/right hands. -1 in, 1 out.
				// Thumb is -1: downwards ( towards palm ), 1: upwards.
				center = Config.fingerBend.fingerSplayCenter[finger];
				jointRange = Config.fingerBend.fingersplayRange[finger];
			}
			else
			{
				if (finger == FingerType::FingerThumb)
				{
					center = Config.fingerBend.thumbCurlCenter[joint];
					jointRange = Config.fingerBend.thumbCurlRange[joint];
				}
				else
				{
					center = Config.fingerBend.commonCurlCenter[joint];
					jointRange = Config.fingerBend.commonCurlRange[joint];
				}
			}

			// Stock range being the rotation between -1 and 1
			float halfStockRange = VRChatOSC::HUMAN_RIG_RANGE[index] * 0.5f;

			// if both raw ranges are the same, this will span from -range to +range
			// values are in radians.
			float startRange = halfStockRange * jointRange.start;
			float endRange = halfStockRange * jointRange.end;

			return std::make_pair(center, MotionRange(startRange, endRange));
		}
		else
		{
			center = 0; // Not configurable atm
			if (joint == HOL::FingerBendType::Splay)
			{
				// Flip and invert if right
				center = Config.skeletalBend.fingerSplayCenter[finger];
				jointRange = Config.skeletalBend.fingersplayRange[finger];

				if (side == HandSide::RightHand)
				{
					center = -center;
					std::swap(jointRange.start, jointRange.end);
					jointRange.start = -jointRange.start;
					jointRange.end = -jointRange.end;
				}
			}
			else
			{
				if (finger == FingerType::FingerThumb)
				{
					center = Config.skeletalBend.thumbCurlCenter[joint];
					jointRange = Config.skeletalBend.thumbCurlRange[joint];
				}
				else
				{
					center = Config.skeletalBend.commonCurlCenter[joint];
					jointRange = Config.skeletalBend.commonCurlRange[joint];
				}
			}

			// For actual bone rotation, unity is opposite to openxr, so flip them here.
			return std::make_pair(center,
					MotionRange(HOL::degreesToRadians(-jointRange.start),
								HOL::degreesToRadians(-jointRange.end)));
		}

		
	}




	// Individual joint on one hand
	float HOL::VRChat::VRChatOSC::computeParameterValue(float rawValue,
														HOL::HandSide side,
														HOL::FingerType finger,
														HOL::FingerBendType joint)
	{
		// Unity humanoid rig curl goes from -1 closed to 1 open.
		// The rotation between these two values is listed in VRChatOSC::HUMAN_RIG_RANGE.
		// We need to rotate a bit more than this in some cases, so we can extend the range.
		// The values used must be mirrored by the animations created in unity.

		if (joint == HOL::FingerBendType::Splay)
		{
			// Splay is always inwards towards the center of the hand,
			// so it will be different for left/right hands. -1 in, 1 out.
			// Thumb is -1: downwards ( towards palm ), 1: upwards.

			if (Config.vrchat.useUnityHumanoidSplay)
			{
				// Raw values are provided, so we do the conversion for unity here.
				// I /think/ all we need to do is flip the input
				if (side == HandSide::LeftHand)
				{
					// If so, index and middle flipped.
					// Thumb splay moves in the opposite direction, so flip on right hand.
					switch (finger)
					{
						case HOL::FingerIndex:
						case HOL::FingerMiddle: {
							rawValue *= -1.f;
							break;
						}
						default:
							break;
					}
				}
				else
				{
					// And opposite for right hand
					switch (finger)
					{
						case HOL::FingerLittle:
						case HOL::FingerRing:
						case HOL::FingerThumb: {
							rawValue *= -1.f;
							break;
						}
						default:
							break;
					}
				}
			}
		}

		auto [center, jointRange] = getRangeParameters(side, finger, joint);


		// For whatever reason these offsets all want to be negative.
		// Since they don't really mean anything to anyone, invert them
		// so the users doesn't need to config negative values.
		center = -center;

		// Any user inputs, such as settings, will be in degrees rather than radians
		center = HOL::degreesToRadians(center);
		// I guess we can just add the center as an offset and it'll work
		// Makes the math a lot simpler
		rawValue += center; 

		// Ratio of RawValue between rangeStart and rangeEnd
		float outputCurl = (rawValue - jointRange.start) / (jointRange.end - jointRange.start);
		// clamp between 0 and 1
		outputCurl = std::clamp(outputCurl, 0.f, 1.f);

		
		if (!Config.vrchat.useUnityHumanoidSplay)
		{
			if (joint != HOL::FingerBendType::Splay)
			{
				// Values end up needing to be inverted. Could adjust the vrchat side too
				// TODO: deal with this
				outputCurl = 1.f - outputCurl;
			}
		}

		// scale our 0-1 -> -1 to 1
		return (outputCurl * 2.f) - 1.f;
	}



	int HOL::VRChat::VRChatOSC::encodePacked(float left, float right)
	{
		// This was originally written with 0-1 values in mind, but now we're reusing
		// the value from the non-packed OSC params, so scale from -1/1 to 0/1
		left = (left + 1.f) * 0.5f;
		right = (right + 1.f) * 0.5f;

		// Each joint has 16 steps. There are 256 animations.
		// In the first 16, the left joint is at 0, and the right cycles through its 16 steps.
		// Set the left joint to 1 and repeat for the next 16. Repeat.
		// So the value of the left joint will be 0-1 scaled to 0-15, and multiplied by 16.
		// Right will be 0-1 scaled to 0-15, added to the value we calculated for left.

		// 0-1 to 0-15, multiply by 16 to get the first entry of the group of 16 where left is in
		// the correct position, 0, 16, 32, 48, 64 ... 240
		float leftEncoded = roundf((left * 15.f)) * 16.f;

		// the same, except do not multiply by 16 because this is the index inside the group of 16
		float rightEncoded = roundf((right * 15.f));

		// Adding the two together, we get two separate 0-15 values encoded in a single 0-255 value.
		// Left will be in the same position for values 0 to 15, 16 to 31, and so on.
		float packed = leftEncoded + rightEncoded;
		
		// Just make damn sure it doesn't round down.
		return round(packed); 
	}

	float HOL::VRChat::VRChatOSC::handleInterlacing(float newValue, float oldValue)
	{
		// We can double the precision from 16 to 32 steps by 
		// alternating between two values when, then having the
		// vrchat side use the average of the two latest values.
		// First decide what the nearest two steps are,
		// then decide if we are within 50% of the center of these two steps.
		// If we are, clamp to the farthest of the two steps.
		// The value we choose will depend on what we chose last, 
		// so store that somewhere.

		// For the sake of my sanity, scale from -1/1 to 0/1 again
		// Also multiply by 15, so we get a range of 0/15 with no rounding
		newValue = ((newValue + 1.f) * 0.5f) * 15.f;
		oldValue = ((oldValue + 1.f) * 0.5f) * 15.f;
		
		// fmod is basically modulo for floats, remainder.
		float remainder = fmod(newValue,1);

		// if within 0.25f of the center of two steps
		if ( abs(0.5f - remainder) <= 0.25f )
		{
			// At this point we don't care what the actual value is, 
			// only that it needs to be between the two steps.
			// Floor the value to get the lower step, and we know +1 
			// will be the upper step.
			newValue = floorf(newValue);

			// Find the step that is the most distant from the previous 
			// value. This ensure that we will always be alternating, and 
			// using the most distant value of the two when moving further.
			float distanceLower = abs(newValue - oldValue);
			float distanceUpper = abs((newValue + 1) - oldValue );

			// However, if we are traveling more than 1 step, doing this will result in an initally
			// overshoot, with looks bad. If the Distance is greather than 1, reverse this.
			float rawDistance = abs(newValue - oldValue);
			if (rawDistance > 1.0f) 
			{
				// just reverse the values and everything will work out
				std::swap(distanceLower, distanceUpper);
			}

			if (distanceLower < distanceUpper)
			{
				// Value is already at lower, so if upper is further away return it instead.
				newValue += 1;	
			}

			

		}
		// If false we don't need to do anything,
		// scale it back to -1/1 and return.
		return ((newValue / 15.f) * 2.f) - 1.f;


	}

	void HOL::VRChat::VRChatOSC::generateOscOutputPacked()
	{
		for (int i = 0; i < FingerType::FingerType_MAX; i++)
		{
			for (int j = 0; j < FingerBendType_MAX; j++)
			{
				int leftSideIndex
					= getParameterIndex(HandSide::LeftHand, (FingerType)i, (FingerBendType)j);
				int rightSideIndex
					= getParameterIndex(HandSide::RightHand, (FingerType)i, (FingerBendType)j);

				// Used max distance values so we don't quick short movements
				float leftBend = this->mOscMaxSinceLastPacked[leftSideIndex];
				float rightBend = this->mOscMaxSinceLastPacked[rightSideIndex];

				// But values may be bogus if we haven't written anything
				// so write latest output if no update since last packed.
				if (!mPackedMaxDistanceUpdated[leftSideIndex])
				{
					leftBend = this->mOscOutput[leftSideIndex];
				}
				if (!mPackedMaxDistanceUpdated[rightSideIndex])
				{
					rightBend = this->mOscOutput[rightSideIndex];
				}
				// then always reset the bool
				mPackedMaxDistanceUpdated[leftSideIndex] = false;
				mPackedMaxDistanceUpdated[rightSideIndex] = false;

				// Store this as the last used value
				this->mOscOutputPrevForPacked[leftSideIndex] = leftBend;
				this->mOscOutputPrevForPacked[rightSideIndex] = rightBend;
				// Also reset the max to the same value
				this->mOscMaxSinceLastPacked[leftSideIndex] = leftBend;
				this->mOscMaxSinceLastPacked[rightSideIndex] = rightBend;


				// Handle interlacing
				if (HOL::Config.vrchat.interlacePacked)
				{
					leftBend
						= handleInterlacing(leftBend, mOscOutputInterlacedForClamp[leftSideIndex]);
					rightBend = handleInterlacing(rightBend,
												  mOscOutputInterlacedForClamp[rightSideIndex]);
					mOscOutputInterlacedForClamp[leftSideIndex] = leftBend;
					mOscOutputInterlacedForClamp[rightSideIndex] = rightBend;
				}

				int index = getParameterIndex((FingerType)i, (FingerBendType)j);
				int packed = encodePacked(leftBend, rightBend);
				this->mOscOutputPacked[index] = packed;

				// 0-255 values in left hand slot, -1 to +1 values in right hand slot
				// We're recreating the 0-255 values from the -1 to +1 for display purposes
				HOL::display::FingerTracking[HandSide::LeftHand].packedBend[i].bend[j] = packed;
			}
		}
	}

	bool VRChatOSC::shouldSendPacked()
	{
		// VRChat sends osc updates at 100ms intervals,
		// so carefully time our updates to match.
		// Doesn't need to be perfect, it's a pretty wide window.
		if (HOL::timeSince(mLastPackedSendTime) >= std::chrono::milliseconds(Config.vrchat.packedUpdateInterval))
		{
			mLastPackedSendTime = std::chrono::steady_clock::now();

			return true;
		}

		return false;
	}

	void HOL::VRChat::VRChatOSC::generateOscOutputFull(HOL::HandPose& leftHand,
													   HOL::HandPose& rightHand)
	{
		for (int side = 0; side < HandSide::HandSide_MAX; side++)
		{
			HandPose& hand = (side == HandSide::LeftHand) ? leftHand : rightHand;

			for (int i = 0; i < FingerType::FingerType_MAX; i++)
			{
				FingerBend& finger = hand.fingers[i];

				for (int j = 0; j < FingerBendType_MAX; j++)
				{
					float bend = computeParameterValue(
						finger.bend[j], (HandSide)side, (FingerType)i, (FingerBendType)j);

					HOL::display::FingerTracking[side].humanoidBend[i].bend[j] = bend;

					int index = getParameterIndex((HandSide)side, (FingerType)i, (FingerBendType)j);
					this->mOscOutput[index] = bend;
				}
			}
		}
	}

	// Make everything the same value and spit that out
	// for when we don't want to launch VR
	void VRChatOSC::generateOscTestOutput()
	{
		float curlValue = Config.vrchat.curlDebug;

		// This will only work when interlacing is enabled for now
		// still need to add separate logic for 100ms updates.
		if (Config.vrchat.alternateCurlTest)
		{
			if (this->mNextNextTransmitSide == HOL::LeftHand)
			{
				curlValue = -1;
			}
			else
			{
				curlValue = 1;
			}
		}

		// Fill full with whatever
		for (int side = 0; side < HandSide::HandSide_MAX; side++)
		{
			for (int i = 0; i < FingerType::FingerType_MAX; i++)
			{
				for (int j = 0; j < FingerBendType_MAX; j++)
				{
					// Configurable in UI
					float bend = curlValue;
					if (j == FingerBendType::Splay)
					{
						bend = Config.vrchat.splayDebug; // Eh
					}

					HOL::display::FingerTracking[side].humanoidBend[i].bend[j] = bend;

					int index = getParameterIndex((HandSide)side, (FingerType)i, (FingerBendType)j);
					this->mOscOutput[index] = bend;
				}
			}
		}
	}

	// includes a hand_side param denoting which hand the data is for
	size_t HOL::VRChat::VRChatOSC::generateOscBundleAlternating()
	{
		HOL::HandSide side = this->swapTransmitSide();

		// 0 or 40 depending on left or right side
		// Use to offset i
		int sideIndexOffset = VRChat::SINGLE_HAND_JOINT_COUNT * side;

		// Size param is presumably buffer size
		OSCPP::Client::Packet packet(this->mOscPacketBuffer, OSC_PACKET_BUFFER_SIZE);

		packet.openBundle(0);

		for (int i = 0; i < SINGLE_HAND_JOINT_COUNT; i++)
		{
			packet.openMessage(VRChatOSC::OSC_PARAMETER_NAMES_ALTERNATING[i].c_str(), 1)
				.float32(this->mOscOutput[i + sideIndexOffset])
				.closeMessage();
		}

		packet.openMessage(OSC_ALTERNATING_HAND_SIDE_PARAMETER.c_str(), 1)
			.int32(side)
			.closeMessage();

		packet.openMessage(OSC_USE_FULL_PARAMETER.c_str(), 1)
			.int32(Config.vrchat.shouldUseFullData() ? 1 : 0)
			.closeMessage();

		packet.closeBundle();

		return packet.size();
	}

	size_t HOL::VRChat::VRChatOSC::generateOscBundlePacked()
	{
		// Size param is presumably buffer size
		OSCPP::Client::Packet packet(this->mOscPacketBuffer, OSC_PACKET_BUFFER_SIZE);

		packet.openBundle(0);

		for (int i = 0; i < SINGLE_HAND_JOINT_COUNT; i++)
		{
			packet.openMessage(VRChatOSC::OSC_PARAMETER_NAMES_PACKED[i].c_str(), 1)
				.int32(this->mOscOutputPacked[i])
				.closeMessage();
		}

		if (HOL::Config.vrchat.interlacePacked)
		{
			// Just need to alternate between 0 and 1, so reuse transmit side logic
			int flipFlop = this->swapTransmitSide();

			packet.openMessage(OSC_PACKED_INTERLACE_BIT.c_str(), 1)
				.int32(flipFlop)
				.closeMessage();
		}

		packet.openMessage(OSC_USE_FULL_PARAMETER.c_str(), 1)
			.int32(Config.vrchat.shouldUseFullData() ? 1 : 0)
			.closeMessage();

		packet.closeBundle();

		return packet.size();
	}

	void VRChatOSC::handleInbetweenPacked()
	{
		// Full values have been updated, but we don't necessarily be sending a packed
		// OSC packet yet, since that only happens every 100 frames.
		// However, they may be very quick movements that we'll miss if we do this.
		// Every frame check the current bend value and store the most distant one.
		// Even if it ends up being a bit delayed, it's better than missing the movement entirely.
		for (int i = 0; i < BOTH_HAND_JOINT_COUNT; i++)
		{
			float newDistance = abs(this->mOscOutput[i] - this->mOscOutputPrevForPacked[i]);
			float prevDistance
				= abs(this->mOscMaxSinceLastPacked[i] - this->mOscOutputPrevForPacked[i]);
			if (newDistance > prevDistance)
			{
				// New value is farther from value we used for previous packed packet than previous one.
				// Replace it.
				mPackedMaxDistanceUpdated[i] = true;
				mOscMaxSinceLastPacked[i] = this->mOscOutput[i];
			}
		}
	}

	size_t HOL::VRChat::VRChatOSC::generateOscBundleFull()
	{
		// Size param is presumably buffer size
		OSCPP::Client::Packet packet(this->mOscPacketBuffer, OSC_PACKET_BUFFER_SIZE);

		packet.openBundle(0);

		for (int i = 0; i < BOTH_HAND_JOINT_COUNT; i++)
		{
			packet.openMessage(VRChatOSC::OSC_PARAMETER_NAMES_FULL[i].c_str(), 1)
				.float32(this->mOscOutput[i])
				.closeMessage();
		}

		packet.openMessage(OSC_USE_FULL_PARAMETER.c_str(), 1)
			.int32(Config.vrchat.shouldUseFullData() ? 1 : 0)
			.closeMessage();

		packet.closeBundle();

		return packet.size();
	}

	char* HOL::VRChat::VRChatOSC::getPacketBuffer()
	{
		return this->mOscPacketBuffer;
	}

	HOL::HandSide HOL::VRChat::VRChatOSC::swapTransmitSide()
	{
		// Swap side, return new side.
		this->mNextNextTransmitSide = this->mNextNextTransmitSide == HandSide::LeftHand
										  ? HandSide::RightHand
										  : HandSide::LeftHand;

		return this->mNextNextTransmitSide;
	}

} // namespace HOL::VRChat
