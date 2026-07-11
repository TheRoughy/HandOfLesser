#include "InstanceHolder.h"

#include <iostream>
#include "XrUtils.h"
#include <thread>
#include "src/core/state_global.h"
#include "src/core/HandOfLesserCore.h"
#include "src/windows/windows_utils.h"
#include <meta_body_tracking_fidelity.h>

using namespace HOL::OpenXR;

InstanceHolder::InstanceHolder()
{
	this->mState = OpenXrState::Uninitialized;
	this->mCallback = nullptr;
}

void InstanceHolder::init()
{
	this->mDispatcher = xr::DispatchLoaderDynamic{};

	try
	{
		this->enumerateLayers();
		this->enumerateExtensions();
		this->initExtensions();

		this->initInstance();
		this->getSystemProperties();
		this->initSession();
		this->initSpaces();
	}
	catch (std::exception exp)
	{
		std::cerr << exp.what() << std::endl;
		std::cerr << "Failed to initialize OpenXR" << std::endl;
		this->updateState(OpenXrState::Failed);
		return;
	}

	this->updateState(OpenXrState::Initialized);
}

void InstanceHolder::initInstance()
{
	// We don't enable any
	std::vector<const char*> enabledLayers;

	// The extensions we rely on work fine with OpenXR 1.0, and some runtimes
	// reject newer requested core API versions.
	xr::Version openXRVersion = xr::Version(1, 0, 0);

	this->mInstance = xr::createInstanceUnique(
		xr::InstanceCreateInfo{
			xr::InstanceCreateFlagBits::None,
			xr::ApplicationInfo{"HandOfLesser",
								1, // app version
								"HandOfLesserStandalone",
								0, // engine version
								openXRVersion},
			uint32_t(enabledLayers.size()),
			enabledLayers.data(),
			uint32_t(this->mEnabledExtensions.size()),
			this->mEnabledExtensions.data(),
		},
		this->mDispatcher);

	// Update the dispatch now that we have an instance
	this->mDispatcher = xr::DispatchLoaderDynamic::createFullyPopulated(this->mInstance.get(),
																		&::xrGetInstanceProcAddr);

	pollEvent();
}

void InstanceHolder::initSession()
{
	this->mSystemId = this->mInstance->getSystem(
		xr::SystemGetInfo{xr::FormFactor::HeadMountedDisplay}, this->mDispatcher);

	XrGraphicsBindingD3D11KHR D3D11Binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
	XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};
	createInfo.systemId = this->mSystemId.get();

	// Non-headless requires that we at least pretend we're going to display stuff
	if (!this->isHeadless())
	{
		auto requirements
			= this->mInstance->getD3D11GraphicsRequirementsKHR(this->mSystemId, this->mDispatcher);

		ID3D11Device* Device = nullptr;
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		HRESULT result = D3D11CreateDevice(NULL,
											 D3D_DRIVER_TYPE_HARDWARE,
											 NULL,
											 0,
											 NULL,
											 0,
											 D3D11_SDK_VERSION,
											 &Device,
											 &featureLevel,
											 NULL);
		if (FAILED(result) || Device == nullptr)
		{
			throw std::runtime_error("Failed to create D3D11 device for OpenXR foreground session: "
									 + FormatWindowsError(static_cast<DWORD>(result)));
		}

		if (featureLevel < requirements.minFeatureLevel)
		{
			Device->Release();
			throw std::runtime_error(
				"D3D11 device does not meet OpenXR runtime minimum feature level");
		}

		D3D11Binding.device = Device;
		createInfo.next = &D3D11Binding;
	}

	XrSession rawSession = XR_NULL_HANDLE;
	if (!handleXR("xrCreateSession call",
				  xrCreateSession(this->mInstance.get(), &createInfo, &rawSession)))
	{
		throw std::runtime_error("Failed to create OpenXR session");
	}

	this->mSession = xr::UniqueDynamicSession(
		xr::Session(rawSession), xr::ObjectDestroy<xr::DispatchLoaderDynamic>{this->mDispatcher});

	pollEvent();

	if (this->fullForegroundMode())
	{
		setupForegroundRendering();
	}
}

