#include "user_interface.h"

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <vector>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <imgui_impl_win32.h>
#include <iterator>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

#include "src/core/settings_global.h"
#include "src/core/app_paths.h"
#include "src/core/ui/display_global.h"
#include "src/core/state_global.h"
#include <HandOfLesserCommon.h>
#include "src/version.h"
#include "src/core/HandOfLesserCore.h"
#include "src/hands/gesture_binding_builder.h"
#include "src/steamvr/input_wrapper.h"

using namespace HOL;

static const int WINDOW_HEIGHT = 600;

UserInterface* UserInterface::Current = nullptr;

namespace
{
	GLFWmonitor* getWindowMonitor(GLFWwindow* window)
	{
		// GLFW only reports an attached monitor for fullscreen windows. For a normal window we
		// approximate "current monitor" by picking the display with the largest overlap area.
		int windowX, windowY, windowWidth, windowHeight;
		glfwGetWindowPos(window, &windowX, &windowY);
		glfwGetWindowSize(window, &windowWidth, &windowHeight);

		int bestOverlap = -1;
		GLFWmonitor* bestMonitor = glfwGetPrimaryMonitor();

		int monitorCount = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
		for (int i = 0; i < monitorCount; i++)
		{
			GLFWmonitor* monitor = monitors[i];
			int monitorX, monitorY;
			glfwGetMonitorPos(monitor, &monitorX, &monitorY);

			const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
			if (videoMode == nullptr)
			{
				continue;
			}

			int overlapWidth
				= std::max(0,
						   std::min(windowX + windowWidth, monitorX + videoMode->width)
							   - std::max(windowX, monitorX));
			int overlapHeight
				= std::max(0,
						   std::min(windowY + windowHeight, monitorY + videoMode->height)
							   - std::max(windowY, monitorY));
			// The window may straddle two monitors during a drag, so choose the one that owns
			// the biggest portion of the current window rectangle.
			int overlapArea = overlapWidth * overlapHeight;

			if (overlapArea > bestOverlap)
			{
				bestOverlap = overlapArea;
				bestMonitor = monitor;
			}
		}

		return bestMonitor;
	}

	void buildUrlLine(const char* label, const char* url)
	{
		ImGui::TextUnformatted(label);
		ImGui::SameLine();
		ImGui::TextLinkOpenURL(url);
	}
}

HOL::UserInterface::UserInterface()
{
	UserInterface::Current = this;
}

void UserInterface::init()
{
	initGLFW();
	this->mImguiIniPath = HOL::Paths::getImguiIniFilePath().string();
	initImgui();
	this->mVisualizer.init();
	this->mAvailableOpenXRRuntimes = HOL::OpenXR::getAvailableOpenXRRuntimePaths(1);

	float xscale, yscale = 0;
	glfwGetWindowContentScale(this->mWindow, &xscale, &yscale);
	this->mContentScale = xscale;
	updateStyles(xscale);
	updateWindowSize(false);
}

void UserInterface::terminate()
{
	// Which to cleanup first?
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(this->mWindow);
	this->mWindow = nullptr;
	glfwTerminate();
}

bool UserInterface::shouldCloseWindow()
{
	return glfwWindowShouldClose(this->mWindow);
}

void UserInterface::onFrame()
{
	glfwPollEvents();
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	glClear(GL_COLOR_BUFFER_BIT); // Whendo we clear?
	// ImGui::ShowDemoWindow(); // Show demo window! :)

	buildInterface();

	// Do stuff

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	glfwSwapBuffers(this->mWindow);
}

void UserInterface::updateStyles(float scale)
{
	this->mContentScale = scale;
	scale *= 1.25f;
	this->mScale = scale;

	ImGuiIO& io = ImGui::GetIO();

	// Setup Dear ImGui style
	ImGuiStyle& style = ImGui::GetStyle();

	ImGuiStyle styleold = style; // Backup colors
	style = ImGuiStyle(); // IMPORTANT: ScaleAllSizes will change the original size, so we should
						  // reset all style config
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;
	style.TabBorderSize = 1.0f;
	style.WindowRounding = 0.0f;
	style.ChildRounding = 0.0f;
	style.PopupRounding = 0.0f;
	style.FrameRounding = 0.0f;
	style.ScrollbarRounding = 0.0f;
	style.GrabRounding = 0.0f;
	style.TabRounding = 0.0f;
	// Keep modal and popup dimming dark so it matches the rest of the UI
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
	style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
	style.ScaleAllSizes(scale);

	io.FontGlobalScale = scale; // Fonts look blurry, need to rebuild atlas I guess.

	return;
}

void UserInterface::updateWindowSize(bool preserveHeight)
{
	const ImGuiStyle& style = ImGui::GetStyle();
	// Keep the window width matched to our fixed content width.
	int width = (int)std::ceil(scaleSize(PanelWidth) + style.WindowPadding.x * 2.0f);
	int minHeight = (int)std::ceil(scaleSize(500.0f));
	int defaultHeight = (int)std::ceil(scaleSize(WINDOW_HEIGHT));
	int maxHeight = GLFW_DONT_CARE;

	if (GLFWmonitor* monitor = getWindowMonitor(this->mWindow))
	{
		int workareaX, workareaY, workareaWidth, workareaHeight;
		glfwGetMonitorWorkarea(monitor, &workareaX, &workareaY, &workareaWidth, &workareaHeight);
		if (workareaHeight > 0)
		{
			// Clamp the default and maximum height to the visible desktop work area so
			// the window does not launch off-screen on shorter displays.
			maxHeight = workareaHeight;
			defaultHeight = std::min(defaultHeight, workareaHeight);
		}
	}

	int currentWidth = 0;
	int currentHeight = 0;
	glfwGetWindowSize(this->mWindow, &currentWidth, &currentHeight);

	int height = defaultHeight;
	if (preserveHeight)
	{
		// On DPI/content-scale changes, preserve the current user-selected height instead
		// of snapping back to the default height every time.
		height = std::max(currentHeight, minHeight);
		if (maxHeight != GLFW_DONT_CARE)
		{
			height = std::min(height, maxHeight);
		}
	}

	// Lock the width, but allow the user to resize vertically within sensible limits.
	glfwSetWindowSizeLimits(this->mWindow, width, minHeight, width, maxHeight);
	if (currentWidth != width || currentHeight != height)
	{
		// Changing the window size can itself trigger another content-scale callback when the
		// window crosses a monitor boundary, so suppress that re-entrant callback path.
		this->mApplyingWindowSize = true;
		glfwSetWindowSize(this->mWindow, width, height);
		this->mApplyingWindowSize = false;
	}
}

bool HOL::UserInterface::rightAlignButton(const char* label, int verticalLineOffset)
{
	float available_width = ImGui::GetContentRegionAvail().x;
	float rightMargin = ImGui::GetStyle().ScrollbarSize + ImGui::GetStyle().ItemSpacing.x;

	// note that hide_text_after_double_hash is false, otherwise it will calculate its width too
	float button_width
		= ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;

	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_width - button_width - rightMargin);

	if (verticalLineOffset != 0)
	{
		ImGui::SetCursorPosY(ImGui::GetCursorPosY()
							 + (ImGui::GetTextLineHeight()
								+ (ImGui::GetStyle().FramePadding.x * 2) * verticalLineOffset));
	}

	return ImGui::Button(label);
}

