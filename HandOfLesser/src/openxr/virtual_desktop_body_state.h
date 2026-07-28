// MIT License
//
// Copyright(c) 2022-2024 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <d3d11.h>
#include <openxr/openxr.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HOL::VirtualDesktop
{
	// Shared-memory ABI exposed by Virtual Desktop. Keep every field, including the currently
	// unused face and eye data, because their layout determines the offsets of hand and body data.
	struct Vector3
	{
		float x;
		float y;
		float z;
	};

	struct Quaternion
	{
		float x;
		float y;
		float z;
		float w;
	};

	struct Pose
	{
		Quaternion orientation;
		Vector3 position;
	};

	struct HandTrackingAimState
	{
		uint64_t aimStatus;
		Pose aimPose;
		float pinchStrengthIndex;
		float pinchStrengthMiddle;
		float pinchStrengthRing;
		float pinchStrengthLittle;
	};

	struct FingerJointState
	{
		Pose pose;
		float radius;
		Vector3 angularVelocity;
		Vector3 linearVelocity;
	};

	struct BodyJointLocation
	{
		uint64_t locationFlags;
		Pose pose;
	};

	struct SkeletonJoint
	{
		int32_t joint;
		int32_t parentJoint;
		Pose pose;
	};

	static constexpr size_t ExpressionCount = 70;
	static constexpr size_t ConfidenceCount = 2;
	static constexpr size_t HandJointCount = XR_HAND_JOINT_COUNT_EXT;
	static constexpr size_t FullBodyJointCount = XR_FULL_BODY_JOINT_COUNT_META;

	struct BodyStateV2
	{
		uint8_t faceIsValid;
		uint8_t isEyeFollowingBlendshapesValid;
		float expressionWeights[ExpressionCount];
		float expressionConfidences[ConfidenceCount];

		uint8_t leftEyeIsValid;
		uint8_t rightEyeIsValid;
		Pose leftEyePose;
		Pose rightEyePose;
		float leftEyeConfidence;
		float rightEyeConfidence;

		uint8_t leftHandActive;
		uint8_t rightHandActive;
		FingerJointState leftHandJointStates[HandJointCount];
		FingerJointState rightHandJointStates[HandJointCount];

		HandTrackingAimState leftAimState;
		HandTrackingAimState rightAimState;

		uint8_t bodyTrackingCalibrated;
		uint8_t bodyTrackingHighFidelity;
		float bodyTrackingConfidence;
		BodyJointLocation bodyJoints[FullBodyJointCount];
		SkeletonJoint skeletonJoints[FullBodyJointCount];
		int32_t skeletonChangedCount;
	};

	static_assert(std::is_trivially_copyable_v<BodyStateV2>);
	static_assert(sizeof(FingerJointState) == 56);
	static_assert(sizeof(HandTrackingAimState) == 56);
	static_assert(sizeof(BodyJointLocation) == 40);
	static_assert(offsetof(BodyStateV2, leftHandJointStates) == 364);
	static_assert(offsetof(BodyStateV2, bodyJoints) == 3400);
	static_assert(sizeof(BodyStateV2) == 0x2640);
} // namespace HOL::VirtualDesktop
