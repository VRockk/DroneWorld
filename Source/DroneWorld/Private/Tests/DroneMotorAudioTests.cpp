#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Flight/DroneMotorAudio.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorAudioDisarmedIsSilent,
	"DroneWorld.Audio.Motor.DisarmedIsSilent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorAudioDisarmedIsSilent::RunTest(const FString& Parameters)
{
	// A disarmed drone has its motors cut, so the rotor is silent whatever throttle was last applied -
	// even at full level the volume is zero. Consistent with arming gating the motors.
	const FMotorAudioParams Params;

	TestEqual(TEXT("disarmed at zero level is silent"), DroneFlight::ComputeMotorAudio(Params, 0.f, /*bArmed*/ false).Volume, 0.f);
	TestEqual(TEXT("disarmed at full level is still silent"), DroneFlight::ComputeMotorAudio(Params, 1.f, /*bArmed*/ false).Volume, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorAudioArmedIdleWhines,
	"DroneWorld.Audio.Motor.ArmedAtZeroThrottleIdles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorAudioArmedIdleWhines::RunTest(const FString& Parameters)
{
	// Armed with the throttle down, the motors spin but do no work: an audible idle whine at the params'
	// idle volume and pitch rather than silence, so an armed drone is always heard.
	FMotorAudioParams Params;
	Params.IdleVolume = 0.3f;
	Params.IdlePitch = 0.7f;

	const FMotorAudioState Idle = DroneFlight::ComputeMotorAudio(Params, 0.f, /*bArmed*/ true);
	TestTrue(TEXT("an armed idle is audible"), Idle.Volume > 0.f);
	TestEqual(TEXT("idle volume is the params' idle end"), Idle.Volume, 0.3f);
	TestEqual(TEXT("idle pitch is the params' idle end"), Idle.Pitch, 0.7f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorAudioVolumeRisesWithThrottle,
	"DroneWorld.Audio.Motor.VolumeRisesWithThrottle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorAudioVolumeRisesWithThrottle::RunTest(const FString& Parameters)
{
	// Volume climbs with the rotor level: spooling up is louder than idle, and full throttle reaches the
	// params' full volume, so the drone audibly works harder as it climbs.
	FMotorAudioParams Params;
	Params.IdleVolume = 0.25f;
	Params.FullVolume = 1.f;

	const float Quiet = DroneFlight::ComputeMotorAudio(Params, 0.2f, /*bArmed*/ true).Volume;
	const float Mid = DroneFlight::ComputeMotorAudio(Params, 0.5f, /*bArmed*/ true).Volume;
	const float Loud = DroneFlight::ComputeMotorAudio(Params, 0.9f, /*bArmed*/ true).Volume;

	TestTrue(TEXT("more throttle is louder"), Quiet < Mid && Mid < Loud);
	TestEqual(TEXT("full throttle reaches full volume"), DroneFlight::ComputeMotorAudio(Params, 1.f, true).Volume, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorAudioPitchRisesWithThrottle,
	"DroneWorld.Audio.Motor.PitchRisesWithThrottle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorAudioPitchRisesWithThrottle::RunTest(const FString& Parameters)
{
	// Pitch sweeps upward with the rotor level: the whine sharpens as the motor spools up and reaches the
	// params' full pitch at full throttle.
	FMotorAudioParams Params;
	Params.IdlePitch = 0.8f;
	Params.FullPitch = 2.f;

	const float Low = DroneFlight::ComputeMotorAudio(Params, 0.1f, /*bArmed*/ true).Pitch;
	const float High = DroneFlight::ComputeMotorAudio(Params, 0.8f, /*bArmed*/ true).Pitch;

	TestTrue(TEXT("more throttle is higher-pitched"), High > Low);
	TestEqual(TEXT("full throttle reaches full pitch"), DroneFlight::ComputeMotorAudio(Params, 1.f, true).Pitch, 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorAudioClampsOutOfRangeLevel,
	"DroneWorld.Audio.Motor.ClampsOutOfRangeLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorAudioClampsOutOfRangeLevel::RunTest(const FString& Parameters)
{
	// An over- or under-range level never drives the sound past its ends: above full reads as full, below
	// idle reads as idle, so a mix that momentarily overshoots cannot scream past the configured top.
	FMotorAudioParams Params;
	Params.IdleVolume = 0.25f;
	Params.FullVolume = 1.f;

	TestEqual(TEXT("level above 1 clamps to full volume"), DroneFlight::ComputeMotorAudio(Params, 1.5f, true).Volume, 1.f);
	TestEqual(TEXT("level below 0 clamps to idle volume"), DroneFlight::ComputeMotorAudio(Params, -0.5f, true).Volume, 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorMixLevelFlightIsEven,
	"DroneWorld.Audio.RotorMix.LevelFlightIsEven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorMixLevelFlightIsEven::RunTest(const FString& Parameters)
{
	// With no attitude demand every rotor pulls the same collective, so a drone climbing straight up has
	// all four corners at the same level and the sound sits centered.
	const FQuadRotorLevels Mix = DroneFlight::ComputeQuadMotorMix(0.5f, 0.f, 0.f, 0.f, /*MixGain*/ 0.4f);

	TestEqual(TEXT("front-left equals collective"), Mix.FrontLeft, 0.5f);
	TestEqual(TEXT("front-right equals collective"), Mix.FrontRight, 0.5f);
	TestEqual(TEXT("rear-left equals collective"), Mix.RearLeft, 0.5f);
	TestEqual(TEXT("rear-right equals collective"), Mix.RearRight, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorMixRollLoadsLeft,
	"DroneWorld.Audio.RotorMix.RollRightLoadsLeftRotors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorMixRollLoadsLeft::RunTest(const FString& Parameters)
{
	// Rolling right loads the left rotors and eases the right ones, so the sound swings to the left side
	// as the drone leans into the roll.
	const FQuadRotorLevels Mix = DroneFlight::ComputeQuadMotorMix(0.5f, /*Roll*/ 1.f, 0.f, 0.f, 0.4f);

	TestTrue(TEXT("front-left works harder than front-right"), Mix.FrontLeft > Mix.FrontRight);
	TestTrue(TEXT("rear-left works harder than rear-right"), Mix.RearLeft > Mix.RearRight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorMixPitchLoadsRear,
	"DroneWorld.Audio.RotorMix.PitchUpLoadsRearRotors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorMixPitchLoadsRear::RunTest(const FString& Parameters)
{
	// Pitching the nose up loads the rear rotors and eases the front, so the sound shifts toward the tail.
	const FQuadRotorLevels Mix = DroneFlight::ComputeQuadMotorMix(0.5f, 0.f, /*Pitch*/ 1.f, 0.f, 0.4f);

	TestTrue(TEXT("rear-left works harder than front-left"), Mix.RearLeft > Mix.FrontLeft);
	TestTrue(TEXT("rear-right works harder than front-right"), Mix.RearRight > Mix.FrontRight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorMixYawLoadsDiagonal,
	"DroneWorld.Audio.RotorMix.YawLoadsOneDiagonal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorMixYawLoadsDiagonal::RunTest(const FString& Parameters)
{
	// Yaw spins up one diagonal pair against the other - the front-right/rear-left diagonal works harder
	// for a right yaw - so a spin is heard as a diagonal swing rather than a front/back or side shift.
	const FQuadRotorLevels Mix = DroneFlight::ComputeQuadMotorMix(0.5f, 0.f, 0.f, /*Yaw*/ 1.f, 0.4f);

	TestTrue(TEXT("front-right works harder than front-left"), Mix.FrontRight > Mix.FrontLeft);
	TestTrue(TEXT("rear-left works harder than rear-right"), Mix.RearLeft > Mix.RearRight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneMotorMixClampsToUnit,
	"DroneWorld.Audio.RotorMix.ClampsToUnitRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneMotorMixClampsToUnit::RunTest(const FString& Parameters)
{
	// A hard demand on top of an already-high or already-low collective cannot push a rotor past full or
	// below idle: every level stays within [0, 1] so the audio mapping always gets a valid level.
	const FQuadRotorLevels High = DroneFlight::ComputeQuadMotorMix(/*Collective*/ 0.95f, 1.f, 1.f, 1.f, /*MixGain*/ 0.8f);
	const FQuadRotorLevels Low = DroneFlight::ComputeQuadMotorMix(/*Collective*/ 0.05f, -1.f, -1.f, -1.f, /*MixGain*/ 0.8f);

	for (float Level : { High.FrontLeft, High.FrontRight, High.RearLeft, High.RearRight })
	{
		TestTrue(TEXT("a loaded rotor never exceeds full"), Level <= 1.f);
	}
	for (float Level : { Low.FrontLeft, Low.FrontRight, Low.RearLeft, Low.RearRight })
	{
		TestTrue(TEXT("an eased rotor never drops below idle"), Level >= 0.f);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