void HOL::UserInterface::showWrappedTooltip(const char* text)
{
	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

void UserInterface::initGLFW()
{
	glfwSetErrorCallback(UserInterface::error_callback);

	if (glfwInit() != GLFW_TRUE)
	{
		std::cerr << "glfw init failed!" << std::endl;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	this->mWindow = glfwCreateWindow(PanelWidth, WINDOW_HEIGHT, "Hand of Lesser", NULL, NULL);
	if (!this->mWindow)
	{
		std::cerr << "glfw windows creation failed!" << std::endl;
	}

	// glfwSetWindowSizeCallback(this->mWindow, framebuffer_size_callback);
	glfwSetWindowContentScaleCallback(this->mWindow, windows_scale_callback);
	glfwMakeContextCurrent(this->mWindow);
	glfwSwapInterval(1);

	std::cout << "GLFW init finished" << std::endl;
}

void UserInterface::initImgui()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = this->mImguiIniPath.c_str();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(this->mWindow,
								 true); // Second param install_callback=true will install GLFW
										// callbacks and chain to existing ones.
	ImGui_ImplOpenGL3_Init();
	std::cout << "Imgui init finished" << std::endl;
}

void UserInterface::error_callback(int error, const char* description)
{
}

void UserInterface::windows_scale_callback(GLFWwindow* window, float xscale, float yscale)
{
	if (UserInterface::Current->mApplyingWindowSize)
	{
		// Ignore scale changes caused by our own corrective resize to avoid bouncing between
		// two monitors with different DPI scales.
		return;
	}

	if (std::abs(UserInterface::Current->mContentScale - xscale) < 0.001f)
	{
		// Some platforms emit redundant scale callbacks while the effective scale is unchanged.
		return;
	}

	std::cout << "New scale: " << xscale << std::endl;
	UserInterface::Current->updateStyles(xscale); // xy should be the same?
	UserInterface::Current->updateWindowSize();
}

float UserInterface::scaleSize(float size)
{
	return size * this->mScale;
}

void UserInterface::buildSingleHandTransformDisplay(HOL::HandSide side)
{
	const char* sideName = (side == HOL::LeftHand ? "Left" : "Right");
	const char* windowName = (side == HOL::LeftHand ? "LeftChild" : "RightRight");
	int width = (side == HOL::LeftHand ? ImGui::GetContentRegionAvail().x * 0.5f : 0);

	ImGui::BeginChild(windowName, ImVec2(width, 0), ImGuiChildFlags_AutoResizeY);

	ImGui::SeparatorText(sideName);

	ImGui::Checkbox("Active", &HOL::display::HandTransform[side].active);
	ImGui::SameLine();
	ImGui::Checkbox("Valid", &HOL::display::HandTransform[side].positionValid);
	ImGui::SameLine();
	ImGui::Checkbox("Tracked", &HOL::display::HandTransform[side].positionTracked);

	const char* dataSource = "Unknown";
	switch (HOL::display::HandTransform[side].dataSource)
	{
		case XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT:
			dataSource = "Hand";
			break;
		case XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT:
			dataSource = "Controller";
			break;
		default:
			break;
	}

	ImGui::Text("Data source: %s", dataSource);
	ImGui::Text("Tracked joints: %d / 26", HOL::display::HandTransform[side].trackedJointCount);
	ImGui::Text(
		"Update rate: %5.2f ms", HOL::display::HandTransform[side].updateRateMS.load());

	ImGui::SeparatorText("Position");

	ImGui::Text("Raw   : %.3f, %.3f, %.3f",
				HOL::display::HandTransform[side].rawPose.position.x(),
				HOL::display::HandTransform[side].rawPose.position.y(),
				HOL::display::HandTransform[side].rawPose.position.z());

	ImGui::Text("Final : %.3f, %.3f, %.3f",
				HOL::display::HandTransform[side].finalPose.position.x(),
				HOL::display::HandTransform[side].finalPose.position.y(),
				HOL::display::HandTransform[side].finalPose.position.z());

	ImGui::Text("Offset: %.3f, %.3f, %.3f",
				HOL::display::HandTransform[side].finalTranslationOffset.x(),
				HOL::display::HandTransform[side].finalTranslationOffset.y(),
				HOL::display::HandTransform[side].finalTranslationOffset.z());

	ImGui::SeparatorText("Orientation");

	{
		Eigen::Vector3f asEuler
			= HOL::display::HandTransform[side].rawPose.orientation.toRotationMatrix()
				  .canonicalEulerAngles(0, 1, 2);
		ImGui::Text("Raw   : %.3f, %.3f, %.3f",
					HOL::radiansToDegrees(asEuler.x()),
					HOL::radiansToDegrees(asEuler.y()),
					HOL::radiansToDegrees(asEuler.z()));
	}

	{
		Eigen::Vector3f asEuler = HOL::display::HandTransform[side]
									  .finalPose.orientation.toRotationMatrix()
									  .canonicalEulerAngles(0, 1, 2);
		ImGui::Text("Final : %.3f, %.3f, %.3f",
					HOL::radiansToDegrees(asEuler.x()),
					HOL::radiansToDegrees(asEuler.y()),
					HOL::radiansToDegrees(asEuler.z()));
	}

	{
		ImGui::Text("Offset: %.3f, %.3f, %.3f",
					HOL::display::HandTransform[side].finalOrientationOffset.x(),
					HOL::display::HandTransform[side].finalOrientationOffset.y(),
					HOL::display::HandTransform[side].finalOrientationOffset.z());
	}

	ImGui::EndChild();
}

void UserInterface::buildSingleFingerTrackingDisplay(
	const char* windowLabel,
	HOL::HandSide side,
	HOL::FingerBend bendValues[HOL::FingerType_MAX],
	bool treatAsInt,
	bool treatAsRadians)
{

	int width = (side == HOL::LeftHand ? ImGui::GetContentRegionAvail().x * 0.5f : 0);

	ImGui::BeginChild(windowLabel, ImVec2(width, 0), ImGuiChildFlags_AutoResizeY);

	// ImGui::SeparatorText(sideName);

	HOL::FingerTrackingDisplay* displayValues = &HOL::display::FingerTracking[(int)side];
	const char* rowNames[] = {"Index", "Middle", "Ring", "Little", "Thumb"};

	// Text fields are not affected by item width, so place manually
	float labelWidth = scaleSize(60);
	float valueWidth = scaleSize(55);

	for (int i = 0; i < HOL::FingerType_MAX; i++)
	{
		float positionOffset = labelWidth;

		ImGui::Text("%s: ", rowNames[i]);
		ImGui::SameLine(positionOffset);

		for (int j = 0; j < HOL::FingerBendType_MAX; j++)
		{
			// Pad positive values with a space so negative values stay aligned.
			// There is a format thing to add a + for positive numbers, but not just a space.

			float value = bendValues[i].bend[j];
			if (treatAsRadians)
			{
				value = HOL::radiansToDegrees(value);
			}

			if (treatAsInt)
			{
				value = value;
				ImGui::Text(value >= 0 ? " %d" : "%d", (int)std::roundf(value));
			}
			else
			{
				value = value;
				ImGui::Text(value >= 0 ? " %.2f" : "%.2f", value);
			}

			positionOffset += valueWidth;

			if (j < HOL::FingerBendType_MAX - 1)
			{
				ImGui::SameLine(positionOffset);
			}
		}
	}

	ImGui::EndChild();
}

void UserInterface::InputFloatMultipleSingleLableWithButtons(
	std::string inputLabelBase,
	std::string visibleLabel,
	float smallIncrement,
	float largeIncrement,
	std::string format,
	int width,
	const std::vector<const float*>& values)
{
	ImGui::PushItemWidth(scaleSize(width));

	for (int i = 0; i < values.size(); i++)
	{
		ImGui::InputFloat(("##" + inputLabelBase + std::to_string(i)).c_str(),
						  (float*)values[i],
						  smallIncrement,
						  largeIncrement,
						  format.c_str());
		ImGui::SameLine();
	}

	ImGui::Text(visibleLabel.c_str());

	ImGui::PopItemWidth();
}

void UserInterface::InputFloatMultipleTopLableWithButtons(
	std::string inputLabelBase,
	const std::vector<std::string>& visibleLabel,
	float smallIncrement,
	float largeIncrement,
	std::string format,
	int width,
	const std::vector<const float*>& values)
{
	ImGui::PushItemWidth(scaleSize(width));

	for (int i = 0; i < values.size(); i++)
	{
		ImGui::BeginGroup();

		const char* label;
		if (i >= visibleLabel.size())
		{
			label = "";
		}
		else
		{
			label = visibleLabel[i].c_str();
		}

		ImGui::Text(label);

		ImGui::InputFloat(("##" + inputLabelBase + std::to_string(i)).c_str(),
						  (float*)values[i],
						  smallIncrement,
						  largeIncrement,
						  format.c_str());

		ImGui::EndGroup();

		if (i < values.size() - 1)
			ImGui::SameLine();
	}

	ImGui::PopItemWidth();
}

void HOL::UserInterface::buildVisual()
{
	mVisualizer.drawVisualizer();
}

void HOL::UserInterface::buildBindings()
{
	const char* editBindingPopupLabel = "EditBindingPopup";
	bool syncDriverSettings = false;
	bool rebuildActions = false;
	bool openEditBindingPopup = false;
	auto ensureCompatibleTarget = [](settings::GestureBinding& binding)
	{
		if (!GestureBindings::isGestureTargetCompatible(binding.kind, binding.target))
		{
			binding.target = GestureBindings::firstCompatibleInputTarget(binding.kind);
		}
	};

	auto supportsPressAndRelease = [](settings::InputTarget target)
	{
		return target == settings::InputTarget::Trigger
			   || (target >= settings::InputTarget::A
				   && target <= settings::InputTarget::Thumbrest);
	};

	auto startNewBinding = [&]()
	{
		mEditBindingIndex = -1;
		mEditBinding = settings::GestureBinding{};
		mEditBinding.enabled = true;
		mEditBinding.kind = settings::GestureKind::Proximity;
		mEditBinding.proximityFinger = FingerMiddle;
		ensureCompatibleTarget(mEditBinding);
		mShowingEditForm = true;
		openEditBindingPopup = true;
	};

	ImGui::BeginChild("BindingsWindow",
					   ImVec2(scaleSize(PanelWidth), 0),	
					   ImGuiChildFlags_AutoResizeY);

	ImGui::SeparatorText("Gesture Bindings");
	ImGui::TextWrapped("Configure which hand gesture maps to which controller input.");

	ImGui::SeparatorText("Joystick Reference");

	const bool bodyTrackingAvailable = HOL::state::Runtime.supportsBodyTracking;
	const bool chestReferenceAvailable
		= bodyTrackingAvailable
		  && (HOL::state::Runtime.isVDXR || Config.trackingFeatures.enableUpperBodyTracking);
	const bool headReferenceAvailable = bodyTrackingAvailable;
	auto drawReferenceRadio = [&](const char* label,
								  HOL::settings::JoystickReferenceMode mode,
								  bool enabled,
								  const char* disabledTooltip)
	{
		if (!enabled)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::RadioButton(label, Config.input.joystickReferenceMode == mode))
		{
			Config.input.joystickReferenceMode = mode;
			syncDriverSettings = true;
		}

		if (!enabled)
		{
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				showWrappedTooltip(disabledTooltip);
			}
		}
	};

	drawReferenceRadio("Chest",
					   HOL::settings::JoystickReferenceMode::Chest,
					   chestReferenceAvailable,
					   "Chest reference requires upper body tracking.");
	ImGui::SameLine();
	drawReferenceRadio("Head",
					   HOL::settings::JoystickReferenceMode::Head,
					   headReferenceAvailable,
					   "Head reference requires body tracking.");
	ImGui::SameLine();
	drawReferenceRadio("Hand",
					   HOL::settings::JoystickReferenceMode::Hand,
					   true,
					   "");

	ImGui::SeparatorText("Gesture Conditions");
	int chainGestureTimeoutMS = Config.input.chainGestureTimeoutMS;
	if (ImGui::InputInt("Sequence Timeout (ms)", &chainGestureTimeoutMS))
	{
		Config.input.chainGestureTimeoutMS = std::max(50, chainGestureTimeoutMS);
		rebuildActions = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Maximum time allowed between taps in a sequence or multi-tap gesture.");
	}

	int holdDurationMS = Config.input.holdDurationMS;
	if (ImGui::InputInt("Hold Duration (ms)", &holdDurationMS))
	{
		Config.input.holdDurationMS = std::max(50, holdDurationMS);
		rebuildActions = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip(
			"How long a gesture must stay active before the Hold modifier triggers.");
	}

	int gateLagTimeMS = Config.input.gateLagTimeMS;
	if (ImGui::InputInt("Gate Lag Time (ms)", &gateLagTimeMS))
	{
		Config.input.gateLagTimeMS = std::max(0, gateLagTimeMS);
		rebuildActions = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("How long after the main gesture starts a gating condition may still "
						   "activate and count.");
	}

	int pinchDistanceMM = Config.input.pinchDistanceMM;
	if (ImGui::InputInt("Pinch Distance (mm)", &pinchDistanceMM))
	{
		Config.input.pinchDistanceMM = std::clamp(pinchDistanceMM, 1, 79);
		rebuildActions = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Maximum finger distance at which a pinch is fully active.");
	}

	float lookAtFovDegrees = Config.input.lookAtFovDegrees;
	if (ImGui::InputFloat("Look-at FoV (deg)", &lookAtFovDegrees, 1.0f, 5.0f, "%.1f"))
	{
		Config.input.lookAtFovDegrees = std::clamp(lookAtFovDegrees, 1.0f, 179.0f);
		rebuildActions = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Cone of the head looking at the hand");
	}

	float inFrontFovDegrees = Config.input.inFrontFovDegrees;
	if (ImGui::InputFloat("In-front FoV (deg)", &inFrontFovDegrees, 1.0f, 5.0f, "%.1f"))
	{
		Config.input.inFrontFovDegrees = std::clamp(inFrontFovDegrees, 1.0f, 179.0f);
		rebuildActions = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip(
			"Cone infront of the user, from the torso.");
	}

	float palmFacingFovDegrees = Config.input.palmFacingFovDegrees;
	if (ImGui::InputFloat("Palm-facing FoV (deg)", &palmFacingFovDegrees, 1.0f, 5.0f, "%.1f"))
	{
		Config.input.palmFacingFovDegrees = std::clamp(palmFacingFovDegrees, 1.0f, 179.0f);
		rebuildActions = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip(
			"Cone from the hand that head must be inside.");
	}

	ImGui::SameLine();
	if (rightAlignButton("Reset##GestureConditions"))
	{
		HOL::settings::InputSettings defaults;
		Config.input.chainGestureTimeoutMS = defaults.chainGestureTimeoutMS;
		Config.input.holdDurationMS = defaults.holdDurationMS;
		Config.input.gateLagTimeMS = defaults.gateLagTimeMS;
		Config.input.pinchDistanceMM = defaults.pinchDistanceMM;
		Config.input.lookAtFovDegrees = defaults.lookAtFovDegrees;
		Config.input.inFrontFovDegrees = defaults.inFrontFovDegrees;
		Config.input.palmFacingFovDegrees = defaults.palmFacingFovDegrees;
		rebuildActions = true;
	}


	ImGui::SeparatorText("Gesture Bindings");

	if (ImGui::Button("Add Binding"))
	{
		startNewBinding();
	}
	ImGui::SameLine();
	if (ImGui::Button("Restore Defaults"))
	{
		ImGui::OpenPopup("RestoreDefaultsBindings");
	}

	if (ImGui::BeginPopupModal("RestoreDefaultsBindings",
								NULL,
								ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Restore gesture bindings to default?");

		float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

		if (ImGui::Button("OK##restoreBindings", ImVec2(btnW, 0)))
		{
			Config.input.gestureBindings = settings::defaultGestureBindings();
			mExpandedBindingDebugRows.clear();
			ImGui::CloseCurrentPopup();
			rebuildActions = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel##restoreBindings", ImVec2(btnW, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::Spacing();
	ImGui::SeparatorText("Bindings By Side");

	auto renderBindingsForSide = [&](HandSide side, const char* label)
	{
		ImGui::SeparatorText(label);

		bool hasBindings = false;
		int visibleBindingIndex = 0;
		for (int i = 0; i < static_cast<int>(Config.input.gestureBindings.size()); i++)
		{
			const settings::GestureBinding& binding = Config.input.gestureBindings[i];
			if (binding.side != side)
			{
				continue;
			}

			hasBindings = true;
			ImGui::PushID(i);

			const ImGuiStyle& style = ImGui::GetStyle();
			const ImVec4 windowBg = style.Colors[ImGuiCol_WindowBg];
			const ImVec4 frameBg = style.Colors[ImGuiCol_FrameBg];
			auto blendColor = [](const ImVec4& a, const ImVec4& b, float t)
			{
				return ImVec4(a.x + (b.x - a.x) * t,
							  a.y + (b.y - a.y) * t,
							  a.z + (b.z - a.z) * t,
							  a.w + (b.w - a.w) * t);
			};
			ImVec4 rowBg
				= (visibleBindingIndex % 2 == 0)
					  ? blendColor(windowBg, frameBg, 0.20f)
					  : blendColor(windowBg, frameBg, 0.10f);
			rowBg.w = 1.0f;

			ImGui::PushStyleColor(ImGuiCol_ChildBg, rowBg);
			ImGui::PushStyleVar(
				ImGuiStyleVar_WindowPadding, ImVec2(scaleSize(8.0f), scaleSize(6.0f)));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, scaleSize(4.0f));
			ImGui::BeginChild("BindingRow",
							  ImVec2(0, 0),
							  ImGuiChildFlags_AutoResizeY
								  | ImGuiChildFlags_AlwaysUseWindowPadding);

			const float previewScale = this->mScale * 0.5f;
			const float previewWidth = UiGraphics::bindingPreviewWidth(previewScale, binding);
			const float previewHeight = previewWidth > 0.0f ? previewScale * 66.0f : 0.0f;
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float previewRightPadding = scaleSize(6.0f);
			const float rowStartY = ImGui::GetCursorPosY();
			bool deleted = false;

			ImGui::BeginGroup();

			bool enabled = binding.enabled;
			if (ImGui::Checkbox("##enabled", &enabled))
			{
				Config.input.gestureBindings[i].enabled = enabled;
				rebuildActions = true;
			}
			ImGui::SameLine();
			const float leftIndent = ImGui::GetCursorPosX();

			const std::string baseDescription = GestureBindings::describeBindingBase(binding);
			const std::vector<std::string> modifierLabels
				= GestureBindings::describeBindingModifierLabels(binding);
			ImGui::TextUnformatted(baseDescription.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("->");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f),
							   "%s",
							   GestureBindings::inputTargetName(binding.target));

			//ImGui::SetCursorPosX(leftIndent);
			if (ImGui::Button("Edit"))
			{
				mEditBindingIndex = i;
				mEditBinding = binding;
				mShowingEditForm = true;
				openEditBindingPopup = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete"))
			{
				Config.input.gestureBindings.erase(Config.input.gestureBindings.begin() + i);
				std::unordered_set<int> updatedExpandedRows;
				for (int expandedIndex : mExpandedBindingDebugRows)
				{
					if (expandedIndex < i)
					{
						updatedExpandedRows.insert(expandedIndex);
					}
					else if (expandedIndex > i)
					{
						updatedExpandedRows.insert(expandedIndex - 1);
					}
				}
				mExpandedBindingDebugRows = std::move(updatedExpandedRows);
				rebuildActions = true;
				deleted = true;
			}
			ImGui::SameLine();
			const bool expanded = mExpandedBindingDebugRows.contains(i);
			if (ImGui::ArrowButton(
					"##debugExpand", expanded ? ImGuiDir_Down : ImGuiDir_Right))
			{
				if (expanded)
				{
					mExpandedBindingDebugRows.erase(i);
				}
				else
				{
					mExpandedBindingDebugRows.insert(i);
				}
			}
			if (!deleted && !modifierLabels.empty())
			{
				ImGui::SameLine();
				std::string modifierDescription;
				for (size_t modifierIndex = 0; modifierIndex < modifierLabels.size(); modifierIndex++)
				{
					if (modifierIndex > 0)
					{
						modifierDescription += " ";
					}
					modifierDescription += "(";
					modifierDescription += modifierLabels[modifierIndex];
					modifierDescription += ")";
				}
				ImGui::TextDisabled("%s", modifierDescription.c_str());
			}

			ImGui::EndGroup();

			if (!deleted && mExpandedBindingDebugRows.contains(i))
			{
				ImGui::Spacing();
				UiGestureDebug::drawActionDebug(
					this->mScale, HOL::HandOfLesserCore::Current->getActionForBindingIndex(i));
			}

			const float leftHeight = ImGui::GetCursorPosY() - rowStartY;
			if (!deleted && previewWidth > 0.0f)
			{
				const float rightEdgeX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
				const float previewX = rightEdgeX - previewWidth - previewRightPadding;
				const float previewY = rowStartY + std::max(0.0f, (leftHeight - previewHeight) * 0.5f);
				ImGui::SameLine(std::max(ImGui::GetCursorPosX() + spacing, previewX));
				ImGui::SetCursorPosY(previewY);
				UiGraphics::drawBindingPreview(previewScale, binding);
			}

			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
			ImGui::PopID();
			if (deleted)
			{
				break;
			}
			ImGui::Spacing();
			visibleBindingIndex++;
		}

		if (!hasBindings)
		{
			ImGui::TextDisabled("No bindings");
		}
	};

	if (ImGui::BeginTabBar("BindingSides"))
	{
		if (ImGui::BeginTabItem("Left Hand"))
		{
			renderBindingsForSide(LeftHand, "Left Hand");
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Right Hand"))
		{
			renderBindingsForSide(RightHand, "Right Hand");
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (openEditBindingPopup)
	{
		ImGui::OpenPopup(editBindingPopupLabel);
	}
	if (mShowingEditForm
		&& ImGui::BeginPopupModal(editBindingPopupLabel, NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::SeparatorText(mEditBindingIndex >= 0 ? "Edit Binding" : "Add Binding");
		settings::GestureBinding& b = mEditBinding;

		int sideValue = static_cast<int>(b.side);
		ImGui::RadioButton("Left", &sideValue, static_cast<int>(LeftHand));
		ImGui::SameLine();
		ImGui::RadioButton("Right", &sideValue, static_cast<int>(RightHand));
		b.side = static_cast<HandSide>(sideValue);
		ImGui::Separator();

		const std::array<settings::GestureKind, 4> editableKinds = {
			settings::GestureKind::Proximity,
			settings::GestureKind::Chain,
			settings::GestureKind::Grip,
			settings::GestureKind::SystemAim};

		int kindIndex = 0;
		for (int i = 0; i < static_cast<int>(editableKinds.size()); i++)
		{
			if (editableKinds[i] == b.kind)
			{
				kindIndex = i;
				break;
			}
		}

		const char* currentKindLabel = GestureBindings::gestureKindName(editableKinds[kindIndex]);
		if (ImGui::BeginCombo("Gesture", currentKindLabel))
		{
			for (int i = 0; i < static_cast<int>(editableKinds.size()); i++)
			{
				const bool selected = kindIndex == i;
				if (ImGui::Selectable(
						GestureBindings::gestureKindName(editableKinds[i]), selected))
				{
					b.kind = editableKinds[i];
					ensureCompatibleTarget(b);
					if (!supportsPressAndRelease(b.target))
					{
						b.pressAndRelease = false;
					}
					kindIndex = i;
				}

				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		ImGui::Separator();

		if (b.kind == settings::GestureKind::Proximity)
		{
			const std::array<FingerType, 4> fingers = {
				FingerIndex, FingerMiddle, FingerRing, FingerLittle};
			int fingerIndex = 0;
			for (int i = 0; i < static_cast<int>(fingers.size()); i++)
			{
				if (fingers[i] == b.proximityFinger)
				{
					fingerIndex = i;
					break;
				}
			}

			static const char* fingerNames[] = {"Index", "Middle", "Ring", "Little"};
			if (ImGui::Combo("Finger", &fingerIndex, fingerNames, IM_ARRAYSIZE(fingerNames)))
			{
				b.proximityFinger = fingers[fingerIndex];
				if (b.proximityFinger != FingerIndex)
				{
					settings::setGestureModifier(
						b.modifiers, settings::GestureModifier::ClosedHand, false);
				}
			}

			bool requiresClosedHand = settings::hasGestureModifier(
				b.modifiers, settings::GestureModifier::ClosedHand);
			ImGui::BeginDisabled(b.proximityFinger != FingerIndex);
			if (ImGui::Checkbox("Closed Hand", &requiresClosedHand))
			{
				settings::setGestureModifier(
					b.modifiers,
					settings::GestureModifier::ClosedHand,
					requiresClosedHand);
			}
			ImGui::EndDisabled();

			if (settings::hasGestureModifier(
					b.modifiers, settings::GestureModifier::ClosedHand))
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(Closed Hand)");
			}

			ImGui::Separator();
		}

		if (b.kind == settings::GestureKind::Chain)
		{
			int chainLength = b.chainLength;
			if (ImGui::SliderInt(
					"Tap Count",
					&chainLength,
					1,
					settings::GestureBinding::MaxChainLength))
			{
				b.chainLength = chainLength;
			}

			static const char* fingerNames[] = {"Index", "Middle", "Ring", "Little"};
			for (int i = 0; i < b.chainLength; i++)
			{
				int fingerIndex = 0;
				for (int finger = 0; finger < 4; finger++)
				{
					if (static_cast<int>(b.chainFingers[i]) == finger)
					{
						fingerIndex = finger;
						break;
					}
				}

				std::string label = "Tap " + std::to_string(i + 1);
				if (ImGui::Combo(
						label.c_str(), &fingerIndex, fingerNames, IM_ARRAYSIZE(fingerNames)))
				{
					b.chainFingers[i] = static_cast<FingerType>(fingerIndex);
				}
			}

			ImGui::Separator();
		}

		bool useHold = settings::hasGestureModifier(b.modifiers, settings::GestureModifier::Hold);
		if (ImGui::Checkbox("Hold", &useHold))
		{
			settings::setGestureModifier(b.modifiers, settings::GestureModifier::Hold, useHold);
		}
		if (useHold)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%d ms)", Config.input.holdDurationMS);
		}

		auto rightAlignCheckbox = [](const char* label, bool* value)
		{
			float availableWidth = ImGui::GetContentRegionAvail().x;
			float checkboxWidth = ImGui::GetFrameHeight()
								  + ImGui::GetStyle().ItemInnerSpacing.x
								  + ImGui::CalcTextSize(label, nullptr, true).x;
			ImGui::SetCursorPosX(
				ImGui::GetCursorPosX() + std::max(0.0f, availableWidth - checkboxWidth));
			return ImGui::Checkbox(label, value);
		};

		bool useLookAt
			= settings::hasGestureModifier(b.modifiers, settings::GestureModifier::LookingAtHand);
		if (ImGui::Checkbox("Look At Hand", &useLookAt))
		{
			settings::setGestureModifier(
				b.modifiers, settings::GestureModifier::LookingAtHand, useLookAt);
			if (!useLookAt)
			{
				settings::setInvertedGestureModifier(
					b.invertedModifiers, settings::GestureModifier::LookingAtHand, false);
			}
		}
		if (useLookAt)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%.1f deg)", Config.input.lookAtFovDegrees);
			ImGui::SameLine();
			bool invertLookAt = settings::hasGestureModifier(
				b.invertedModifiers, settings::GestureModifier::LookingAtHand);
			if (rightAlignCheckbox("Invert##LookAtHand", &invertLookAt))
			{
				settings::setInvertedGestureModifier(
					b.invertedModifiers, settings::GestureModifier::LookingAtHand, invertLookAt);
			}
		}

		bool useInFront
			= settings::hasGestureModifier(b.modifiers, settings::GestureModifier::InFrontOfUser);
		if (ImGui::Checkbox("In Front", &useInFront))
		{
			settings::setGestureModifier(
				b.modifiers, settings::GestureModifier::InFrontOfUser, useInFront);
			if (!useInFront)
			{
				settings::setInvertedGestureModifier(
					b.invertedModifiers, settings::GestureModifier::InFrontOfUser, false);
			}
		}
		if (useInFront)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%.1f deg)", Config.input.inFrontFovDegrees);
			ImGui::SameLine();
			bool invertInFront = settings::hasGestureModifier(
				b.invertedModifiers, settings::GestureModifier::InFrontOfUser);
			if (rightAlignCheckbox("Invert##InFront", &invertInFront))
			{
				settings::setInvertedGestureModifier(
					b.invertedModifiers, settings::GestureModifier::InFrontOfUser, invertInFront);
			}
		}

		bool usePalmFacing = settings::hasGestureModifier(
			b.modifiers, settings::GestureModifier::PalmFacingUser);
		if (ImGui::Checkbox("Palm Facing User", &usePalmFacing))
		{
			settings::setGestureModifier(
				b.modifiers, settings::GestureModifier::PalmFacingUser, usePalmFacing);
			if (!usePalmFacing)
			{
				settings::setInvertedGestureModifier(
					b.invertedModifiers, settings::GestureModifier::PalmFacingUser, false);
			}
		}
		if (usePalmFacing)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%.1f deg)", Config.input.palmFacingFovDegrees);
			ImGui::SameLine();
			bool invertPalmFacing = settings::hasGestureModifier(
				b.invertedModifiers, settings::GestureModifier::PalmFacingUser);
			if (rightAlignCheckbox("Invert##PalmFacingUser", &invertPalmFacing))
			{
				settings::setInvertedGestureModifier(
					b.invertedModifiers,
					settings::GestureModifier::PalmFacingUser,
					invertPalmFacing);
			}
		}

		ImGui::Separator();

		std::vector<GestureBindings::InputTargetOption> compatibleTargets;
		for (const auto& option : GestureBindings::inputTargetOptions())
		{
			if (GestureBindings::isGestureTargetCompatible(b.kind, option.target))
			{
				compatibleTargets.push_back(option);
			}
		}
		ensureCompatibleTarget(b);

		const char* currentTargetLabel = GestureBindings::inputTargetName(b.target);
		if (ImGui::BeginCombo("Target", currentTargetLabel))
		{
			for (const auto& option : compatibleTargets)
			{
				const bool selected = option.target == b.target;
				if (ImGui::Selectable(option.label, selected))
				{
					b.target = option.target;
					if (!supportsPressAndRelease(b.target))
					{
						b.pressAndRelease = false;
					}
				}

				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		if (supportsPressAndRelease(b.target))
		{
			if (ImGui::Checkbox("Press and Release", &b.pressAndRelease))
			{
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Save"))
		{
			ensureCompatibleTarget(b);
			if (mEditBindingIndex >= 0
				&& mEditBindingIndex < static_cast<int>(Config.input.gestureBindings.size()))
			{
				Config.input.gestureBindings[mEditBindingIndex] = b;
			}
			else
			{
				Config.input.gestureBindings.push_back(b);
			}

			rebuildActions = true;
			mShowingEditForm = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			mShowingEditForm = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::EndChild();

	if (syncDriverSettings)
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}

	if (rebuildActions)
	{
		HOL::HandOfLesserCore::Current->rebuildActions();
	}
}

void HOL::UserInterface::buildMisc()
{
	const char* restoreDefaultsLabel = "Factory reset";
	if (ImGui::Button(restoreDefaultsLabel))
		ImGui::OpenPopup(restoreDefaultsLabel);

	// Always center this window when appearing
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal(restoreDefaultsLabel, NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Restore all settings to default?");
		ImGui::Separator();

		float buttonWidth
			= (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

		if (ImGui::Button("OK##restoreDefaults", ImVec2(buttonWidth, 0)))
		{
			Config = HOL::settings::HandOfLesserSettings();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Cancel##restoreDefaults", ImVec2(buttonWidth, 0)))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	if (ImGui::Button("Request High Fidelity"))
	{
		HOL::HandOfLesserCore::Current->featuresManager.requestBodyTrackingFidelity(true);
	}

	ImGui::SameLine();

	if (ImGui::Button("Request Low Fidelity"))
	{
		HOL::HandOfLesserCore::Current->featuresManager.requestBodyTrackingFidelity(false);
	}

	if (ImGui::Button("Resume Multimodal"))
	{
		HOL::HandOfLesserCore::Current->featuresManager.setMultimodalEnabled(true);
	}

	ImGui::SameLine();

	if (ImGui::Button("Pause Multimodal"))
	{
		HOL::HandOfLesserCore::Current->featuresManager.setMultimodalEnabled(false);
	}
}

void HOL::UserInterface::buildBodyTrackers()
{
	bool syncSettings = false;

	ImGui::BeginChild(
		"BodyTrackersWindow", ImVec2(scaleSize(PanelWidth), 0), ImGuiChildFlags_AutoResizeY);

	ImGui::SeparatorText("Body Trackers");

	syncSettings
		|= ImGui::Checkbox("Enable Body Trackers", &Config.bodyTrackers.enableBodyTrackers);

	ImGui::SeparatorText("Core Trackers");

	syncSettings |= ImGui::Checkbox(
		"Hips", &Config.bodyTrackers.enabled[static_cast<int>(BodyTrackerRole::Hips)]);
	syncSettings |= ImGui::Checkbox(
		"Chest", &Config.bodyTrackers.enabled[static_cast<int>(BodyTrackerRole::Chest)]);

	ImGui::SeparatorText("Left Arm Trackers");

	syncSettings |= ImGui::Checkbox("Left Upper Arm",
									&Config.bodyTrackers.enabled[static_cast<int>(
										BodyTrackerRole::LeftUpperArm)]);
	syncSettings |= ImGui::Checkbox("Left Lower Arm",
									&Config.bodyTrackers.enabled[static_cast<int>(
										BodyTrackerRole::LeftLowerArm)]);

	ImGui::SeparatorText("Right Arm Trackers");

	syncSettings |= ImGui::Checkbox("Right Upper Arm",
									&Config.bodyTrackers.enabled[static_cast<int>(
										BodyTrackerRole::RightUpperArm)]);
	syncSettings |= ImGui::Checkbox("Right Lower Arm",
									&Config.bodyTrackers.enabled[static_cast<int>(
										BodyTrackerRole::RightLowerArm)]);

	ImGui::EndChild();

	if (syncSettings)
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}
}

void HOL::UserInterface::buildSteamVR()
{
	bool syncSettings = false;

	ImGui::BeginChild(
		"LeftSkeletalWindow", ImVec2(scaleSize(PanelWidth), 0), ImGuiChildFlags_AutoResizeY);

	/////////////////
	// General
	/////////////////

	ImGui::SeparatorText("Transmission");

	syncSettings |= ImGui::Checkbox("Transmit legacy finger curl",
									&Config.steamvr.transmitLegacyFingerCurl);

	syncSettings |= ImGui::Checkbox("Transmit SteamVR Input", &Config.steamvr.sendSteamVRInput);

	ImGui::SeparatorText("Blocking behavior");

	syncSettings |= ImGui::Checkbox("Block Controller Input while handtracking",
									&Config.steamvr.blockControllerInputWhileHandTracking);
	syncSettings |= ImGui::Checkbox("Disable other controllers while handtracking",
									&Config.steamvr.disableOtherControllersWhileHandTracking);

	ImGui::SeparatorText("General");
	ImGui::TextDisabled(
		"Pose smoothing profile: %s", state::Runtime.isOVR ? "Oculus" : "Standard");

	syncSettings |= ImGui::InputFloat("Steam Pose offset (ms)",
									  &Config.steamvr.poseSmoothing.steamPoseTimeOffsetMS,
									  1.0f,
									  5.0f,
									  "%.0f");
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Positive is future, negative is past. Higher value will be smoother "
						   "but less responsive.");
	}
	syncSettings |= ImGui::InputFloat("Position smoothing (ms)",
									  &Config.steamvr.poseSmoothing.positionSmoothingMS,
									  1.0f,
									  5.0f,
									  "%.0f");
	syncSettings |= ImGui::InputFloat("Rotation smoothing (ms)",
									  &Config.steamvr.poseSmoothing.rotationSmoothingMS,
									  1.0f,
									  5.0f,
									  "%.0f");
	if (ImGui::InputFloat("Position jitter filter (mm)",
						  &Config.steamvr.poseSmoothing.positionJitterRadiusMM,
						  0.1f,
						  0.5f,
						  "%.1f"))
	{
		Config.steamvr.poseSmoothing.positionJitterRadiusMM
			= std::max(0.0f, Config.steamvr.poseSmoothing.positionJitterRadiusMM);
		syncSettings = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Suppress small hand-position corrections.");
	}
	if (ImGui::InputFloat("Rotation jitter filter (deg)",
						  &Config.steamvr.poseSmoothing.rotationJitterRadiusDegrees,
						  0.1f,
						  0.5f,
						  "%.1f"))
	{
		Config.steamvr.poseSmoothing.rotationJitterRadiusDegrees
			= std::max(0.0f, Config.steamvr.poseSmoothing.rotationJitterRadiusDegrees);
		syncSettings = true;
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Suppress small hand-rotation corrections.");
	}
	syncSettings |= ImGui::Checkbox("Trigger stabilization", &Config.steamvr.triggerStabilization);
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Apply temporary stabilization on trigger input.");
	}
	syncSettings |= ImGui::InputFloat("Trigger stabilization amount (ms)",
									  &Config.steamvr.triggerStabilizationSmoothingMS,
									  10.0f,
									  50.0f,
									  "%.0f");
	syncSettings |= ImGui::InputFloat("Stabilization Falloff (ms)",
									  &Config.steamvr.triggerStabilizationFalloffMS,
									  10.0f,
									  50.0f,
									  "%.0f");
	syncSettings |= ImGui::InputFloat("Hand-tracking resume blend (ms)",
									  &Config.steamvr.handTrackingResumeBlendMS,
									  10.0f,
									  50.0f,
									  "%.0f");
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Blend duration when transitioning from body tracking to hand tracking");
	}

	syncSettings |= ImGui::InputFloat("Linear Velocity multiplier",
									  &Config.steamvr.poseSmoothing.linearVelocityMultiplier,
									  0.05f,
									  0.1f,
									  "%.3f");
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("SteamVR uses velocity for pose prediction");
	}
	syncSettings |= ImGui::InputFloat("Angular Velocity multiplier",
									  &Config.steamvr.poseSmoothing.angularVelocityMultiplier,
									  0.05f,
									  0.1f,
									  "%.3f");
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("SteamVR uses velocity for pose prediction. Predicting rotation usually "
						   "reuslts in rubberbanding, so keep it near or at zero.");
	}

	if (rightAlignButton("Reset##SteamVRGeneral"))
	{
		HOL::settings::SteamVRSettings steamVrDefaults;
		Config.steamvr.poseSmoothing
			= HOL::settings::defaultSteamVRPoseSmoothingSettings(state::Runtime.isOVR);
		Config.steamvr.triggerStabilization = steamVrDefaults.triggerStabilization;
		Config.steamvr.triggerStabilizationSmoothingMS
			= steamVrDefaults.triggerStabilizationSmoothingMS;
		Config.steamvr.triggerStabilizationFalloffMS
			= steamVrDefaults.triggerStabilizationFalloffMS;
		Config.steamvr.handTrackingResumeBlendMS = steamVrDefaults.handTrackingResumeBlendMS;
		syncSettings = true;
	}

	/////////////////
	// Skeletal
	/////////////////

	ImGui::SeparatorText("Skeletal Input");

	syncSettings |= ImGui::InputFloat(
		"Length multiplier", &Config.skeletal.jointLengthMultiplier, 0.01f, 0.1f, "%.3f");
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip(
			"Stretch finger joint length; they end up a bit short with OpenXR data.");
	}
	ImGui::SameLine();
	if (rightAlignButton("Reset##Skeletal"))
	{
		HOL::settings::SkeletalInput defaults;
		Config.skeletal.jointLengthMultiplier = defaults.jointLengthMultiplier;
		syncSettings = true;
	}

	/////////////////
	// Skeletal Offset
	/////////////////

	ImGui::BeginChild("SkeletalTranslationInput",
					  ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0),
					  ImGuiChildFlags_AutoResizeY);

	ImGui::SeparatorText("Translation##Skeletal");

	auto& positionOffset = Config.skeletal.positionOffset;
	ImGui::InputFloat("posX##Skeletal", &positionOffset.x(), 0.001f, 0.01f, "%.3f");
	ImGui::InputFloat("posY##Skeletal", &positionOffset.y(), 0.001f, 0.01f, "%.3f");
	ImGui::InputFloat("posZ##Skeletal", &positionOffset.z(), 0.001f, 0.01f, "%.3f");

	ImGui::EndChild();
	ImGui::SameLine();

	ImGui::BeginChild("OrientationInput##Skeletal", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);

	ImGui::SeparatorText("Orientation##Skeletal");

	auto& orientationOffset = Config.skeletal.orientationOffset;
	ImGui::InputFloat("rotX##Skeletal", &orientationOffset.x(), 1.0f, 5.0f, "%.3f");
	ImGui::InputFloat("rotY##Skeletal", &orientationOffset.y(), 1.0f, 5.0f, "%.3f");
	ImGui::InputFloat("rotZ##Skeletal", &orientationOffset.z(), 1.0f, 5.0f, "%.3f");

	if (rightAlignButton("Reset##SkeletalOffset"))
	{
		HOL::settings::SkeletalInput defaults;
		Config.skeletal.positionOffset = defaults.positionOffset;
		Config.skeletal.orientationOffset = defaults.orientationOffset;
		syncSettings = true;
	}

	ImGui::EndChild();
	ImGui::EndChild();

	/////////////////
	// Devices
	/////////////////

	ImGui::SeparatorText("Devices");

	ImGui::Checkbox("Show all devices", &mShowAllDevices);
	ImGui::SameLine();
	if (ImGui::Checkbox("Show status", &Config.steamvr.showDevicePoseDiagnostics))
	{
		syncSettings = true;
	}

	// Helper lambda to convert device class to string
	auto roleToString = [](vr::ETrackedDeviceClass deviceClass) -> const char*
	{
		switch (deviceClass)
		{
			case vr::TrackedDeviceClass_HMD:
				return "HMD";
			case vr::TrackedDeviceClass_Controller:
				return "Controller";
			case vr::TrackedDeviceClass_GenericTracker:
				return "Tracker";
			case vr::TrackedDeviceClass_TrackingReference:
				return "Base Station";
			case vr::TrackedDeviceClass_DisplayRedirect:
				return "Display Redirect";
			default:
				return "Unknown";
		}
	};

	auto trackingResultToString = [](vr::ETrackingResult result) -> const char*
	{
		switch (result)
		{
			case vr::TrackingResult_Uninitialized:
				return "Uninitialized";
			case vr::TrackingResult_Calibrating_InProgress:
				return "Calibrating";
			case vr::TrackingResult_Calibrating_OutOfRange:
				return "Calibrating (Out of Range)";
			case vr::TrackingResult_Running_OK:
				return "Running OK";
			case vr::TrackingResult_Running_OutOfRange:
				return "Running (Out of Range)";
			case vr::TrackingResult_Fallback_RotationOnly:
				return "Fallback Rotation Only";
			default:
				return "Unknown";
		}
	};

	auto trackingResultColor = [](vr::ETrackingResult result) -> ImVec4
	{
		switch (result)
		{
			case vr::TrackingResult_Running_OK:
				return ImVec4(0.2f, 0.9f, 0.2f, 1.0f);
			case vr::TrackingResult_Running_OutOfRange:
				return ImVec4(0.9f, 0.85f, 0.2f, 1.0f);
			case vr::TrackingResult_Fallback_RotationOnly:
				return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
			default:
				return ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
		}
	};

	auto drawStatusDot = [](const std::string& id, ImVec4 color, const std::string& tooltip)
	{
		float cellWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		float size = ImGui::GetTextLineHeight();
		float xOffset = std::max(0.0f, (cellWidth - size) * 0.5f);
		float yOffset
			= std::max(0.0f, (ImGui::GetFrameHeight() - size) * 0.5f);
		if (xOffset > 0.0f)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset);
		}
		if (yOffset > 0.0f)
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yOffset);
		}
		ImGui::ColorButton(
			id.c_str(), color, ImGuiColorEditFlags_NoTooltip, ImVec2(size, size));
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
	};

	// Collect and sort devices (active first, then inactive)
	std::vector<std::pair<std::string, HOL::settings::DeviceConfig*>> devices;
	for (auto& [serial, config] : Config.deviceSettings.devices)
	{
		const bool isUnsupportedRole = config.role == vr::TrackedDeviceClass_HMD
									   || config.role == vr::TrackedDeviceClass_GenericTracker;
		if (isUnsupportedRole)
		{
			if (config.actAsTracker || config.alsoWhenHeld)
			{
				config.actAsTracker = false;
				config.alsoWhenHeld = false;
				syncSettings = true;
			}
		}

		if (mShowAllDevices || config.activatedThisSession)
		{
			devices.push_back({serial, &config});
		}
	}

	// Sort: activated first, then by serial
	std::sort(devices.begin(),
			  devices.end(),
			  [](const auto& a, const auto& b)
			  {
				  if (a.second->activatedThisSession != b.second->activatedThisSession)
				  {
					  return a.second->activatedThisSession > b.second->activatedThisSession;
				  }
				  return a.first < b.first;
			  });

	// Display devices in a table
	int tableColumnCount = Config.steamvr.showDevicePoseDiagnostics ? 8 : 4;
	if (ImGui::BeginTable("DevicesTable",
						  tableColumnCount,
						  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
							  | ImGuiTableFlags_SizingFixedFit))
	{
		ImGui::TableSetupColumn("Serial", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, scaleSize(85));
		ImGui::TableSetupColumn("Tracker", ImGuiTableColumnFlags_WidthFixed, scaleSize(55));
		ImGui::TableSetupColumn("Held", ImGuiTableColumnFlags_WidthFixed, scaleSize(45));
		if (Config.steamvr.showDevicePoseDiagnostics)
		{
			ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, scaleSize(30));
			ImGui::TableSetupColumn("C", ImGuiTableColumnFlags_WidthFixed, scaleSize(30));
			ImGui::TableSetupColumn("T", ImGuiTableColumnFlags_WidthFixed, scaleSize(30));
			ImGui::TableSetupColumn("S", ImGuiTableColumnFlags_WidthFixed, scaleSize(30));
		}
		ImGui::TableHeadersRow();

		for (const auto& [serial, config] : devices)
		{
			ImGui::TableNextRow();
			const bool isUnsupportedRole = config->role == vr::TrackedDeviceClass_HMD
										   || config->role == vr::TrackedDeviceClass_GenericTracker;

			// Grey out inactive devices
			if (!config->activatedThisSession)
			{
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
			}

			ImGui::TableNextColumn();
			ImGui::Text("%s", serial.c_str());

			ImGui::TableNextColumn();
			ImGui::Text("%s", roleToString(config->role));

			ImGui::TableNextColumn();
			if (!isUnsupportedRole
				&& ImGui::Checkbox(("##actAsTracker_" + serial).c_str(), &config->actAsTracker))
			{
				syncSettings = true;
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("Act as Tracker: mirror this controller as an emulated tracker.");
			}

			ImGui::TableNextColumn();
			// "Also when held" only relevant if actAsTracker is enabled
			ImGui::BeginDisabled(isUnsupportedRole || !config->actAsTracker);
			if (!isUnsupportedRole
				&& ImGui::Checkbox(("##alsoWhenHeld_" + serial).c_str(), &config->alsoWhenHeld))
			{
				syncSettings = true;
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip(
					"Also When Held: keep acting as a tracker even while the controller is held.");
			}
			ImGui::EndDisabled();

			if (Config.steamvr.showDevicePoseDiagnostics)
			{
				bool nativePoseStale = config->nativePoseAgeMs > 50;
				char ageText[32] = {};
				snprintf(ageText,
						 sizeof(ageText),
						 "%05llu ms",
						 static_cast<unsigned long long>(config->nativePoseAgeMs));

				ImGui::TableNextColumn();
				drawStatusDot("##pose_valid_" + serial,
							  config->nativePoseIsValid ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f)
														: ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
							  std::string("Pose Is Valid: ")
								  + (config->nativePoseIsValid ? "true" : "false"));

				ImGui::TableNextColumn();
				drawStatusDot("##device_connected_" + serial,
							  config->nativeDeviceIsConnected ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f)
															  : ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
							  std::string("Device Is Connected: ")
								  + (config->nativeDeviceIsConnected ? "true" : "false"));

				ImGui::TableNextColumn();
				drawStatusDot("##tracking_result_" + serial,
							  trackingResultColor(config->nativeTrackingResult),
							  std::string("Tracking Result: ")
								  + trackingResultToString(config->nativeTrackingResult));

				ImGui::TableNextColumn();
				drawStatusDot("##pose_stale_" + serial,
							  nativePoseStale ? ImVec4(0.9f, 0.2f, 0.2f, 1.0f)
											  : ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
							  std::string("Pose Stale: ") + (nativePoseStale ? "true" : "false")
								  + "\nLast native pose age: " + ageText);
			}

			if (!config->activatedThisSession)
			{
				ImGui::PopStyleVar();
			}
		}

		ImGui::EndTable();
	}

	if (syncSettings)
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}
}

