#pragma once

#include "CoreMinimal.h"
#include "DroneMotorAudio.generated.h"

// The per-drone tuning of the motor sound: the volume and pitch the looping rotor whine reads at idle
// (armed, sticks centered) and at full throttle, plus how strongly banking redistributes thrust across
// the rotors for the per-rotor mix. It rides on the pawn alongside the rotor sound assets, so each
// drone Blueprint carries its own motor voice - a small quad's frantic whine, a large one's deeper
// hum - without touching the flight-tuning Preset.
USTRUCT(BlueprintType)
struct FMotorAudioParams
{
	GENERATED_BODY()

	// Volume multiplier when armed at zero throttle - the motors are spinning but doing no work, so they
	// idle audibly rather than going silent. Kept above zero so an armed drone is always heard.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio", meta = (ClampMin = "0.0"))
	float IdleVolume = 0.25f;

	// Volume multiplier at full throttle, the loudest the motors get.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio", meta = (ClampMin = "0.0"))
	float FullVolume = 1.f;

	// Pitch multiplier on the rotor sound when armed at zero throttle - the low end of the whine.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio", meta = (ClampMin = "0.0"))
	float IdlePitch = 0.8f;

	// Pitch multiplier at full throttle - the top of the whine, higher than idle so spooling up sweeps
	// the pitch upward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio", meta = (ClampMin = "0.0"))
	float FullPitch = 2.f;

	// How strongly an attitude demand redistributes thrust across the four rotors for the per-rotor mix,
	// as a fraction of the collective added to the working rotors and removed from the others. Zero makes
	// every rotor track the collective alike (no banking shift); higher values make banking swing the
	// sound harder between the corners. Purely an audio mix - the kinematic flight model holds the real
	// dynamics - so it is tuned by ear, not against physics.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio", meta = (ClampMin = "0.0"))
	float RotorMixGain = 0.4f;
};

// The four rotor levels of a quad after the audio mix, 0 idle .. 1 full each, laid out by corner as
// seen from above with the drone facing +X (FrontRight = +X +Y). The mix only redistributes the
// collective for the sake of the sound; it is not the flight model's force law.
USTRUCT(BlueprintType)
struct FQuadRotorLevels
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio")
	float FrontLeft = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio")
	float FrontRight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio")
	float RearLeft = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio")
	float RearRight = 0.f;
};

// One rotor's audio at this instant: the volume and pitch multipliers to drive its looping sound. Plain
// data out of the pure mapping, set straight onto the audio component each tick.
USTRUCT(BlueprintType)
struct FMotorAudioState
{
	GENERATED_BODY()

	// Volume multiplier for the rotor's looping sound, 0 when silent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio")
	float Volume = 0.f;

	// Pitch multiplier for the rotor's looping sound.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor Audio")
	float Pitch = 1.f;
};

namespace DroneFlight
{
	// Map one rotor's realized level (0 idle .. 1 full) to its audio: volume and pitch both rise with the
	// level between the params' idle and full ends, so the whine swells and sharpens as the motor spools
	// up. Because the level fed in is the realized (motor-lagged) throttle, the sound tracks the live
	// motor state - it spools up with the thrust rather than snapping to the stick. A disarmed drone is
	// silent (volume zero) whatever its level, consistent with the motors being cut when disarmed. The
	// level is clamped to [0, 1], so an over- or under-range input never drives the sound past its ends.
	// Pure of (params, level, armed).
	DRONEWORLD_API FMotorAudioState ComputeMotorAudio(const FMotorAudioParams& Params, float RotorLevel, bool bArmed);

	// Distribute the collective throttle across a quad's four rotors by the attitude demand, so banking
	// spins up the rotors doing the work and eases off the others - the audible shift a real quad makes
	// when it leans. Each rotor is the collective plus MixGain times the demand it answers: roll right
	// loads the left rotors, pitch up loads the rear, yaw loads one diagonal pair (FrontRight/RearLeft).
	// Every level is clamped to [0, 1]. Pure of (collective, roll, pitch, yaw, gain). This drives the
	// per-rotor sound only; the kinematic flight model still owns the actual dynamics, so the mix is a
	// listening aid, not a rotor-physics model. The collective passed in is the realized (motor-lagged)
	// throttle, so the whole mix spools up with the thrust.
	DRONEWORLD_API FQuadRotorLevels ComputeQuadMotorMix(float Collective, float Roll, float Pitch, float Yaw, float MixGain);
}
