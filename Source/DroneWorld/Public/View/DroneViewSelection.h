#pragma once

#include "CoreMinimal.h"

namespace DroneView
{
	// Which actor a pilot views the world through. OnboardDirect is the drone's own onboard camera,
	// rendered straight to the screen - the flatscreen experience, with no capture and no Pilot Station.
	// VrPilotStation is the VR experience: the onboard camera is captured to a Feed shown
	// on a screen inside the Void, and that station is the view target while the drone pawn is possessed.
	enum class EDroneViewMode : uint8
	{
		OnboardDirect,
		VrPilotStation
	};

	// The view setup a controller chooses at possession time. A VR Pilot Station is stood up only for the
	// locally-controlled human pilot with an HMD present; every other case - a flatscreen pilot, an AI
	// drone, a non-local pawn - views the onboard camera directly, so the Void and Feed capture are never
	// paid for them. Plain booleans in, mode out, so the policy is testable without a live controller, HMD,
	// or world.
	DRONEWORLD_API EDroneViewMode SelectViewMode(bool bIsLocallyControlled, bool bHmdEnabled);
}