void HOL::UserInterface::buildControllerInput()
{
	bool syncSettings = false;

	ImGui::BeginChild(
		"ControllerInputWindow", ImVec2(scaleSize(PanelWidth), 0), ImGuiChildFlags_AutoResizeY);
	ImGui::SeparatorText("Suppress Touch");

	std::map<std::string, std::set<std::string>> discoveredButtonsByDeviceSerial;
	for (const auto& [serial, device] : Config.deviceSettings.devices)
	{
		if (serial.empty()
			|| (device.role != vr::TrackedDeviceClass_Invalid
				&& device.role != vr::TrackedDeviceClass_Controller))
		{
			continue;
		}

		for (const std::string& buttonPath : device.touchButtons)
		{
			discoveredButtonsByDeviceSerial[serial].insert(buttonPath);
		}
	}

	std::vector<std::string> deviceSerials;
	deviceSerials.reserve(discoveredButtonsByDeviceSerial.size());
	for (const auto& [serial, buttons] : discoveredButtonsByDeviceSerial)
	{
		deviceSerials.push_back(serial);
	}

	if (!deviceSerials.empty() && (mSelectedTouchOverrideDeviceSerial.empty()
								   || !discoveredButtonsByDeviceSerial.contains(
									   mSelectedTouchOverrideDeviceSerial)))
	{
		mSelectedTouchOverrideDeviceSerial = deviceSerials.front();
	}
	else if (deviceSerials.empty())
	{
		mSelectedTouchOverrideDeviceSerial.clear();
	}

	std::vector<std::string> availableButtons;
	const auto selectedDeviceIt
		= discoveredButtonsByDeviceSerial.find(mSelectedTouchOverrideDeviceSerial);
	if (selectedDeviceIt != discoveredButtonsByDeviceSerial.end())
	{
		availableButtons.assign(selectedDeviceIt->second.begin(), selectedDeviceIt->second.end());
	}

	if (mSelectedTouchOverrideButtonPath.empty()
		|| std::find(availableButtons.begin(),
					 availableButtons.end(),
					 mSelectedTouchOverrideButtonPath)
			== availableButtons.end())
	{
		mSelectedTouchOverrideButtonPath
			= availableButtons.empty() ? "" : availableButtons.front();
	}

	const std::string selectedControllerLabel = mSelectedTouchOverrideDeviceSerial.empty()
													? std::string("No controllers discovered")
													: mSelectedTouchOverrideDeviceSerial;
	ImGui::BeginDisabled(deviceSerials.empty());
	if (ImGui::BeginCombo("Controller", selectedControllerLabel.c_str()))
	{
		for (const std::string& serial : deviceSerials)
		{
			bool selected = serial == mSelectedTouchOverrideDeviceSerial;
			if (ImGui::Selectable(serial.c_str(), selected))
			{
				mSelectedTouchOverrideDeviceSerial = serial;
				mSelectedTouchOverrideButtonPath.clear();
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();

	const std::string selectedButtonLabel
		= mSelectedTouchOverrideButtonPath.empty()
			  ? std::string("No touch button available")
			  : HOL::SteamVR::formatLogicalButtonLabel(mSelectedTouchOverrideButtonPath);
	ImGui::BeginDisabled(availableButtons.empty());
	if (ImGui::BeginCombo("Button", selectedButtonLabel.c_str()))
	{
		for (const std::string& buttonPath : availableButtons)
		{
			const std::string label = HOL::SteamVR::formatLogicalButtonLabel(buttonPath);
			bool selected = buttonPath == mSelectedTouchOverrideButtonPath;
			if (ImGui::Selectable(label.c_str(), selected))
			{
				mSelectedTouchOverrideButtonPath = buttonPath;
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(mSelectedTouchOverrideDeviceSerial.empty()
						 || mSelectedTouchOverrideButtonPath.empty());
	if (ImGui::Button("Add"))
	{
		auto& device = Config.deviceSettings.devices[mSelectedTouchOverrideDeviceSerial];
		device.serial = mSelectedTouchOverrideDeviceSerial;
		auto buttonIt = std::find_if(
			device.inputOverrides.begin(),
			device.inputOverrides.end(),
			[&](const HOL::settings::ControllerButtonOverride& override)
			{ return override.buttonPath == mSelectedTouchOverrideButtonPath; });
		if (buttonIt == device.inputOverrides.end())
		{
			HOL::settings::ControllerButtonOverride newButtonOverride;
			newButtonOverride.buttonPath = mSelectedTouchOverrideButtonPath;
			newButtonOverride.suppressTouch = true;
			device.inputOverrides.push_back(newButtonOverride);
		}
		else
		{
			buttonIt->suppressTouch = true;
		}

		syncSettings = true;
	}
	ImGui::EndDisabled();

	ImGui::SeparatorText("Configured Overrides");

	std::string removeDeviceSerial;
	int removeButtonIndex = -1;
	if (ImGui::BeginTable("ControllerTouchOverrides",
						  4,
						  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
							  | ImGuiTableFlags_SizingFixedFit))
	{
		ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, scaleSize(110));
		ImGui::TableSetupColumn("Suppress Touch", ImGuiTableColumnFlags_WidthFixed, scaleSize(110));
		ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, scaleSize(70));
		ImGui::TableHeadersRow();

		for (auto& [serial, device] : Config.deviceSettings.devices)
		{
			for (size_t buttonIndex = 0; buttonIndex < device.inputOverrides.size(); ++buttonIndex)
			{
				auto& buttonOverride = device.inputOverrides[buttonIndex];
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(serial.c_str());

				ImGui::TableNextColumn();
				const std::string buttonLabel
					= HOL::SteamVR::formatLogicalButtonLabel(buttonOverride.buttonPath);
				ImGui::TextUnformatted(buttonLabel.c_str());

				ImGui::TableNextColumn();
				if (ImGui::Checkbox(("##suppressTouch_" + serial + "_"
									 + buttonOverride.buttonPath)
										.c_str(),
									&buttonOverride.suppressTouch))
				{
					syncSettings = true;
				}

				ImGui::TableNextColumn();
				if (ImGui::Button(
						("Delete##touchOverride_" + serial + "_"
						 + buttonOverride.buttonPath)
							.c_str()))
				{
					removeDeviceSerial = serial;
					removeButtonIndex = static_cast<int>(buttonIndex);
				}
			}
		}

		ImGui::EndTable();
	}

	if (!removeDeviceSerial.empty() && removeButtonIndex >= 0)
	{
		auto deviceIt = Config.deviceSettings.devices.find(removeDeviceSerial);
		if (deviceIt != Config.deviceSettings.devices.end()
			&& removeButtonIndex < static_cast<int>(deviceIt->second.inputOverrides.size()))
		{
			deviceIt->second.inputOverrides.erase(
				deviceIt->second.inputOverrides.begin() + removeButtonIndex);
		}
		syncSettings = true;
	}

	ImGui::EndChild();

	if (syncSettings)
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}
}

void HOL::UserInterface::buildMain()
{
	ImGui::BeginChild(
		"LeftMainWindow", ImVec2(scaleSize(PanelWidth), 0), ImGuiChildFlags_AutoResizeY);

	/////////////////////////
	// State
	///////////////////////

	ImGui::SeparatorText("State");

	ImVec4 openXrStateColor = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
	switch (HOL::state::Runtime.openxrState)
	{
		case HOL::OpenXR::OpenXrState::Running:
			openXrStateColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
			break;
		case HOL::OpenXR::OpenXrState::Failed:
			openXrStateColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
			break;
		case HOL::OpenXR::OpenXrState::Initialized:
			openXrStateColor = ImVec4(0.9f, 0.85f, 0.2f, 1.0f);
			break;
		case HOL::OpenXR::OpenXrState::Uninitialized:
		case HOL::OpenXR::OpenXrState::Exited:
		default:
			break;
	}

	ImGui::Text("OpenXR Runtime:");
	ImGui::SameLine();
	ImGui::Text("%s", HOL::state::Runtime.runtimeName);

	ImGui::Text("OpenXR Session state:");
	ImGui::SameLine();
	ImGui::TextColored(openXrStateColor,
					   "%s",
					   HOL::OpenXR::getOpenXrStateString(HOL::state::Runtime.openxrState));

	// Driver connection status
	bool connected = HOL::HandOfLesserCore::Current->isDriverConnected();
	ImGui::Text("Driver:");
	ImGui::SameLine();
	if (connected)
	{
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");
	}
	else
	{
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Disconnected");
	}

	if (ImGui::Checkbox("Launch app automatically with SteamVR", &Config.steamvr.autoLaunchApp))
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}
	if (ImGui::Checkbox("Close app automatically with SteamVR", &Config.steamvr.closeAppOnSteamVRExit))
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}

	//////////////////
	// OpenXR Runtime
	////////////////////

	ImGui::SeparatorText("OpenXR Runtime");

	std::unordered_map<std::string, int> runtimeNameCounts;
	for (const auto& runtimePath : this->mAvailableOpenXRRuntimes)
	{
		runtimeNameCounts[HOL::OpenXR::getOpenXRRuntimeName(runtimePath)]++;
	}
	std::string selectedRuntimeLabel = "Auto";
	if (!Config.openxr.runtimeOverridePath.empty())
	{
		selectedRuntimeLabel = HOL::OpenXR::getOpenXRRuntimeName(Config.openxr.runtimeOverridePath);
		if (runtimeNameCounts[selectedRuntimeLabel] > 1)
		{
			selectedRuntimeLabel = Config.openxr.runtimeOverridePath;
		}
	}

	if (ImGui::BeginCombo("##OpenXRRuntime", selectedRuntimeLabel.c_str()))
	{
		bool selectedAuto = Config.openxr.runtimeOverridePath.empty();
		if (ImGui::Selectable("Auto", selectedAuto))
		{
			Config.openxr.runtimeOverridePath.clear();
			HOL::HandOfLesserCore::Current->syncSettings();
		}
		if (ImGui::IsItemHovered())
		{
			showWrappedTooltip("Use the current system default OpenXR runtime.");
		}

		for (const auto& runtimePath : this->mAvailableOpenXRRuntimes)
		{
			std::string runtimeName = HOL::OpenXR::getOpenXRRuntimeName(runtimePath);
			if (runtimeNameCounts[runtimeName] > 1)
			{
				runtimeName = runtimePath;
			}
			bool selected = Config.openxr.runtimeOverridePath == runtimePath;
			if (ImGui::Selectable(runtimeName.c_str(), selected))
			{
				Config.openxr.runtimeOverridePath = runtimePath;
				HOL::HandOfLesserCore::Current->syncSettings();
			}
			if (ImGui::IsItemHovered())
			{
				showWrappedTooltip(runtimePath.c_str());
			}
		}

		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button("Restart"))
	{
		HOL::HandOfLesserCore::Current->requestTerminate(true);
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Restart to switch runtimes.");
	}

	ImGui::SeparatorText("Runtime");
	ImGui::InputInt("Prediction (ms)", &Config.general.motionPredictionMS);
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip(
			"Amount of prediction to request from the OpenXR runtime. Does not apply to VDXR.");
	}
	if (ImGui::InputInt("Update Interval (ms)", &Config.general.updateIntervalMS))
	{
		if (Config.general.updateIntervalMS < 1)
		{
			Config.general.updateIntervalMS = 1;
		}
	}
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip(
			"Update frequency. Data updates at 10-16ms. Too low or too high causes stutter.");
	}

	if (rightAlignButton("Reset##General"))
	{
		HOL::settings::GeneralSettings defaults;
		Config.general.motionPredictionMS = defaults.motionPredictionMS;
		Config.general.updateIntervalMS = defaults.updateIntervalMS;
	}

	//////////////////
	// Mode
	////////////////////

	ImGui::SeparatorText("Hand Tracking Mode");

	// Left / Right hand dropdowns for which controllers to possess

	// Only list controllers that are currently relevant to this session. The actual selection is
	// persisted as a serial, with an empty string meaning automatic choice in the driver.
	std::vector<std::string> activeControllerSerials;
	for (auto& [serial, config] : Config.deviceSettings.devices)
	{
		if (config.role == vr::TrackedDeviceClass_Controller && config.activatedThisSession)
		{
			activeControllerSerials.push_back(serial);
		}
	}
	std::sort(activeControllerSerials.begin(), activeControllerSerials.end());

	auto drawPreferredPossessionCombo = [&](const char* label, std::string& preferredSerial)
	{
		const char* preview = preferredSerial.empty() ? "Auto" : preferredSerial.c_str();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f
								- ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::BeginCombo(label, preview))
		{
			if (ImGui::Selectable("Auto", preferredSerial.empty()))
			{
				preferredSerial.clear();
				HOL::HandOfLesserCore::Current->syncSettings();
			}

			for (const auto& serial : activeControllerSerials)
			{
				bool selected = preferredSerial == serial;
				if (ImGui::Selectable(serial.c_str(), selected))
				{
					preferredSerial = serial;
					HOL::HandOfLesserCore::Current->syncSettings();
				}
			}

			ImGui::EndCombo();
		}
	};

	// Actual hand tracking mode switches on left side

	std::vector<std::tuple<std::string, HOL::ControllerMode>> modeRadioButtons
		= {{"Do Nothing", HOL::ControllerMode::NoControllerMode},
		   {"Emulate separate controller", HOL::ControllerMode::EmulateControllerMode},
		   {"Possess existing controller", HOL::ControllerMode::HookedControllerMode}};

	bool steamVrRuntime = HOL::state::Runtime.isSteamVR;
	int displayedControllerMode = HOL::Config.handPose.controllerMode;
	if (steamVrRuntime && displayedControllerMode == HOL::ControllerMode::HookedControllerMode)
	{
		displayedControllerMode = HOL::ControllerMode::NoControllerMode;
	}

	ImGui::BeginChild("HandTrackingModeLeft",
					  ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0),
					  ImGuiChildFlags_AutoResizeY);

	for (const auto& buttonContent : modeRadioButtons)
	{
		const HOL::ControllerMode buttonMode = std::get<1>(buttonContent);
		const bool modeDisabled
			= steamVrRuntime && buttonMode == HOL::ControllerMode::HookedControllerMode;
		if (modeDisabled)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::RadioButton(std::get<0>(buttonContent).c_str(),
							   &displayedControllerMode,
							   buttonMode))
		{
			HOL::Config.handPose.controllerMode
				= static_cast<HOL::ControllerMode>(displayedControllerMode);
			HOL::HandOfLesserCore::Current->syncSettings();
		}

		if (modeDisabled)
		{
			ImGui::EndDisabled();
		}

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			if (modeDisabled)
			{
				showWrappedTooltip("SteamVR hand-tracking controllers use a nonstandard input "
								   "profile. Use a separate emulated controller for normal inputs.");
			}
			else
			{
				switch (buttonMode)
				{
					case HOL::ControllerMode::NoControllerMode:
						showWrappedTooltip("Do nothing.");
						break;
					case HOL::ControllerMode::EmulateControllerMode:
						showWrappedTooltip("Emulate a brand new controller.");
						break;
					case HOL::ControllerMode::HookedControllerMode:
						showWrappedTooltip("Take control of existing controllers.");
						break;
					default:
						break;
				}
			}
		}
	}

	ImGui::EndChild();
	ImGui::SameLine();

	// Which controllers to possess selection on right side

	ImGui::BeginChild("HandTrackingModeRight", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);

	bool hookedModeActive
		= HOL::Config.handPose.controllerMode == HOL::ControllerMode::HookedControllerMode;
	const bool possessionLockedBySteamVR = steamVrRuntime;

	int possessionBehavior = Config.handPose.possessionBehavior;

	ImGui::BeginDisabled(!hookedModeActive || steamVrRuntime);

	drawPreferredPossessionCombo("Possess Left##PreferredPossessLeft",
								 Config.deviceSettings.preferredLeftControllerSerial);
	drawPreferredPossessionCombo("Possess Right##PreferredPossessRight",
								 Config.deviceSettings.preferredRightControllerSerial);

	ImGui::EndDisabled();

	ImGui::BeginDisabled(!hookedModeActive || possessionLockedBySteamVR);

	ImVec2 possessionGroupStart = ImGui::GetCursorScreenPos();
	ImGui::TextUnformatted("Possession");
	if (ImGui::RadioButton("Full", &possessionBehavior, HOL::PossessionBehavior_Full))
	{
		Config.handPose.possessionBehavior
			= static_cast<HOL::PossessionBehavior>(possessionBehavior);
		HOL::HandOfLesserCore::Current->syncSettings();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		if (!possessionLockedBySteamVR)
		{
			showWrappedTooltip("Replace controller pose and input while hand tracking is primary.");
		}
	}

	if (ImGui::RadioButton("Fallback", &possessionBehavior, HOL::PossessionBehavior_Fallback))
	{
		Config.handPose.possessionBehavior
			= static_cast<HOL::PossessionBehavior>(possessionBehavior);
		HOL::HandOfLesserCore::Current->syncSettings();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		if (!possessionLockedBySteamVR)
		{
			showWrappedTooltip("Only possess if possessed device is not tracking.");
		}
	}

	ImVec2 possessionGroupEnd = ImGui::GetItemRectMax();
	ImGui::EndDisabled();

	if (possessionLockedBySteamVR
		&& ImGui::IsMouseHoveringRect(possessionGroupStart, possessionGroupEnd))
	{
		showWrappedTooltip("Possession is unavailable with the SteamVR runtime. Use a separate "
						   "emulated controller for normal inputs.");
	}

	ImGui::EndChild();

	///////////////////////////
	// Skeletal tracking level
	///////////////////////////

	ImGui::SeparatorText("Skeletal Tracking Level");

	bool hookedTrackingLevelConfigurable = display::DriverStatus.hasNormalControllers
		&& display::DriverStatus.hasHandTrackingControllers;
	bool trackingLevelLocked
		= connected && HOL::Config.handPose.controllerMode == HOL::ControllerMode::HookedControllerMode
		  && !hookedTrackingLevelConfigurable;

	ImVec2 trackingLevelStart = ImGui::GetCursorScreenPos();

	if (trackingLevelLocked)
	{
		ImGui::BeginDisabled();
	}

	ImGui::BeginGroup();

	int skeletalTrackingLevel = HOL::Config.skeletal.trackingLevel;
	if (trackingLevelLocked)
	{
		skeletalTrackingLevel = display::DriverStatus.hasHandTrackingControllers
			? vr::VRSkeletalTracking_Full
			: vr::VRSkeletalTracking_Partial;
	}

	bool updateSkeletalTrackingLevel = false;
	updateSkeletalTrackingLevel |= ImGui::RadioButton(
		"Partial", &skeletalTrackingLevel, vr::VRSkeletalTracking_Partial);
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Expose skeletal input as partial finger tracking only.");
	}
	updateSkeletalTrackingLevel |= ImGui::RadioButton(
		"Full", &skeletalTrackingLevel, vr::VRSkeletalTracking_Full);
	if (ImGui::IsItemHovered())
	{
		showWrappedTooltip("Expose skeletal input as full hand tracking.");
	}

	ImGui::EndGroup();

	if (trackingLevelLocked)
	{
		ImVec2 trackingLevelEnd = ImGui::GetItemRectMax();
		ImGui::EndDisabled();

		if (ImGui::IsMouseHoveringRect(trackingLevelStart, trackingLevelEnd))
		{
			showWrappedTooltip("Possess mode can only switch tracking level when both partial and "
							   "full controllers are present.");
		}
	}

	if (updateSkeletalTrackingLevel)
	{
		HOL::Config.skeletal.trackingLevel
			= static_cast<vr::EVRSkeletalTrackingLevel>(skeletalTrackingLevel);
		HOL::HandOfLesserCore::Current->syncSettings();
	}

	if (HOL::Config.handPose.controllerMode == HOL::ControllerMode::EmulateControllerMode)
	{
		ImGui::SeparatorText("Emulated controller profile");

		if (ImGui::RadioButton("Index",
							   (int*)&HOL::Config.handPose.emulatedControllerProfile,
							   HOL::EmulatedControllerProfile::EmulatedControllerProfile_Index))
		{
			HOL::HandOfLesserCore::Current->syncSettings();
		}

		if (ImGui::RadioButton(
				"Oculus Touch",
				(int*)&HOL::Config.handPose.emulatedControllerProfile,
				HOL::EmulatedControllerProfile::EmulatedControllerProfile_OculusTouch))
		{
			HOL::HandOfLesserCore::Current->syncSettings();
		}
	}

	/////////////////
	// Tracking features (Oculus only)
	/////////////////

	ImGui::SeparatorText("Tracking features");

	if (!HOL::state::Runtime.isOVR)
	{
		ImGui::BeginDisabled();
	}

	ImGui::BeginGroup();

	if (ImGui::Checkbox("Upper body tracking", &Config.trackingFeatures.enableUpperBodyTracking))
	{
		HOL::HandOfLesserCore::Current->featuresManager.applyTrackingFeatures(
			Config.trackingFeatures.enableUpperBodyTracking,
			Config.trackingFeatures.enableSimultaneousTracking);
	}

	if (ImGui::Checkbox("Simultaneous hand and controller tracking",
						&Config.trackingFeatures.enableSimultaneousTracking))
	{
		HOL::HandOfLesserCore::Current->featuresManager.applyTrackingFeatures(
			Config.trackingFeatures.enableUpperBodyTracking,
			Config.trackingFeatures.enableSimultaneousTracking);
	}
	ImGui::SameLine();

	if (!Config.trackingFeatures.enableSimultaneousTracking)
	{
		ImGui::BeginDisabled();
	}

	if (ImGui::Checkbox("Force hand primary",
						&Config.trackingFeatures.forceMultimodalHandPrimary))
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}

	if (!Config.trackingFeatures.enableSimultaneousTracking)
	{
		ImGui::EndDisabled();
	}

	// Disable (grey out) augment checkbox when simultaneous tracking not enabled
	if (!Config.trackingFeatures.enableSimultaneousTracking)
		ImGui::BeginDisabled();

	if (ImGui::Checkbox("Augment controller skeleton with hand tracking",
						&Config.skeletal.augmentControllerSkeleton))
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}

	if (!Config.trackingFeatures.enableSimultaneousTracking)
		ImGui::EndDisabled();

	ImGui::EndGroup();

	if (!HOL::state::Runtime.isOVR)
	{
		ImGui::EndDisabled();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			showWrappedTooltip("These features are only available when using the Oculus runtime.");
		}
	}
	ImGui::EndChild();
}

