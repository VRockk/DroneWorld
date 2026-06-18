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

float DroneInput::ShapeAxis(float Raw, const FDroneRates& Rates)
{
	// The deadzone + expo curve sets the shape; the rate scales it, so sensitivity is a per-preset trait
	// layered on top of the same curve every drone shares.
	return Rates.RateScale * ShapeAxis(Raw, Rates.Deadzone, Rates.Expo);
}

FDroneControlIntent DroneInput::GateMotors(const FDroneControlIntent& Intent)
{
	if (Intent.bArmed)
	{
		return Intent;
	}

	// Disarmed: keep the mode flags but cut everything the motors act on, so a disarmed drone makes no
	// thrust and no commanded rotation no matter what the pilot does with the sticks. The flight model
	// still applies gravity and drag, so the drone simply sits or falls rather than flying.
	FDroneControlIntent Gated = Intent;
	Gated.Throttle = 0.f;
	Gated.Yaw = 0.f;
	Gated.Pitch = 0.f;
	Gated.Roll = 0.f;
	return Gated;
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
