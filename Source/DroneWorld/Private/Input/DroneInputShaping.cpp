#include "Input/DroneInputShaping.h"

float DroneInput::ShapeAxis(float Raw, float Deadzone, float Expo)
{
	const float Magnitude = FMath::Abs(Raw);
	if (Magnitude <= Deadzone)
	{
		return 0.f;
	}

	// Rescale the travel beyond the deadzone back to a full [0, 1] range so the stick still reaches 1.
	const float Rescaled = (Magnitude - Deadzone) / FMath::Max(1.f - Deadzone, KINDA_SMALL_NUMBER);
	const float Clamped = FMath::Clamp(Rescaled, 0.f, 1.f);

	const float Shaped = (1.f - Expo) * Clamped + Expo * Clamped * Clamped * Clamped;
	return FMath::Sign(Raw) * Shaped;
}

FDroneControlIntent DroneInput::MapMode2(float LeftX, float LeftY, float RightX, float RightY)
{
	FDroneControlIntent Intent;
	// The throttle stick rests at zero and is pushed up to add collective: the upper half of travel
	// maps to [0, 1], while a centered or downward stick is no thrust. Releasing the stick therefore
	// means no thrust, so a quad descends and a fixed-wing slows rather than holding a hidden setpoint.
	Intent.Throttle = FMath::Clamp(LeftY, 0.f, 1.f);
	Intent.Yaw = LeftX;
	Intent.Pitch = RightY;
	Intent.Roll = RightX;
	return Intent;
}
