#include "Flight/DroneImperfection.h"

namespace
{
	// Smooth coherent noise in [-1, 1]: two slow sines, a base and an incommensurate second harmonic,
	// whose weights total one. The result wanders organically rather than jittering and never leaves
	// unit bounds, so callers can scale it by an amplitude and trust the output stays within it. Phase
	// shifts the whole waveform, so a per-drone or per-axis phase gives independent, repeatable motion.
	float CoherentNoise(float Phase, float Frequency, float TimeSeconds)
	{
		const float TwoPi = 2.f * PI;
		return 0.6f * FMath::Sin(TwoPi * Frequency * TimeSeconds + Phase)
			+ 0.4f * FMath::Sin(TwoPi * Frequency * 1.9f * TimeSeconds + Phase * 1.3f);
	}

	// A two-axis coherent-noise wander on X and Y (Z zero), each scaled to Magnitude. A seed-derived
	// phase makes each drone wander on its own repeatable path, a per-axis offset keeps X and Y from
	// marching in lockstep, and the unit-bounded noise keeps each axis within Magnitude. PhaseBias shifts
	// the whole waveform so independent uses of the same seed (positional drift vs. attitude wobble) do
	// not move in step. Reused for the in-flight drift force, the hover hold-point offset, and the
	// roll/pitch wobble, so each expresses the same kind of wander in its own units.
	FVector CoherentWander(int32 Seed, float Magnitude, float Frequency, float TimeSeconds, float PhaseBias = 0.f)
	{
		const float Phase = (float)Seed * 1.2399f + PhaseBias;
		const float X = Magnitude * CoherentNoise(Phase, Frequency, TimeSeconds);
		const float Y = Magnitude * CoherentNoise(Phase + 2.1f, Frequency, TimeSeconds);
		return FVector(X, Y, 0.f);
	}
}

FVector DroneFlight::ComputeWorldWind(const FWorldWindParams& Wind, int32 Seed, float TimeSeconds)
{
	const FVector Direction = Wind.Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	// The gust ebbs and flows within the configured variance, so the wind never exceeds base +/- variance.
	// A seed-derived phase lets different levels (or reseeds) gust on their own schedule.
	const float Gust = CoherentNoise((float)Seed * 0.7531f, Wind.GustFrequency, TimeSeconds);
	return Direction * (Wind.BaseSpeed + Wind.GustVariance * Gust);
}

FDroneForces DroneFlight::ComputeImperfectionForces(
	const FImperfectionParams& Params,
	int32 Seed,
	float TimeSeconds,
	const FVector& WorldWind,
	bool bHovering,
	bool bGrounded)
{
	FDroneForces Out;

	// A drone resting on a surface is held by the ground, not flying. The whole layer exists to keep
	// airborne flight from ever being perfectly still or perfectly rigid, so on the ground it adds
	// nothing: a parked drone neither drifts, catches the wind, wobbles, nor bobs - it sits still.
	if (bGrounded)
	{
		return Out;
	}

	// A seed-derived phase so each drone wanders on its own path; the same seed reproduces it exactly,
	// and two drones with different seeds drift independently.
	const float Phase = (float)Seed * 1.2399f;

	// Coherent-noise horizontal drift, so the drone is never perfectly still. In free flight it is a
	// force that nudges the drone around; while hovering the position-hold spring would only cancel a
	// force, so there the same wander moves the hold point instead (see ComputeDriftOffset) and no drift
	// force is added here.
	if (!bHovering)
	{
		Out.Force += CoherentWander(Seed, Params.DriftAmplitude, Params.DriftFrequency, TimeSeconds);
	}

	// A slight coherent-noise wobble on roll (X) and pitch (Y), applied whether hovering or flying free,
	// so the airframe is never perfectly rigid. Yaw (Z) is left to the pilot. A phase bias decorrelates
	// it from the positional drift so attitude and position do not wander in step.
	Out.Torque += CoherentWander(Seed, Params.AttitudeWobbleAmplitude, Params.AttitudeWobbleFrequency, TimeSeconds, 3.7f);

	// Hover bob: a slow vertical oscillation applied only while hovering, so a parked drone breathes up
	// and down instead of freezing. Bounded by HoverBobAmplitude.
	if (bHovering)
	{
		Out.Force.Z += Params.HoverBobAmplitude * FMath::Sin(2.f * PI * Params.HoverBobFrequency * TimeSeconds + Phase);
	}

	// Wind: the global world wind scaled by this drone's susceptibility, so a light drone is shoved
	// harder than a heavy one by the same wind.
	Out.Force += WorldWind * Params.WindSusceptibility;

	return Out;
}

FVector DroneFlight::ComputeDriftOffset(const FImperfectionParams& Params, int32 Seed, float TimeSeconds)
{
	// The same coherent-noise wander as the in-flight drift, scaled to a distance and returned as a
	// position offset. The hover hold point rides this, so a parked drone gently circles its spot within
	// DriftHoverRadius instead of freezing on the setpoint.
	return CoherentWander(Seed, Params.DriftHoverRadius, Params.DriftFrequency, TimeSeconds);
}

float DroneFlight::StepMotorLag(float CurrentThrust, float CommandedThrust, float LagTau, float DeltaSeconds)
{
	// No lag configured: the command is realized at once.
	if (LagTau <= 0.f)
	{
		return CommandedThrust;
	}

	// First-order exponential approach: each step closes a fixed fraction of the gap to the command,
	// so thrust ramps in smoothly and the step is stable for any DeltaSeconds. The fraction is
	// 1 - e^(-dt/tau): small for a step short relative to the spool time, approaching 1 for a long one.
	const float Alpha = 1.f - FMath::Exp(-DeltaSeconds / LagTau);
	return CurrentThrust + (CommandedThrust - CurrentThrust) * Alpha;
}
