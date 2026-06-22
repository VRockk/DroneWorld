#include "Flight/DroneMotorAudio.h"

FMotorAudioState DroneFlight::ComputeMotorAudio(const FMotorAudioParams& Params, float RotorLevel, bool bArmed)
{
	FMotorAudioState Out;

	// Disarmed motors are cut, so the rotor is silent however much throttle was last commanded - the
	// sound does not linger as the realized level decays. Pitch is parked at idle; it is inaudible at
	// zero volume but leaves the sound ready to resume from the low end when the drone arms again.
	if (!bArmed)
	{
		Out.Volume = 0.f;
		Out.Pitch = Params.IdlePitch;
		return Out;
	}

	// Volume and pitch both ride the realized level from their idle ends to their full ends, so the whine
	// swells and sharpens together as the motor spools up. The level is clamped first so an over-range
	// input cannot drive either past its full value.
	const float Level = FMath::Clamp(RotorLevel, 0.f, 1.f);
	Out.Volume = FMath::Lerp(Params.IdleVolume, Params.FullVolume, Level);
	Out.Pitch = FMath::Lerp(Params.IdlePitch, Params.FullPitch, Level);
	return Out;
}

FQuadRotorLevels DroneFlight::ComputeQuadMotorMix(float Collective, float Roll, float Pitch, float Yaw, float MixGain)
{
	// Each rotor answers the share of the attitude demand pointed at its corner: roll right loads the
	// left side, pitch up loads the rear, and yaw loads the front-right/rear-left diagonal against the
	// other. The signs below lay that out per corner; the offset is scaled by MixGain and clamped so a
	// big demand redistributes the sound without driving any rotor outside [0, 1].
	auto Mix = [Collective, MixGain](float RollSign, float PitchSign, float YawSign, float Roll, float Pitch, float Yaw)
	{
		const float Offset = MixGain * (RollSign * Roll + PitchSign * Pitch + YawSign * Yaw);
		return FMath::Clamp(Collective + Offset, 0.f, 1.f);
	};

	FQuadRotorLevels Out;
	Out.FrontLeft = Mix(/*Roll*/ +1.f, /*Pitch*/ -1.f, /*Yaw*/ -1.f, Roll, Pitch, Yaw);
	Out.FrontRight = Mix(/*Roll*/ -1.f, /*Pitch*/ -1.f, /*Yaw*/ +1.f, Roll, Pitch, Yaw);
	Out.RearLeft = Mix(/*Roll*/ +1.f, /*Pitch*/ +1.f, /*Yaw*/ +1.f, Roll, Pitch, Yaw);
	Out.RearRight = Mix(/*Roll*/ -1.f, /*Pitch*/ +1.f, /*Yaw*/ -1.f, Roll, Pitch, Yaw);
	return Out;
}
