#pragma once

#include <d3d11.h> // Needs to go before all the openxr stuff
#include <openxr/openxr_platform.h>
#include <openxr/openxr.hpp>
#include "XrEventsInterface.h"
#include "openxr_state.h"
#include <HandOfLesserCommon.h>

#include <iostream>

namespace HOL::OpenXR
{
	class InstanceHolder
	{
	public:
		InstanceHolder();
		void init();
		void beginSession();
		void pollEvent();
		void endSession();
		void setCallback(XrEventsInterface* callback);
		XrTime getTime();
		HOL::OpenXR::OpenXrState getState();
		bool isHeadless();
		bool getHmdPose(XrSpace space, XrTime time, HOL::PoseLocation& pose);

		xr::UniqueDynamicInstance mInstance;
		xr::UniqueDynamicSession mSession;
		xr::UniqueDynamicSpace mLocalSpace;
		xr::UniqueDynamicSpace mStageSpace;
		xr::DispatchLoaderDynamic mDispatcher;

		void getHmdPosition();

		// Foreground mode
		int foregroundRender();
		bool fullForegroundMode();

	private:
		bool mHeadless;
		HOL::OpenXR::OpenXrState mState;
		xr::SystemId mSystemId;

		std::vector<xr::ExtensionProperties> mExtensions;
		std::vector<xr::ApiLayerProperties> mLayers;
		std::vector<const char*> mEnabledExtensions;

		XrEventsInterface* mCallback;

		void enumerateLayers();
		void enumerateExtensions();
		void initExtensions();

		void initInstance();
		void initSession();
		void initSpaces();

		void getSystemProperties();

		// Foreground mode
		XrViewConfigurationProperties mViewportConfiguration{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
		XrViewConfigurationView mViewConfigurationView[2];

		// This is the actual state of the session, which we don't usually care about.
		// When in headless mode we either get initialized or we don't. 
		xr::SessionState mSessionState = xr::SessionState::Unknown;	
		void doForegroundRendering(XrFrameState frameState);
		void setupForegroundRendering();


		void updateState(HOL::OpenXR::OpenXrState newState);

		template <typename Dispatch> void pollEventInternal(xr::Instance instance, Dispatch&& d)
		{
			while (1)
			{
				xr::EventDataBuffer event;
				auto result = instance.pollEvent(event, d);
				if (result == xr::Result::EventUnavailable)
				{
					return;
				}
				else if (result != xr::Result::Success)
				{
					std::cerr << "Got error polling for events: " << to_string(result) << std::endl;
					return;
				}

				std::cout << "Event: " << to_string_literal(event.type) << std::endl;
				if (event.type == xr::StructureType::EventDataSessionStateChanged)
				{
					auto& change = reinterpret_cast<xr::EventDataSessionStateChanged&>(event);
					this->mSessionState = change.state;
					std::cout << to_string(change.state) << std::endl;
				}
			}
		}
	};
} // namespace HOL::OpenXR