void HOL::UserInterface::buildTracking()
{
	ImGui::BeginChild(
		"TrackingWindow", ImVec2(scaleSize(PanelWidth), 0), ImGuiChildFlags_AutoResizeY);

	ImGui::SeparatorText("Main");

	if (ImGui::Checkbox("Apply base offset", &Config.handPose.applyBaseOffset))
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}
	ImGui::Checkbox("Force inactive", &Config.handPose.forceInactive);

	ImGui::SeparatorText("Offset preset");

	if (ImGui::Button("Zero"))
	{
		HOL::settings::restoreDefaultControllerOffset(HOL::ZERO);
		HOL::HandOfLesserCore::Current->syncSettings();
	}

	ImGui::SameLine();

	if (ImGui::Button("Roughy VRC"))
	{
		HOL::settings::restoreDefaultControllerOffset(HOL::RoughyVRChatHand);
		HOL::HandOfLesserCore::Current->syncSettings();
	}

	ImGui::BeginChild("TrackingTranslationInput",
					  ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0),
					  ImGuiChildFlags_AutoResizeY);

	bool offsetChanged = false;
	ImGui::SeparatorText("Translation");

	auto& positionOffset = Config.handPose.positionOffset;
	offsetChanged |= ImGui::InputFloat("posX", &positionOffset.x(), 0.001f, 0.01f, "%.3f");
	offsetChanged |= ImGui::InputFloat("posY", &positionOffset.y(), 0.001f, 0.01f, "%.3f");
	offsetChanged |= ImGui::InputFloat("posZ", &positionOffset.z(), 0.001f, 0.01f, "%.3f");

	ImGui::EndChild();
	ImGui::SameLine();

	ImGui::BeginChild("TrackingOrientationInput", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);

	ImGui::SeparatorText("Orientation");

	auto& orientationOffset = Config.handPose.orientationOffset;
	offsetChanged |= ImGui::InputFloat("rotX", &orientationOffset.x(), 1.0f, 5.0f, "%.3f");
	offsetChanged |= ImGui::InputFloat("rotY", &orientationOffset.y(), 1.0f, 5.0f, "%.3f");
	offsetChanged |= ImGui::InputFloat("rotZ", &orientationOffset.z(), 1.0f, 5.0f, "%.3f");

	ImGui::EndChild();

	if (offsetChanged)
	{
		HOL::HandOfLesserCore::Current->syncSettings();
	}

	buildSingleHandTransformDisplay(HOL::LeftHand);
	ImGui::SameLine();
	buildSingleHandTransformDisplay(HOL::RightHand);

	ImGui::SeparatorText("Body tracking");

	float bodyTrackingConfidence = HOL::display::BodyTracking.confidence;
	bool hasBodyTrackingEstimate = bodyTrackingConfidence > 0.0f;

	ImGui::Text("Confidence:");
	ImGui::SameLine();
	if (hasBodyTrackingEstimate)
	{
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%.3f", bodyTrackingConfidence);
	}
	else
	{
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
						   "%.3f - Unavailable",
						   bodyTrackingConfidence);
	}
	if (ImGui::IsItemHovered())
	{
		if (!hasBodyTrackingEstimate)
		{
			showWrappedTooltip(
				"Body tracking data is unavailable. Upper body tracking, simultaneous "
				"hand/controller tracking, and non-hand joystick reference modes all rely on "
				"body tracking data being available here.");
		}
		else
		{
			showWrappedTooltip("Upper body tracking, multimodal controller detection, and non-hand "
							   "joystick reference modes all rely on body tracking data being "
							   "available here.");
		}
	}
	ImGui::EndChild();
}

