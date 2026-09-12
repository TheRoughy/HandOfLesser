#include "visualizer.h"
#include "visualizer_json.h"

#include "imgui.h"
#include <HandOfLesserCommon.h>
#include <openxr/openxr_reflection.h>
#include "src/core/app_paths.h"
#include "src/core/settings_global.h"
#include "src/core/state_global.h"
#include "src/core/ui/display_global.h"
#include <src/core/HandOfLesserCore.h>
#include "src/openxr/HandTracking.h"
#include "src/openxr/body_tracking.h"
#include "src/openxr/XrUtils.h"
#include "src/openxr/xr_joint_utils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>

namespace HOL
{
	namespace
	{
		constexpr int SnapshotFileVersion = 2;
		constexpr float HoverRadius = 9.0f;

		const char* providerName(HOL::state::TrackingProvider provider)
		{
			switch (provider)
			{
				case HOL::state::TrackingProvider::OpenXR:
					return "OpenXR";
				case HOL::state::TrackingProvider::SteamVRDriver:
					return "SteamVR driver";
				case HOL::state::TrackingProvider::VirtualDesktopSharedMemory:
					return "VD shared memory";
			}
			return "Unknown";
		}

		const char* handJointName(int joint)
		{
			switch (joint)
			{
#define HOL_HAND_JOINT_CASE(name, value) case value: return #name;
				XR_LIST_ENUM_XrHandJointEXT(HOL_HAND_JOINT_CASE)
#undef HOL_HAND_JOINT_CASE
			}
			return "Unknown hand joint";
		}

		const char* bodyJointName(int joint)
		{
			switch (joint)
			{
#define HOL_BODY_JOINT_CASE(name, value) case value: return #name;
				XR_LIST_ENUM_XrBodyJointFB(HOL_BODY_JOINT_CASE)
#undef HOL_BODY_JOINT_CASE
			}
			return "Unknown body joint";
		}

		ImU32 snapshotColor(size_t index, int alpha = 210)
		{
			constexpr std::array<std::array<int, 3>, 8> Colors = {{
				{255, 90, 110},
				{70, 210, 255},
				{255, 210, 70},
				{100, 235, 140},
				{215, 125, 255},
				{255, 145, 65},
				{90, 235, 220},
				{235, 120, 190},
			}};
			const auto& color = Colors[index % Colors.size()];
			return IM_COL32(color[0], color[1], color[2], alpha);
		}

		std::string formatCaptureTime(int64_t timestamp)
		{
			const std::time_t time = static_cast<std::time_t>(timestamp);
			std::tm localTime{};
			localtime_s(&localTime, &time);
			char buffer[20]{};
			std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
			return buffer;
		}
	} // namespace

	Visualizer::Visualizer()
	{
		this->mRawZoom = 5;
		applyZoom();

		this->mCameraPosition = Eigen::Vector3f(0, 0, this->mCameraZoom);
		this->mCameraRotation = Eigen::Vector3f(0, 0, 0);
		this->mCameraAim = Eigen::Vector3f(0, 0, 0);
		this->mCameraOrientation = Eigen::Quaternionf::Identity();

		this->mViewMatrix = Eigen::Matrix4f::Identity();
		this->mProjectionMatrix = Eigen::Matrix4f::Identity();

		this->mActiveDrawQueue = &this->mDrawSwap0;
		this->mSwapQueue = &this->mDrawSwap1;
		this->mDrawQueue = &this->mDrawSwap2;

		this->mTrackingWriteFrame = &this->mTrackingFrame0;
		this->mTrackingPendingFrame = &this->mTrackingFrame1;
		this->mTrackingDisplayFrame = &this->mTrackingFrame2;
	}

	ImU32 Visualizer::fadeColor(ImU32 baseColor, float alpha)
	{
		ImVec4 asFloat = ImGui::ColorConvertU32ToFloat4(baseColor);
		asFloat.w *= std::clamp(alpha, 0.0f, 1.0f);
		return ImGui::ColorConvertFloat4ToU32(asFloat);
	}

	void Visualizer::init()
	{
		// Must be called on UI thread!
		this->mUiThreadId = std::this_thread::get_id();
		loadSnapshots();
	}

	void Visualizer::centerTo(Eigen::Vector3f center)
	{
		Eigen::Vector3f move = center - mCameraAim;

		this->mCameraAim += move;
		this->mCameraPosition += move;
	}

	bool Visualizer::isActive() const
	{
		return this->mIsActive;
	}

	void Visualizer::setActive(bool active)
	{
		this->mIsActive = active;
	}

	void Visualizer::publishTrackingFrame(const HOL::OpenXR::HandTracking& handTracking,
									  const HOL::OpenXR::BodyTracking& bodyTracking)
	{
		if (!mIsActive)
		{
			return;
		}

		auto& frame = *mTrackingWriteFrame;
		frame = {};
		frame.valid = true;
		frame.runtimeName = HOL::state::Runtime.runtimeName;
		frame.provider = HOL::state::Runtime.trackingProvider;

		for (int side = 0; side < HOL::HandSide_MAX; side++)
		{
			const auto handSide = static_cast<HOL::HandSide>(side);
			const OpenXRHand* hand = handTracking.getHand(handSide);
			const HOL::HandPose& pose = handTracking.getHandPose(handSide);
			auto& handFrame = frame.hands[side];
			handFrame.active = pose.active;
			handFrame.valid = pose.poseValid;
			handFrame.tracked = pose.poseTracked;
			std::copy_n(
				hand->getLastJointLocations(), XR_HAND_JOINT_COUNT_EXT, handFrame.joints.begin());

			auto& controller = frame.controllers[side];
			controller.active = pose.active;
			controller.valid = pose.poseValid;
			controller.tracked = pose.poseTracked;
			controller.pose = pose.controllerLocation;
		}

		const OpenXRBody& body = bodyTracking.getBodyTracker();
		frame.bodyAvailable = body.isAvailable();
		frame.bodyActive = body.active;
		frame.bodyConfidence = body.confidence;
		std::copy_n(
			body.getLastJointLocations(), XR_BODY_JOINT_COUNT_FB, frame.bodyJoints.begin());
		frame.bodyTrackerLocations = bodyTracking.getLastBodyTrackerLocations();

		// The producer and UI each retain their own frame. Only pointer ownership moves while locked,
		// so neither thread reads memory while the other is writing it.
		std::scoped_lock lock(mTrackingFrameLock);
		std::swap(mTrackingWriteFrame, mTrackingPendingFrame);
		mTrackingFramePending = true;
	}