void InstanceHolder::doForegroundRendering(XrFrameState frameState)
{
	XrCompositionLayerProjectionView projectionViews[2] = {
		{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
	XrCompositionLayerProjection projectionLayer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	XrCompositionLayerBaseHeader* layers[] = {(XrCompositionLayerBaseHeader*)&projectionLayer};
	XrViewLocateInfo locateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.displayTime = frameState.predictedDisplayTime;
	locateInfo.space = this->mStageSpace.get();
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	XrViewState viewState = {XR_TYPE_VIEW_STATE};
	XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
	uint32_t viewCount;
	handleXR("xrLocateViews call",
			 xrLocateViews(this->mSession.get(), &locateInfo, &viewState, 2, &viewCount, views));
}

void InstanceHolder::initSpaces()
{
	this->mLocalSpace = this->mSession->createReferenceSpaceUnique(
		xr::ReferenceSpaceCreateInfo{xr::ReferenceSpaceType::Local, xr::Posef{}},
		this->mDispatcher);

	mStageSpace = this->mSession->createReferenceSpaceUnique(
		xr::ReferenceSpaceCreateInfo{xr::ReferenceSpaceType::Stage, xr::Posef{}},
		this->mDispatcher);
}

void HOL::OpenXR::InstanceHolder::getSystemProperties()
{
	XrSystemSimultaneousHandsAndControllersPropertiesMETA modalProperties{
		XR_TYPE_SYSTEM_SIMULTANEOUS_HANDS_AND_CONTROLLERS_PROPERTIES_META};

	xr::SystemProperties systemProperties;
	systemProperties.type = xr::StructureType::SystemProperties;
	systemProperties.next = &modalProperties;

	this->mInstance->getSystemProperties(this->mSystemId, systemProperties, this->mDispatcher);

	// META lies
	std::cout << "Supported multimodal: " << modalProperties.supportsSimultaneousHandsAndControllers
			  << "\n";
}

bool InstanceHolder::getHmdPose(XrSpace space, XrTime time, HOL::PoseLocation& pose)
{
	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = time;
	locateInfo.space = space;

	XrViewState viewState{XR_TYPE_VIEW_STATE};
	XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
	uint32_t viewCount = 0;

	if (!handleXR("xrLocateViews HMD pose",
				  xrLocateViews(this->mSession.get(), &locateInfo, &viewState, 2, &viewCount, views)))
	{
		return false;
	}

	const bool positionValid = (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
	const bool orientationValid
		= (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
	if (viewCount == 0 || !positionValid || !orientationValid)
	{
		return false;
	}

	Eigen::Vector3f position = Eigen::Vector3f::Zero();
	for (uint32_t i = 0; i < viewCount; i++)
	{
		position += HOL::OpenXR::toEigenVector(views[i].pose.position);
	}

	pose.position = position / static_cast<float>(viewCount);
	pose.orientation = HOL::OpenXR::toEigenQuaternion(views[0].pose.orientation);
	return true;
}

void InstanceHolder::pollEvent()
{
	pollEventInternal(this->mInstance.get(), this->mDispatcher);
}

void InstanceHolder::endSession()
{
	if (mState == OpenXrState::Running)
	{
		this->mSession->requestExitSession(this->mDispatcher);
		this->mSession->endSession(this->mDispatcher);
		this->updateState(OpenXrState::Exited);
	}
}

void InstanceHolder::beginSession()
{

	// Ignored if headless
	auto viewConfigType = xr::ViewConfigurationType::PrimaryStereo;

	try
	{
		// Begin session
		this->mSession->beginSession({viewConfigType}, this->mDispatcher);
	}
	catch (std::exception exp)
	{
		std::cerr << exp.what() << std::endl;
		std::cerr << "Failed to begin OpenXR session" << std::endl;
		this->updateState(OpenXrState::Failed);
		return;
	}

	this->pollEvent();
	this->updateState(OpenXrState::Running);
}

void InstanceHolder::enumerateLayers()
{
	this->mLayers = xr::enumerateApiLayerPropertiesToVector(this->mDispatcher);
	std::cout << "Enumerating layers:" << std::endl;
	std::cout << "Number of layers: " << this->mLayers.size() << std::endl;
	for (const auto& prop : this->mLayers)
	{
		std::cout << prop.layerName << " - " << prop.description << std::endl;
	}
}

void InstanceHolder::enumerateExtensions()
{
	// enumerateInstanceExtensionPropertiesToVector() may get stuck if the HMD is not connected
	// properly Observed with airlink, probably means you've been booted back to the menu
	this->mExtensions
		= xr::enumerateInstanceExtensionPropertiesToVector(nullptr, this->mDispatcher);
	std::cout << "Enumerating extensions:" << std::endl;
	std::cout << "Number of extensions: " << this->mExtensions.size() << std::endl;
	for (auto& extension : this->mExtensions)
	{
		std::cout << extension.extensionName << std::endl;
	}
}

void InstanceHolder::initExtensions()
{
	state::Runtime.supportsBodyTracking
		= hasExtension(this->mExtensions, XR_FB_BODY_TRACKING_EXTENSION_NAME);
	state::Runtime.supportsHandTrackingAim
		= hasExtension(this->mExtensions, XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
	state::Runtime.supportsHandTrackingDataSource
		= hasExtension(this->mExtensions, XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME);

	this->mEnabledExtensions = {XR_EXT_HAND_TRACKING_EXTENSION_NAME,
		XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME, // Used to get current time.
		XR_KHR_D3D11_ENABLE_EXTENSION_NAME};

	if (state::Runtime.supportsBodyTracking)
	{
		this->mEnabledExtensions.push_back(XR_FB_BODY_TRACKING_EXTENSION_NAME);
	}

	if (hasExtension(this->mExtensions, XR_META_BODY_TRACKING_FIDELITY_EXTENSION_NAME))
	{
		this->mEnabledExtensions.push_back(XR_META_BODY_TRACKING_FIDELITY_EXTENSION_NAME);
	}

	if (hasExtension(this->mExtensions, XR_META_HEADSET_ID_EXTENSION_NAME))
	{
		this->mEnabledExtensions.push_back(XR_META_HEADSET_ID_EXTENSION_NAME);
	}

	if (hasExtension(this->mExtensions, XR_META_SIMULTANEOUS_HANDS_AND_CONTROLLERS_EXTENSION_NAME))
	{
		this->mEnabledExtensions.push_back(
			XR_META_SIMULTANEOUS_HANDS_AND_CONTROLLERS_EXTENSION_NAME);
	}

	// We use aim states to get the hand-tracked menu gesture.
	if (state::Runtime.supportsHandTrackingAim)
	{
		this->mEnabledExtensions.push_back(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
	}

	// If the runtime exposes hand-tracking data source, always enable it so we can
	// inspect where hand data came from and reject controller-fed hand poses.
	if (state::Runtime.supportsHandTrackingDataSource)
	{
		this->mEnabledExtensions.push_back(XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME);
	}

	if (hasExtension(this->mExtensions, XR_MND_HEADLESS_EXTENSION_NAME))
	{
		std::cout << "Runtime supports headless extension, running in headless mode" << std::endl;
		this->mEnabledExtensions.push_back(XR_MND_HEADLESS_EXTENSION_NAME);
		this->mHeadless = true;
	}
	else
	{
		std::cout << "Runtime does not support headless extension, running as foreground app"
				  << std::endl;
		this->mHeadless = false;
	}

	for (auto& extension : this->mEnabledExtensions)
	{
		if (!hasExtension(this->mExtensions, extension))
		{
			std::cout << "Missing required extensions: " << extension << std::endl;
		}
	}
}

void HOL::OpenXR::InstanceHolder::getHmdPosition()
{
	// TODO return actual value. Doesn't work in VD at least.

	XrViewLocateInfo view_locate_info
		= {.type = XR_TYPE_VIEW_LOCATE_INFO,
		   .next = NULL,
		   .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		   .displayTime = getTime(),
		   .space = this->mStageSpace.get()};

	// TODO: use actual view count
	uint32_t view_count = 2;
	std::vector<XrView> views(view_count);
	for (uint32_t i = 0; i < view_count; i++)
	{
		views[i].type = XR_TYPE_VIEW;
		views[i].next = NULL;
	};

	XrViewState view_state = {.type = XR_TYPE_VIEW_STATE, .next = NULL};
	auto result = xrLocateViews(this->mSession->get(),
								&view_locate_info,
								&view_state,
								view_count,
								&view_count,
								views.data());
	if (handleXR("XrLocateViews", result))
	{
		printf("X: %.3f, Y: %.3f, Z: %.3f\n",
			   views[0].pose.position.x,
			   views[0].pose.position.y,
			   views[0].pose.position.z);

		printf("X: %.3f, Y: %.3f, Z: %.3f\n",
			   views[1].pose.position.x,
			   views[1].pose.position.y,
			   views[1].pose.position.z);
	}
}

void HOL::OpenXR::InstanceHolder::setupForegroundRendering()
{
	///////////////
	// Session
	//////////////

	// Get the viewport configuration info for the chosen viewport configuration type.
	// App only supports the primary stereo view config.
	const XrViewConfigurationType supportedViewConfigType
		= XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	// Enumerate the viewport configurations.
	{
		uint32_t viewportConfigTypeCount = 0;
		handleXR(
			"xrEnumerateViewConfigurations call",
			xrEnumerateViewConfigurations(
				this->mInstance.get(), this->mSystemId.get(), 0, &viewportConfigTypeCount, NULL));

		std::vector<XrViewConfigurationType> viewportConfigurationTypes(viewportConfigTypeCount);

		handleXR("xrEnumerateViewConfigurations call",
				 xrEnumerateViewConfigurations(this->mInstance.get(),
											   this->mSystemId.get(),
											   viewportConfigTypeCount,
											   &viewportConfigTypeCount,
											   viewportConfigurationTypes.data()));

		for (uint32_t i = 0; i < viewportConfigTypeCount; i++)
		{
			const XrViewConfigurationType viewportConfigType = viewportConfigurationTypes[i];
			XrViewConfigurationProperties viewportConfig{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
			handleXR("xrGetViewConfigurationProperties call",
					 xrGetViewConfigurationProperties(this->mInstance.get(),
													  this->mSystemId.get(),
													  viewportConfigType,
													  &viewportConfig));
			uint32_t viewCount;
			handleXR("xrEnumerateViewConfigurationViews call",
					 xrEnumerateViewConfigurationViews(this->mInstance.get(),
													   this->mSystemId.get(),
													   viewportConfigType,
													   0,
													   &viewCount,
													   NULL));

			if (viewCount > 0)
			{
				std::vector<XrViewConfigurationView> elements(viewCount,
															  {XR_TYPE_VIEW_CONFIGURATION_VIEW});

				handleXR("xrEnumerateViewConfigurationViews call",
						 xrEnumerateViewConfigurationViews(this->mInstance.get(),
														   this->mSystemId.get(),
														   viewportConfigType,
														   viewCount,
														   &viewCount,
														   elements.data()));

				// Cache the view config properties for the selected config type.
				if (viewportConfigType == supportedViewConfigType)
				{
					// assert(viewCount == 2);
					for (uint32_t e = 0; e < viewCount; e++)
					{
						mViewConfigurationView[e] = elements[e];
					}
				}
			}
			else
			{
				printf("Empty viewport configuration type: %d", viewCount);
			}
		}
	}

	// Get the viewport configuration info for the chosen viewport configuration type.
	handleXR("xrGetViewConfigurationProperties call",
			 xrGetViewConfigurationProperties(this->mInstance.get(),
											  this->mSystemId.get(),
											  supportedViewConfigType,
											  &this->mViewportConfiguration));
}

bool HOL::OpenXR::InstanceHolder::fullForegroundMode()
{
	// Set to true to run in foreground when headless mode is not available.
	// Otherwise we will never enter focused mode and must patch oculus dll to get data.
	return !this->isHeadless() && false;
}

void InstanceHolder::setCallback(XrEventsInterface* callback)
{
	this->mCallback = callback;
}

XrTime InstanceHolder::getTime()
{
	return getXrTimeNow(this->mInstance.get(), this->mDispatcher).get();
}

void InstanceHolder::updateState(OpenXrState newState)
{
	this->mState = newState;
	HOL::state::Runtime.openxrState = newState;
	HandOfLesserCore::Current->syncState();
}

OpenXrState InstanceHolder::getState()
{
	return this->mState;
}

bool HOL::OpenXR::InstanceHolder::isHeadless()
{
	return this->mHeadless;
}

int InstanceHolder::foregroundRender()
{
	xr::Time time = getXrTimeNow(this->mInstance.get(), this->mDispatcher);

	if (this->mSessionState != xr::SessionState::Stopping)
	{
		XrFrameWaitInfo waitFrameInfo = {XR_TYPE_FRAME_WAIT_INFO};
		XrFrameState frameState = {XR_TYPE_FRAME_STATE};
		handleXR("xrWaitFrame call",
				 xrWaitFrame(this->mSession.get(), &waitFrameInfo, &frameState));

		////////////////
		// Begin frame
		////////////////

		XrFrameBeginInfo beginFrameDesc = {XR_TYPE_FRAME_BEGIN_INFO};
		handleXR("xrBeginFrame call", (xrBeginFrame(this->mSession.get(), &beginFrameDesc)));

		doForegroundRendering(frameState);

		///////////////////
		// Frame stuff
		//////////////////////

		XrFrameEndInfo endFrameInfo = {XR_TYPE_FRAME_END_INFO};
		endFrameInfo.displayTime = frameState.predictedDisplayTime;
		endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		endFrameInfo.layerCount = 0;
		endFrameInfo.layers = nullptr;

		handleXR("xrEndFrame call", xrEndFrame(this->mSession.get(), &endFrameInfo));
	}

	return 0;
}