void UserInterface::buildVRChatOSCSettings()

{
	// ImGui::BeginChild("VRChatSettings",
	//				  ImVec2(ImGui::GetContentRegionAvail().x * 1.f, 0),
	//				  ImGuiChildFlags_AutoResizeY);

	ImGui::BeginChild(
		"VRChatSettings", ImVec2(scaleSize(PanelWidth), 0), ImGuiChildFlags_AutoResizeY);

	const HOL::settings::VRChatSettings vrchatDefaults;
	Config.vrchat.sendFull = vrchatDefaults.sendFull;
	Config.vrchat.sendAlternating = vrchatDefaults.sendAlternating;
	Config.vrchat.sendPacked = vrchatDefaults.sendPacked;
	Config.vrchat.interlacePacked = vrchatDefaults.interlacePacked;
	Config.vrchat.useUnityHumanoidSplay = vrchatDefaults.useUnityHumanoidSplay;
	Config.vrchat.alternateCurlTest = vrchatDefaults.alternateCurlTest;

	ImGui::SeparatorText("VRChat");

	ImGui::Checkbox("Send OSC", &Config.vrchat.sendOsc);
	// ImGui::Checkbox("Send full", &Config.vrchat.sendFull);
	// ImGui::SameLine();
	// ImGui::Checkbox("Send Packed", &Config.vrchat.sendPacked);

	ImGui::Checkbox("Send OSC test data", &Config.vrchat.sendDebugOsc);

	// if (ImGui::InputInt("Packed Update Interval (ms)", &Config.vrchat.packedUpdateInterval))
	// {
	// 	// Should never be anything but 100, and definitely never less
	// 	if (Config.vrchat.packedUpdateInterval < 100)
	// 	{
	// 		Config.vrchat.packedUpdateInterval = 100;
	// 	}
	// }

	ImGui::SameLine();
	if (rightAlignButton("Reset##VRChat"))
	{
		Config.vrchat = HOL::settings::VRChatSettings();
	}

	if (Config.vrchat.sendDebugOsc)
	{
		ImGui::SeparatorText("OSC test data");
		ImGui::SliderFloat("Curl", &Config.vrchat.curlDebug, -1, 1);
		ImGui::SliderFloat("Splay", &Config.vrchat.splayDebug, -1, 1);
	}

	if (Config.vrchat.useUnityHumanoidSplay)
	{
		ImGui::SeparatorText("Offsets");

		InputFloatMultipleSingleLableWithButtons("thumbRotationOffset",
												 "Thumb rotation offset",
												 1.f,
												 1.f,
												 "%.1f",
												 90,
												 {&Config.fingerBend.ThumbAxisOffset[0],
												  &Config.fingerBend.ThumbAxisOffset[1],
												  &Config.fingerBend.ThumbAxisOffset[2]});

		ImGui::SameLine();
		if (rightAlignButton("Reset##VRChat_humanoid_thumb_axis"))
		{
			Config.fingerBend.ThumbAxisOffset = HOL::settings::FingerBendSettings().ThumbAxisOffset;
		}

		ImGui::SeparatorText("Curl Center");

		InputFloatMultipleSingleLableWithButtons("commonFingerCurlCenter",
												 "Common",
												 0.5f,
												 1.f,
												 "%.1f",
												 90,
												 {&Config.fingerBend.commonCurlCenter[0],
												  &Config.fingerBend.commonCurlCenter[1],
												  &Config.fingerBend.commonCurlCenter[2]});

		InputFloatMultipleSingleLableWithButtons("thumbFingerCurlCenter",
												 "Thumb",
												 0.5f,
												 1.f,
												 "%.1f",
												 90,
												 {&Config.fingerBend.thumbCurlCenter[0],
												  &Config.fingerBend.thumbCurlCenter[1],
												  &Config.fingerBend.thumbCurlCenter[2]});

		ImGui::SameLine();
		if (rightAlignButton("Reset##VRChat_humanoid_curl"))
		{
			// Well this is cancer
			auto defValues = HOL::settings::FingerBendSettings();
			std::copy(std::begin(defValues.commonCurlCenter),
					  std::end(defValues.commonCurlCenter),
					  std::begin(Config.fingerBend.commonCurlCenter));

			std::copy(std::begin(defValues.thumbCurlCenter),
					  std::end(defValues.thumbCurlCenter),
					  std::begin(Config.fingerBend.thumbCurlCenter));
		}

		ImGui::SeparatorText("Splay Center");

		InputFloatMultipleTopLableWithButtons("fingerSplayCenter",
											  {"Index", "Middle", "Ring", "Little", "Thumb"},
											  0.5f,
											  1.f,
											  "%.1f",
											  90,
											  {&Config.fingerBend.fingerSplayCenter[0],
											   &Config.fingerBend.fingerSplayCenter[1],
											   &Config.fingerBend.fingerSplayCenter[2],
											   &Config.fingerBend.fingerSplayCenter[3],
											   &Config.fingerBend.fingerSplayCenter[4]});

		// This section has 2-line groups so we need to compensate
		ImGui::SameLine();
		if (rightAlignButton("Reset##VRChat_humanoid_splay", 1))
		{
			// still cancer
			auto defValues = HOL::settings::FingerBendSettings();
			std::copy(std::begin(defValues.fingerSplayCenter),
					  std::end(defValues.fingerSplayCenter),
					  std::begin(Config.fingerBend.fingerSplayCenter));
		}
	}
	else
	{
		ImGui::SeparatorText("Curl Offset");

		InputFloatMultipleSingleLableWithButtons("Common curl offset",
												 "Common",
												 0.5f,
												 1.f,
												 "%.1f",
												 90,
												 {&Config.skeletalBend.commonCurlCenter[0],
												  &Config.skeletalBend.commonCurlCenter[1],
												  &Config.skeletalBend.commonCurlCenter[2]});

		InputFloatMultipleSingleLableWithButtons("Thumb curl offset",
												 "Thumb",
												 0.5f,
												 1.f,
												 "%.1f",
												 90,
												 {&Config.skeletalBend.thumbCurlCenter[0],
												  &Config.skeletalBend.thumbCurlCenter[1],
												  &Config.skeletalBend.thumbCurlCenter[2]});

		ImGui::SeparatorText("Splay Offset");

		InputFloatMultipleTopLableWithButtons("Splay offset",
											  {"Index", "Middle", "Ring", "Little", "Thumb"},
											  0.5f,
											  1.f,
											  "%.1f",
											  90,
											  {&Config.skeletalBend.fingerSplayCenter[0],
											   &Config.skeletalBend.fingerSplayCenter[1],
											   &Config.skeletalBend.fingerSplayCenter[2],
											   &Config.skeletalBend.fingerSplayCenter[3],
											   &Config.skeletalBend.fingerSplayCenter[4]});
	}

	// Raw
	ImGui::SeparatorText("Raw bend");
	buildSingleFingerTrackingDisplay("rawBendLeft",
									 HOL::LeftHand,
									 HOL::display::FingerTracking[HOL::LeftHand].rawBend,
									 false,
									 true);
	ImGui::SameLine();
	buildSingleFingerTrackingDisplay("rawBendRight",
									 HOL::RightHand,
									 HOL::display::FingerTracking[HOL::RightHand].rawBend,
									 false,
									 true);

	// Humanoid
	ImGui::SeparatorText("Humanoid bend");
	buildSingleFingerTrackingDisplay("humanoidBendLeft",
									 HOL::LeftHand,
									 HOL::display::FingerTracking[HOL::LeftHand].humanoidBend,
									 false,
									 false);
	ImGui::SameLine();
	buildSingleFingerTrackingDisplay("humanoidBendRight",
									 HOL::RightHand,
									 HOL::display::FingerTracking[HOL::RightHand].humanoidBend,
									 false,
									 false);

	// Encoded, 0-255 in left hand, -1 to +1 that we'll be sending to vrchat in right hand
	ImGui::SeparatorText("Packed bend");
	buildSingleFingerTrackingDisplay("packedBendBothHandsRaw",
									 HOL::LeftHand,
									 HOL::display::FingerTracking[HOL::LeftHand].packedBend,
									 true,
									 false);

	ImGui::EndChild();
}