	void Visualizer::consumeTrackingFrame()
	{
		std::scoped_lock lock(mTrackingFrameLock);
		if (!mTrackingFramePending)
		{
			return;
		}
		std::swap(mTrackingPendingFrame, mTrackingDisplayFrame);
		mTrackingFramePending = false;
	}

	void Visualizer::swapOuterDrawQueue()
	{
		// Swap swap and draw
		mDrawSwapLock.lock();
		std::swap(this->mActiveDrawQueue, this->mDrawQueue);
		mDrawSwapLock.unlock();
	}

	void Visualizer::swapInnerDrawQueue()
	{
		// Swap swap and draw
		mDrawSwapLock.lock();
		std::swap(this->mActiveDrawQueue, this->mDrawQueue);
		mDrawSwapLock.unlock();
	}

	void Visualizer::clearDrawQueue()
	{
		this->mDrawQueue->points.clear();
		this->mDrawQueue->lines.clear();
		this->mDrawQueue->triangles.clear();
	}

	void Visualizer::drawVisualizer()
	{
		this->swapInnerDrawQueue();
		consumeTrackingFrame();

		drawAxis();

		// Auto-resizing? it's a mystery
		// This nonsense is good enough for now
		ImVec2 parentWindowSize = ImGui::GetContentRegionAvail();

		ImGui::BeginChild("VisualWindow",
						  parentWindowSize,
						  ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX);

		ImVec2 upperLeft = ImGui::GetCursorScreenPos();
		ImVec2 lowerRight = ImGui::GetContentRegionAvail();

		/*
		ImGui::GetWindowDrawList()->AddCircleFilled(
			upperLeft, 5, IM_COL32(255, 0, 0, 255)); // Red dot
		ImGui::GetWindowDrawList()->AddCircleFilled(
			lowerRight, 5, IM_COL32(0, 255, 0, 255)); // Green dot

		// Draw lines
		ImGui::GetWindowDrawList()->AddLine(
			upperLeft, lowerRight, IM_COL32(23, 139, 255, 255)); // Yellow line
		*/

		calculateProjectionMatrix();
		drawTrackingFrames();

		// Controllers
		updateControllerTrails();
		drawControllerTrails();
		drawModifierCones();

		// General draw requests
		drawTriangles();
		drawLines();
		drawPoints();
		this->clearInternalDrawQueue(); // Leave clean slate for next frame

		// handle input and draw widgets after drawing scene,
		// so we know the area we don't want to be interactable
		ImGui::SliderFloat("FoV", &this->mFov, 10.f, 179.f, "%.3f");
		if (!mTrackingDisplayFrame->valid)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("Snapshot"))
		{
			captureSnapshot();
		}
		if (!mTrackingDisplayFrame->valid)
		{
			ImGui::EndDisabled();
		}
		ImGui::SameLine();
		if (mSnapshots.empty())
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("Clear Snapshots"))
		{
			clearSnapshots();
		}
		if (mSnapshots.empty())
		{
			ImGui::EndDisabled();
		}
		ImGui::SameLine();
		ImGui::Text("%zu snapshot%s", mSnapshots.size(), mSnapshots.size() == 1 ? "" : "s");
		for (size_t index = 0; index < mSnapshots.size(); index++)
		{
			const auto& snapshot = mSnapshots[index];
			const ImVec4 color = ImGui::ColorConvertU32ToFloat4(snapshotColor(index));
			ImGui::TextColored(color,
							   "Snapshot %zu: %s / %s, %s",
							   index + 1,
							   snapshot.frame.runtimeName.c_str(),
							   providerName(snapshot.frame.provider),
							   formatCaptureTime(snapshot.capturedAt).c_str());
		}
		if (ImGui::Checkbox("Follow left   ", &HOL::Config.visualizer.followLeftHand))
		{
			if (Config.visualizer.followLeftHand)
			{
				Config.visualizer.followRightHand = false;
			}
		}

		ImGui::SameLine();

		if (ImGui::Checkbox("Follow Right", &HOL::Config.visualizer.followRightHand))
		{
			if (Config.visualizer.followRightHand)
			{
				Config.visualizer.followLeftHand = false;
			}
		}

		// Body tracking / tracker visualization controls
		ImGui::Checkbox("Body Palm Axes", &HOL::Config.visualizer.showBodyTrackingPalmAxes);
		ImGui::SameLine();
		ImGui::Checkbox("Body Joint Axes", &HOL::Config.visualizer.showBodyTrackingJointAxes);
		ImGui::SameLine();
		ImGui::Checkbox("Body Tracker Axes", &HOL::Config.visualizer.showBodyTrackerAxes);

		// Hand tracking visualization controls
		ImGui::Checkbox("Hand Palm Axes", &HOL::Config.visualizer.showHandTrackingPalmAxes);
		ImGui::SameLine();
		ImGui::Checkbox("Hand Joint Axes", &HOL::Config.visualizer.showHandTrackingJointAxes);

		// Final controller pose visualization controls
		ImGui::Checkbox("Controller Trails", &HOL::Config.visualizer.showControllerPositionTrails);
		ImGui::Checkbox("Look-at Cone", &HOL::Config.visualizer.showLookAtModifierCone);
		ImGui::SameLine();
		ImGui::Checkbox("In-view Cone", &HOL::Config.visualizer.showInViewModifierCone);
		ImGui::SameLine();
		ImGui::Checkbox(
			"Palm-facing Cone", &HOL::Config.visualizer.showPalmFacingModifierCone);

		ImVec2 uiBounds = ImVec2(0, ImGui::GetCursorScreenPos().y);
		ImGui::SameLine(); // so we get the actual end position of the slider
		uiBounds.x = ImGui::GetCursorScreenPos().x;

		handleInput(uiBounds);
		drawTrackingHoverInfo();

		ImGui::GetWindowDrawList()->AddCircleFilled(
			ImVec2((upperLeft.x + lowerRight.x) / 2.0f, (upperLeft.y + lowerRight.y) / 2.0f),
			3,
			IM_COL32(23, 139, 255, 255)); // Green dot

		ImGui::EndChild();
	}

	void Visualizer::drawTrackingFrames()
	{
		mHoverTargets.clear();
		if (mTrackingDisplayFrame->valid)
		{
			drawTrackingFrame(*mTrackingDisplayFrame, -1);
		}
		for (size_t index = 0; index < mSnapshots.size(); index++)
		{
			drawTrackingFrame(mSnapshots[index].frame, static_cast<int>(index));
		}
	}

	void Visualizer::drawTrackingFrame(const TrackingVisualizationFrame& frame,
									   int snapshotIndex)
	{
		const bool snapshot = snapshotIndex >= 0;
		const ImU32 snapshotPointColor
			= snapshot ? snapshotColor(static_cast<size_t>(snapshotIndex)) : 0;
		const ImU32 snapshotLineColor
			= snapshot ? snapshotColor(static_cast<size_t>(snapshotIndex), 180) : 0;
		const ImU32 handPointColor
			= snapshot ? snapshotPointColor : IM_COL32(155, 155, 155, 255);
		const ImU32 handLineColor
			= snapshot ? snapshotLineColor : IM_COL32(255, 255, 255, 255);

		for (int side = 0; side < HOL::HandSide_MAX; side++)
		{
			const auto& hand = frame.hands[side];
			for (int jointIndex = 0; jointIndex < XR_HAND_JOINT_COUNT_EXT; jointIndex++)
			{
				const auto& joint = hand.joints[jointIndex];
				const Eigen::Vector3f position = OpenXR::toEigenVector(joint.pose.position);
				submitPoint(position, handPointColor, snapshot ? 4.0f : 5.0f);
				mHoverTargets.push_back({HoverType::HandJoint, side, jointIndex, position});
			}

			for (int finger = 0; finger < FingerType_MAX; finger++)
			{
				const XrHandJointEXT rootJoint
					= OpenXR::getRootJoint(static_cast<FingerType>(finger));
				for (int jointOffset = 0; jointOffset < 4; jointOffset++)
				{
					const auto& joint = hand.joints[rootJoint + jointOffset];
					const auto& nextJoint = hand.joints[rootJoint + jointOffset + 1];
					submitLine(OpenXR::toEigenVector(joint.pose.position),
							   OpenXR::toEigenVector(nextJoint.pose.position),
							   handLineColor,
							   snapshot ? 1.5f : 2.0f);
				}
			}

			const auto& palm = hand.joints[XR_HAND_JOINT_PALM_EXT];
			if (Config.visualizer.showHandTrackingPalmAxes
				&& (palm.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
			{
				submitOrientationAxes(OpenXR::toEigenVector(palm.pose.position),
								  OpenXR::toEigenQuaternion(palm.pose.orientation),
								  0.120f,
								  snapshot ? 3.0f : 6.0f);
			}

			if (Config.visualizer.showHandTrackingJointAxes)
			{
				for (const auto& joint : hand.joints)
				{
					const bool poseValid
						= (joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
						  && (joint.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
					if (poseValid)
					{
						submitOrientationAxes(OpenXR::toEigenVector(joint.pose.position),
										  OpenXR::toEigenQuaternion(joint.pose.orientation),
										  0.040f,
										  snapshot ? 1.0f : 2.0f);
					}
				}
			}

			if (!snapshot)
			{
				if ((side == HOL::LeftHand && Config.visualizer.followLeftHand)
					|| (side == HOL::RightHand && Config.visualizer.followRightHand))
				{
					centerTo(OpenXR::toEigenVector(palm.pose.position));
				}
			}
		}

		for (int jointIndex = 0; jointIndex < XR_BODY_JOINT_COUNT_FB; jointIndex++)
		{
			const auto& joint = frame.bodyJoints[jointIndex];
			const bool valid
				= (joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
			if (!valid && (!frame.bodyAvailable || !frame.bodyActive))
			{
				continue;
			}
			const bool tracked
				= (joint.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
			ImU32 color = snapshotPointColor;
			if (!snapshot)
			{
				color = valid ? (tracked ? IM_COL32(0, 255, 0, 150)
									 : IM_COL32(255, 180, 0, 170))
							  : IM_COL32(255, 0, 0, 150);
			}
			const Eigen::Vector3f position = OpenXR::toEigenVector(joint.pose.position);
			submitPoint(position, color, snapshot ? 5.0f : 7.0f);
			mHoverTargets.push_back({HoverType::BodyJoint, 0, jointIndex, position});
		}

		if (!snapshot)
		{
			for (XrBodyJointFB palmIndex :
				 {XR_BODY_JOINT_LEFT_HAND_PALM_FB, XR_BODY_JOINT_RIGHT_HAND_PALM_FB})
			{
				const auto& palm = frame.bodyJoints[palmIndex];
				const bool valid
					= (palm.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
				const bool tracked
					= (palm.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
				if (valid && !tracked)
				{
					submitPoint(OpenXR::toEigenVector(palm.pose.position),
								IM_COL32(255, 255, 0, 255),
								7.0f);
				}
			}
		}

		if (Config.visualizer.showBodyTrackingJointAxes)
		{
			for (const auto& joint : frame.bodyJoints)
			{
				const bool poseValid
					= (joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
					  && (joint.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
				if (poseValid)
				{
					submitOrientationAxes(OpenXR::toEigenVector(joint.pose.position),
								  OpenXR::toEigenQuaternion(joint.pose.orientation),
								  0.040f,
								  snapshot ? 1.0f : 2.0f);
				}
			}
		}

		if (Config.visualizer.showBodyTrackingPalmAxes)
		{
			for (XrBodyJointFB palmIndex :
				 {XR_BODY_JOINT_LEFT_HAND_PALM_FB, XR_BODY_JOINT_RIGHT_HAND_PALM_FB})
			{
				const auto& palm = frame.bodyJoints[palmIndex];
				if (palm.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
				{
					submitOrientationAxes(OpenXR::toEigenVector(palm.pose.position),
									  OpenXR::toEigenQuaternion(palm.pose.orientation),
									  0.120f,
									  snapshot ? 3.0f : 6.0f);
				}
			}
		}

		// Controller markers appear while snapshots exist so the app-side result can be compared
		// without permanently adding more geometry to the normal visualizer.
		if (!mSnapshots.empty())
		{
			for (int side = 0; side < HOL::HandSide_MAX; side++)
			{
				const auto& controller = frame.controllers[side];
				if (!controller.active || !controller.valid)
				{
					continue;
				}
				const ImU32 color = snapshot ? snapshotPointColor
					: (side == HOL::LeftHand ? IM_COL32(80, 180, 255, 255)
											  : IM_COL32(255, 180, 80, 255));
				submitPoint(controller.pose.position, color, 9.0f);
				submitOrientationAxes(
					controller.pose.position, controller.pose.orientation, 0.08f, 3.0f);
				mHoverTargets.push_back(
					{HoverType::Controller, side, 0, controller.pose.position});
			}
		}

		if (!snapshot && Config.visualizer.showBodyTrackerAxes && frame.bodyAvailable)
		{
			for (const auto& location : frame.bodyTrackerLocations)
			{
				submitOrientationAxes(location.position, location.orientation, 0.060f, 3.0f);
			}
		}
	}

	void Visualizer::drawTrackingHoverInfo()
	{
		if (!ImGui::IsWindowHovered() || mHoverTargets.empty())
		{
			return;
		}

		const ImVec2 mouse = ImGui::GetMousePos();
		const HoverTarget* hovered = nullptr;
		float closestDistanceSquared = HoverRadius * HoverRadius;
		for (const auto& target : mHoverTargets)
		{
			const ImVec2 point = projectToScreen(target.position);
			const float dx = point.x - mouse.x;
			const float dy = point.y - mouse.y;
			const float distanceSquared = dx * dx + dy * dy;
			if (distanceSquared <= closestDistanceSquared)
			{
				closestDistanceSquared = distanceSquared;
				hovered = &target;
			}
		}
		if (hovered == nullptr)
		{
			return;
		}

		struct PoseInfo
		{
			HOL::PoseLocation pose;
			XrSpaceLocationFlags flags = 0;
			bool active = true;
			bool valid = true;
			bool tracked = true;
		};

		const auto getPoseInfo = [&](const TrackingVisualizationFrame& frame,
									 const HoverTarget& target,
									 PoseInfo& info)
		{
			if (target.type == HoverType::HandJoint)
			{
				const auto& hand = frame.hands[target.side];
				const auto& joint = hand.joints[target.joint];
				info.pose.position = OpenXR::toEigenVector(joint.pose.position);
				info.pose.orientation = OpenXR::toEigenQuaternion(joint.pose.orientation);
				info.flags = joint.locationFlags;
				info.active = hand.active;
				info.valid = (joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
				info.tracked = (joint.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
			}
			else if (target.type == HoverType::BodyJoint)
			{
				const auto& joint = frame.bodyJoints[target.joint];
				info.pose.position = OpenXR::toEigenVector(joint.pose.position);
				info.pose.orientation = OpenXR::toEigenQuaternion(joint.pose.orientation);
				info.flags = joint.locationFlags;
				info.active = frame.bodyActive;
				info.valid = (joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
				info.tracked = (joint.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
			}
			else
			{
				const auto& controller = frame.controllers[target.side];
				info.pose = controller.pose;
				info.active = controller.active;
				info.valid = controller.valid;
				info.tracked = controller.tracked;
			}
		};

		const auto drawPose = [&](const char* label,
							  const TrackingVisualizationFrame& frame,
							  const PoseInfo& info,
							  const PoseInfo* live,
							  const ImVec4* color)
		{
			if (color != nullptr)
			{
				ImGui::TextColored(*color, "%s: %s / %s", label, frame.runtimeName.c_str(), providerName(frame.provider));
			}
			else
			{
				ImGui::Text("%s: %s / %s", label, frame.runtimeName.c_str(), providerName(frame.provider));
			}
			ImGui::Text("  Position: %.6f, %.6f, %.6f",
						info.pose.position.x(),
						info.pose.position.y(),
						info.pose.position.z());
			ImGui::Text("  Quaternion: %.6f, %.6f, %.6f, %.6f",
						info.pose.orientation.x(),
						info.pose.orientation.y(),
						info.pose.orientation.z(),
						info.pose.orientation.w());
			if (info.pose.orientation.squaredNorm() > 0.000001f)
			{
				const Eigen::Vector3f euler = HOL::quaternionToEulerAngles(
					info.pose.orientation.normalized())
					* (180.0f / std::numbers::pi_v<float>);
				ImGui::Text("  Euler XYZ: %.3f, %.3f, %.3f", euler.x(), euler.y(), euler.z());
			}
			ImGui::Text("  Active: %s  Valid: %s  Tracked: %s  Flags: 0x%llx",
						info.active ? "yes" : "no",
						info.valid ? "yes" : "no",
						info.tracked ? "yes" : "no",
						static_cast<unsigned long long>(info.flags));

			if (live != nullptr)
			{
				const float translationMM = (info.pose.position - live->pose.position).norm() * 1000.0f;
				float rotationDegrees = 0.0f;
				if (info.pose.orientation.squaredNorm() > 0.000001f
					&& live->pose.orientation.squaredNorm() > 0.000001f)
				{
					const float dot = std::clamp(
						std::abs(info.pose.orientation.normalized().dot(
							live->pose.orientation.normalized())),
						0.0f,
						1.0f);
					rotationDegrees
						= 2.0f * std::acos(dot) * (180.0f / std::numbers::pi_v<float>);
				}
				ImGui::Text("  Delta from live: %.3f mm, %.3f deg", translationMM, rotationDegrees);
			}
		};

		ImGui::BeginTooltip();
		if (hovered->type == HoverType::HandJoint)
		{
			ImGui::Text("%s hand, %s (%d)",
						hovered->side == HOL::LeftHand ? "Left" : "Right",
						handJointName(hovered->joint),
						hovered->joint);
		}
		else if (hovered->type == HoverType::BodyJoint)
		{
			ImGui::Text("%s (%d)", bodyJointName(hovered->joint), hovered->joint);
		}
		else
		{
			ImGui::Text("%s app-side controller pose",
						hovered->side == HOL::LeftHand ? "Left" : "Right");
		}
		ImGui::Separator();

		PoseInfo liveInfo;
		const PoseInfo* live = nullptr;
		if (mTrackingDisplayFrame->valid)
		{
			getPoseInfo(*mTrackingDisplayFrame, *hovered, liveInfo);
			live = &liveInfo;
			drawPose("Live", *mTrackingDisplayFrame, liveInfo, nullptr, nullptr);
		}
		for (size_t index = 0; index < mSnapshots.size(); index++)
		{
			PoseInfo info;
			getPoseInfo(mSnapshots[index].frame, *hovered, info);
			const ImVec4 color = ImGui::ColorConvertU32ToFloat4(snapshotColor(index));
			const std::string label = "Snapshot " + std::to_string(index + 1) + " ("
				+ formatCaptureTime(mSnapshots[index].capturedAt) + ")";
			drawPose(label.c_str(), mSnapshots[index].frame, info, live, &color);
		}
		ImGui::EndTooltip();
	}

	void Visualizer::captureSnapshot()
	{
		if (!mTrackingDisplayFrame->valid)
		{
			return;
		}
		TrackingVisualizationSnapshot snapshot;
		snapshot.capturedAt = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
						  .count();
		snapshot.frame = *mTrackingDisplayFrame;
		mSnapshots.push_back(std::move(snapshot));
		saveSnapshots();
	}

	void Visualizer::clearSnapshots()
	{
		mSnapshots.clear();
		std::error_code error;
		std::filesystem::remove(HOL::Paths::getVisualizerSnapshotsFilePath(), error);
		if (error)
		{
			std::cerr << "Failed to remove visualizer snapshots: " << error.message() << std::endl;
		}
	}

	void Visualizer::saveSnapshots() const
	{
		const nlohmann::json root = {
			{"version", SnapshotFileVersion},
			{"snapshots", mSnapshots},
		};

		std::ofstream file(HOL::Paths::getVisualizerSnapshotsFilePath());
		if (!file)
		{
			std::cerr << "Failed to open the visualizer snapshot file for writing." << std::endl;
			return;
		}
		file << root.dump(2);
	}

	void Visualizer::loadSnapshots()
	{
		const std::filesystem::path path = HOL::Paths::getVisualizerSnapshotsFilePath();
		std::ifstream file(path);
		if (!file)
		{
			return;
		}

		try
		{
			nlohmann::json root;
			file >> root;
			if (root.at("version").get<int>() != SnapshotFileVersion)
			{
				std::cerr << "Ignoring visualizer snapshots from an unsupported version."
						  << std::endl;
				return;
			}

			mSnapshots = root.at("snapshots").get<std::vector<TrackingVisualizationSnapshot>>();
		}
		catch (const std::exception& ex)
		{
			std::cerr << "Failed to load visualizer snapshots: " << ex.what() << std::endl;
		}
	}

	void Visualizer::drawPoints()
	{
		auto queues = {this->mActiveDrawQueue, &this->mInternalDrawQueue};

		for (auto queue : queues)
		{
			for (auto& point : queue->points)
			{

				auto projected = projectToScreen(point.position);

				ImGui::GetWindowDrawList()->AddCircleFilled(
					projected, point.size, point.color); // Red dot
			}
		}
	}

	void Visualizer::drawLines()
	{
		auto queues = {this->mActiveDrawQueue, &this->mInternalDrawQueue};

		for (auto queue : queues)
		{
			for (auto& line : queue->lines)
			{
				ImGui::GetWindowDrawList()->AddLine(projectToScreen(line.start),
													projectToScreen(line.end),
													line.color,
													line.width); // Red dot
			}
		}
	}

	void Visualizer::updateControllerTrails()
	{
		// Grabs the final position we send to steamvr.
		// Note that this does not include the driver offset.

		if (!HOL::Config.visualizer.showControllerPositionTrails)
		{
			clearControllerTrails();
			return;
		}

		// Add current position
		auto now = std::chrono::steady_clock::now();
		for (int side = 0; side < HandSide_MAX; side++)
		{
			auto& trail = this->mControllerTrails[side];

			// Get rid of older points
			while (!trail.empty() && now - trail.front().time > ControllerTrailDuration)
			{
				trail.pop_front();
			}

			const auto& controller = mTrackingDisplayFrame->controllers[side];
			if (!mTrackingDisplayFrame->valid || !controller.active || !controller.valid)
			{
				continue;
			}

			trail.push_back({controller.pose.position, now});
		}
	}

	void Visualizer::drawTriangles()
	{
		auto queues = {this->mActiveDrawQueue, &this->mInternalDrawQueue};

		for (auto queue : queues)
		{
			for (auto& triangle : queue->triangles)
			{
				ImGui::GetWindowDrawList()->AddTriangleFilled(projectToScreen(triangle.p0),
															 projectToScreen(triangle.p1),
															 projectToScreen(triangle.p2),
															 triangle.color);
			}
		}
	}

	void Visualizer::drawControllerTrails()
	{
		if (!HOL::Config.visualizer.showControllerPositionTrails)
		{
			return;
		}

		auto now = std::chrono::steady_clock::now();
		const ImU32 trailColors[] = {
			IM_COL32(80, 180, 255, 220),
			IM_COL32(255, 180, 80, 220),
		};

		for (int side = 0; side < HandSide_MAX; side++)
		{
			auto& trail = this->mControllerTrails[side];
			if (trail.size() < 2)
			{
				continue;
			}

			for (size_t i = 1; i < trail.size(); i++)
			{
				float startAge = std::chrono::duration<float>(now - trail[i - 1].time).count()
								 / std::chrono::duration<float>(ControllerTrailDuration).count();
				float endAge = std::chrono::duration<float>(now - trail[i].time).count()
							   / std::chrono::duration<float>(ControllerTrailDuration).count();
				float segmentAlpha = 1.0f - ((startAge + endAge) * 0.5f);

				// Fade older segments out so the freshest motion stands out at a glance.
				ImGui::GetWindowDrawList()->AddLine(projectToScreen(trail[i - 1].position),
													projectToScreen(trail[i].position),
													fadeColor(trailColors[side], segmentAlpha),
													2.0f);
			}

			for (const auto& point : trail)
			{
				float pointAge = std::chrono::duration<float>(now - point.time).count()
								 / std::chrono::duration<float>(ControllerTrailDuration).count();
				float pointAlpha = 1.0f - pointAge;
				ImGui::GetWindowDrawList()->AddCircleFilled(
					projectToScreen(point.position),
					2.5f,
					fadeColor(trailColors[side], pointAlpha));
			}
		}
	}

	void Visualizer::clearControllerTrails()
	{
		for (auto& trail : this->mControllerTrails)
		{
			trail.clear();
		}
	}

	void Visualizer::clearInternalDrawQueue()
	{
		this->mInternalDrawQueue.points.clear();
		this->mInternalDrawQueue.lines.clear();
		this->mInternalDrawQueue.triangles.clear();
	}

	DrawQueue* Visualizer::getDrawQueueForSubmit()
	{
		// Internal queue if on UI thread, otherwise draw queue.
		return std::this_thread::get_id() == this->mUiThreadId
							   ? &this->mInternalDrawQueue
							   : this->mDrawQueue;
	}

	void Visualizer::handleInput(ImVec2 excludeBounds)
	{
		bool moved = false;

		ImVec2 leftMouseClickPos = ImGui::GetIO().MouseClickedPos[0];
		ImVec2 rightMouseClickPos = ImGui::GetIO().MouseClickedPos[1];
		ImVec2 mouseCurrentPos = ImGui::GetIO().MousePos;

		// Exclude bounds is the upper left corner where our other widgets live
		bool leftClickValid
			= leftMouseClickPos.x > excludeBounds.x || leftMouseClickPos.y > excludeBounds.y;
		bool rightClickValid
			= rightMouseClickPos.x > excludeBounds.x || rightMouseClickPos.y > excludeBounds.y;
		bool posValid = mouseCurrentPos.x > excludeBounds.x || mouseCurrentPos.y > excludeBounds.y;

		if (rightClickValid && ImGui::IsMouseDragging(1)) // Right click
		{
			moved = true;
			ImVec2 delta = ImGui::GetIO().MouseDelta;

			float sensitivity = 0.5f;

			Eigen::Vector3f move(-delta.x * sensitivity, delta.y * sensitivity, 0);

			// Modulate by zoom level
			float zoomMultiplier = this->mCameraZoom / 1000.f;
			move *= zoomMultiplier;

			move = this->mCameraOrientation * move;

			mCameraAim += move;

			this->centerToAim();
		}

		if (leftClickValid && ImGui::IsMouseDragging(0)) // Left click
		{
			moved = true;
			// Adjust camera rotation based on mouse movement
			float rotationSensitivity = 0.1f; // Adjust as needed
			float pitch = ImGui::GetIO().MouseDelta.y * rotationSensitivity;
			float yaw = ImGui::GetIO().MouseDelta.x * rotationSensitivity;

			// Would add delta to existing rotation, but quaternions end up with weird will when you
			// do that.
			mCameraRotation.x() -= pitch;
			mCameraRotation.y() -= yaw;

			mCameraRotation.x() = std::clamp(mCameraRotation.x(), -90.0f, 90.0f);

			// Create quaternion from pitch/yaw input
			// Do separately and combine so we apply yaw first, then pitch
			Eigen::Quaternionf pitchOffset
				= HOL::quaternionFromEulerAnglesDegrees(Eigen::Vector3f(mCameraRotation.x(), 0, 0));
			Eigen::Quaternionf yawOffset
				= HOL::quaternionFromEulerAnglesDegrees(Eigen::Vector3f(0, mCameraRotation.y(), 0));

			this->mCameraOrientation = yawOffset * pitchOffset;

			this->centerToAim();
		}

		if (posValid)
		{
			// Adjust zoom level based on mouse wheel input
			float zoomDelta = ImGui::GetIO().MouseWheel;

			if (zoomDelta != 0)
			{
				moved = true;
				float zoomSensitivity = 0.2f;

				mRawZoom -= zoomDelta * zoomSensitivity;

				applyZoom();

				centerToAim();
			}
		}

		/*
		if (moved)
		{
			printf("Pos: %.3f, %.3f, %.3f\n",
				   mCameraPosition.x(),
				   mCameraPosition.y(),
				   mCameraPosition.z());

			printf("Aim: %.3f, %.3f, %.3f\n",
			mCameraAim.x(), mCameraAim.y(), mCameraAim.z());

			printf("Rot: %.3f, %.3f, %.3f\n",
				mCameraRotation.x(),
				mCameraRotation.y(),
				mCameraRotation.z());
		}
		*/
	}

	// Stolen from
	// https://www.scratchapixel.com/lessons/3d-basic-rendering/perspective-and-orthographic-projection-matrix/opengl-perspective-projection-matrix.html
	static void gluPerspective(const float& angleOfView,
							   const float& imageAspectRatio,
							   const float& n,
							   const float& f,
							   float& b,
							   float& t,
							   float& l,
							   float& r)
	{
		float scale = tan(angleOfView * 0.5 * std::numbers::pi_v<float> / 180) * n;
		r = imageAspectRatio * scale, l = -r;
		t = scale, b = -t;
	}

	static void glFrustum(const float& b,
						  const float& t,
						  const float& l,
						  const float& r,
						  const float& n,
						  const float& f,
						  Eigen::Matrix4f& M)
	{
		// Set OpenGL perspective projection matrix
		M(0, 0) = 2.f * n / (r - l);
		M(0, 1) = 0;
		M(0, 2) = 0;
		M(0, 3) = 0;

		M(1, 0) = 0;
		M(1, 1) = 2.f * n / (t - b);
		M(1, 2) = 0;
		M(1, 3) = 0;

		M(2, 0) = (r + l) / (r - l);
		M(2, 1) = (t + b) / (t - b);
		M(2, 2) = -(f + n) / (f - n);
		M(2, 3) = -1;

		M(3, 0) = 0;
		M(3, 1) = 0;
		M(3, 2) = -2.f * f * n / (f - n);
		M(3, 3) = 0;
	}

	void Visualizer::calculateProjectionMatrix()
	{
		Eigen::Matrix4f rotationMatrix = Eigen::Matrix4f::Identity();

		rotationMatrix.block<3, 3>(0, 0) = mCameraOrientation.toRotationMatrix();

		Eigen::Affine3f translationAff(
			Eigen::Translation3f(mCameraPosition.x(), mCameraPosition.y(), mCameraPosition.z()));

		this->mViewMatrix = translationAff.matrix() * rotationMatrix;

		// World -> camera, so inverse
		this->mViewMatrix = this->mViewMatrix.matrix().inverse().eval();

		ImVec2 upperLeft = ImGui::GetCursorScreenPos();
		ImVec2 lowerRight = ImGui::GetContentRegionAvail();
		ImVec2 screenSize(lowerRight.x - upperLeft.x, lowerRight.y - upperLeft.y);

		float ratio = screenSize.x / screenSize.y; // Calculate or provide the aspect
												   // ratio (width / height)
		// of the viewport
		float nearPlane = 0.1; // Specify the distance to the near clipping plane
		float farPlane = 1000; // Specify the distance to the far clipping plane

		float b, t, l, r;
		gluPerspective(this->mFov, ratio, nearPlane, farPlane, b, t, l, r);
		glFrustum(b, t, l, r, nearPlane, farPlane, this->mProjectionMatrix);
	}

	ImVec2 Visualizer::projectToScreen(const Eigen::Vector3f& position)
	{
		Eigen::Vector4f homogeneousPosition(position.x(), position.y(), position.z(), 1.0f);

		Eigen::Vector4f screenPosition = (mProjectionMatrix * mViewMatrix) * homogeneousPosition;

		// Normalize by W to do perspective magic
		screenPosition /= screenPosition.w();

		// Ehh clip space is weird
		screenPosition.y() = -screenPosition.y();

		// Get size of windoe we're drawing into
		ImVec2 upperLeft = ImGui::GetCursorScreenPos();
		ImVec2 screenSize = ImGui::GetContentRegionAvail();

		float width = (screenSize.x - upperLeft.x);
		float height = (screenSize.y - upperLeft.y);

		// -1 to 1 : 0 to 1
		screenPosition.x() += 1.0f;
		screenPosition.y() += 1.0f;
		screenPosition.x() /= 2.0f;
		screenPosition.y() /= 2.0f;

		// And back into our screen space
		screenPosition.x() *= width;
		screenPosition.y() *= height;

		return ImVec2(screenPosition.x() + upperLeft.x, screenPosition.y() + upperLeft.y);
	}
	void Visualizer::drawAxis()
	{
		auto colorGrey = IM_COL32(155, 155, 155, 255);
		auto colorRed = IM_COL32(255, 0, 0, 255);
		auto colorGreen = IM_COL32(0, 255, 0, 255);
		auto colorBlue = IM_COL32(23, 139, 255, 255);

		float width = 1;

		float halfWidth = width / 2.f;

		Eigen::Vector3f topLeft = Eigen::Vector3f(-halfWidth, halfWidth, halfWidth);
		Eigen::Vector3f topRight = Eigen::Vector3f(halfWidth, halfWidth, halfWidth);
		Eigen::Vector3f bottomRight = Eigen::Vector3f(halfWidth, -halfWidth, halfWidth);
		Eigen::Vector3f bottomLeft = Eigen::Vector3f(-halfWidth, -halfWidth, halfWidth);

		Eigen::Vector3f axisCenter = bottomLeft;
		axisCenter.z() = -halfWidth;

		this->submitPoint(topLeft, colorGrey, 5);
		this->submitPoint(topRight, colorGrey, 5);
		this->submitPoint(bottomRight, colorGrey, 5);
		this->submitPoint(bottomLeft, colorBlue, 5);

		this->submitLine(axisCenter, bottomLeft, colorBlue, 5);

		topLeft.z() = -halfWidth;
		topRight.z() = -halfWidth;
		bottomRight.z() = -halfWidth;
		bottomLeft.z() = -halfWidth;

		this->submitLine(axisCenter, topLeft, colorGreen, 5);
		this->submitLine(axisCenter, bottomRight, colorRed, 5);

		this->submitPoint(topLeft, colorGreen, 5);
		this->submitPoint(topRight, colorGrey, 5);
		this->submitPoint(bottomRight, colorRed, 5);
		this->submitPoint(bottomLeft, colorGrey, 5);

		this->submitPoint(this->mCameraAim, colorGrey, 8);
	}
	void Visualizer::centerToAim()
	{
		auto aimToNewCam = Eigen::Vector3f(0, 0, 1);
		aimToNewCam *= this->mCameraZoom;

		this->mCameraPosition = this->mCameraOrientation * aimToNewCam;
		mCameraPosition += this->mCameraAim;
	}
	void Visualizer::applyZoom()
	{
		mCameraZoom = mRawZoom * mRawZoom;

		if (mCameraZoom < 0.01)
		{
			mCameraZoom = 0.01;
		}
	}

	void Visualizer::submitPoint(const Eigen::Vector3f& position, ImU32 color, float size)
	{
		DrawQueue* queue = getDrawQueueForSubmit();

		queue->points.push_back({position, color, size});
	}

	void Visualizer::submitLine(const Eigen::Vector3f& start,
								const Eigen::Vector3f& end,
								ImU32 color,
								float width)
	{
		DrawQueue* queue = getDrawQueueForSubmit();

		queue->lines.push_back({start, end, color, width});
	}

	void Visualizer::submitTriangle(const Eigen::Vector3f& p0,
									const Eigen::Vector3f& p1,
									const Eigen::Vector3f& p2,
									ImU32 color)
	{
		DrawQueue* queue = getDrawQueueForSubmit();

		queue->triangles.push_back({p0, p1, p2, color});
	}

	void Visualizer::submitOrientationAxes(const Eigen::Vector3f& position,
										   const Eigen::Quaternionf& orientation,
										   float axisLength,
										   float lineWidth)
	{
		// Standard RGB color scheme: Red=X, Green=Y, Blue=Z
		auto colorRed = IM_COL32(255, 0, 0, 255);
		auto colorGreen = IM_COL32(0, 255, 0, 255);
		auto colorBlue = IM_COL32(23, 139, 255, 255);

		// Calculate end points for each axis
		Eigen::Vector3f xEnd = position + (orientation * Eigen::Vector3f(axisLength, 0, 0));
		Eigen::Vector3f yEnd = position + (orientation * Eigen::Vector3f(0, axisLength, 0));
		Eigen::Vector3f zEnd = position + (orientation * Eigen::Vector3f(0, 0, axisLength));

		// Submit the three axis lines
		submitLine(position, xEnd, colorRed, lineWidth);	// X axis - Red
		submitLine(position, yEnd, colorGreen, lineWidth);	// Y axis - Green
		submitLine(position, zEnd, colorBlue, lineWidth);	// Z axis - Blue
	}

	void Visualizer::submitCone(const Eigen::Vector3f& origin,
								const Eigen::Vector3f& forward,
								float fovDegrees,
								float length,
								ImU32 fillColor,
								ImU32 lineColor,
								float lineWidth)
	{
		Eigen::Vector3f coneForward = forward;
		if (coneForward.squaredNorm() <= 0.000001f || length <= 0.0f)
		{
			return;
		}

		coneForward.normalize();
		float clampedFovDegrees = std::clamp(fovDegrees, 1.0f, 179.0f);
		float halfAngleRadians
			= clampedFovDegrees * 0.5f * (std::numbers::pi_v<float> / 180.0f);
		float radius = std::tan(halfAngleRadians) * length;
		Eigen::Vector3f baseCenter = origin + coneForward * length;

		Eigen::Vector3f referenceUp = std::abs(coneForward.z()) < 0.99f
										  ? Eigen::Vector3f::UnitZ()
										  : Eigen::Vector3f::UnitX();
		Eigen::Vector3f right = coneForward.cross(referenceUp).normalized();
		Eigen::Vector3f up = right.cross(coneForward).normalized();

		constexpr int SegmentCount = 24;
		std::array<Eigen::Vector3f, SegmentCount> rimPoints;
		for (int i = 0; i < SegmentCount; i++)
		{
			float angle = (2.0f * std::numbers::pi_v<float> * i) / SegmentCount;
			rimPoints[i] = baseCenter + right * (std::cos(angle) * radius)
						   + up * (std::sin(angle) * radius);
		}

		for (int i = 0; i < SegmentCount; i++)
		{
			int next = (i + 1) % SegmentCount;
			submitTriangle(origin, rimPoints[i], rimPoints[next], fillColor);
			submitLine(origin, rimPoints[i], lineColor, lineWidth);
			submitLine(rimPoints[i], rimPoints[next], lineColor, lineWidth);
		}
	}

	void Visualizer::drawModifierCones()
	{
		const auto& bodyTracking = HOL::display::BodyTracking;
		if (bodyTracking.headPoseValid)
		{
			const Eigen::Vector3f headForward
				= bodyTracking.headPose.orientation * Eigen::Vector3f::UnitY();

			if (HOL::Config.visualizer.showLookAtModifierCone)
			{
				submitCone(bodyTracking.headPose.position,
						   headForward,
						   HOL::Config.input.lookAtFovDegrees,
						   0.25f,
						   IM_COL32(80, 180, 255, 35),
						   IM_COL32(80, 180, 255, 110));
			}

			if (HOL::Config.visualizer.showInViewModifierCone)
			{
				submitCone(bodyTracking.headPose.position,
						   headForward,
						   HOL::Config.input.inViewFovDegrees,
						   0.25f,
						   IM_COL32(255, 210, 80, 28),
						   IM_COL32(255, 210, 80, 110));
			}
		}

		if (HOL::Config.visualizer.showPalmFacingModifierCone)
		{
			const ImU32 fillColors[] = {
				IM_COL32(80, 255, 180, 28),
				IM_COL32(255, 120, 180, 28),
			};
			const ImU32 lineColors[] = {
				IM_COL32(80, 255, 180, 110),
				IM_COL32(255, 120, 180, 110),
			};

			for (int side = 0; side < HandSide_MAX; side++)
			{
				const auto& transform = HOL::display::HandTransform[side];
				if (!transform.active || !transform.positionValid)
				{
					continue;
				}

				Eigen::Vector3f palmForward
					= transform.rawPose.orientation * -Eigen::Vector3f::UnitY();
				submitCone(transform.rawPose.position,
						   palmForward,
						   HOL::Config.input.palmFacingFovDegrees,
						   0.18f,
						   fillColors[side],
						   lineColors[side]);
			}
		}
	}

} // namespace HOL