void UserInterface::buildInterface()
{
	ImGui::SetNextWindowPos(ImVec2(0, 0));

	auto io = ImGui::GetIO();
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));

	ImGui::Begin("HandOfLesser", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
	bool visualTabOpen = false;

	ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
	if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags))
	{
		if (ImGui::BeginTabItem("Main"))
		{
			buildMain();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Hands"))
		{
			buildTracking();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("SteamVR"))
		{
			buildSteamVR();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("VRChat"))
		{
			buildVRChatOSCSettings();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Input"))
		{
			buildBindings();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Visual"))
		{
			visualTabOpen = true;
			buildVisual();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Body Trackers"))
		{
			buildBodyTrackers();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Touch"))
		{
			buildControllerInput();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Misc"))
		{
			buildMisc();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("About"))
		{
			buildAbout();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	this->mVisualizer.setActive(visualTabOpen);

	ImGui::End();
}

void UserInterface::buildAbout()
{
	ImGui::SeparatorText("HandOfLesser");
	ImGui::Text("Version: %s (%s)", HOL_VERSION_STRING, HOL_BUILD_COMMIT);
	ImGui::TextUnformatted("Author: Roughy");
	ImGui::TextUnformatted("License: MIT");

	ImGui::Spacing();
	buildUrlLine("GitHub:", "https://github.com/Nordskog/HandOfLesser");
	buildUrlLine("Discord:", "https://discord.gg/k9QNcvvJmF");
}

Visualizer* HOL::UserInterface::getVisualizer()
{
	return &this->mVisualizer;
}
