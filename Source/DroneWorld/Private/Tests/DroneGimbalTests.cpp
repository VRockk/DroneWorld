#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DronePawn.h"
#include "Flight/DroneFlightTypes.h"

namespace
{
	// A range wide enough to read slewing without hitting a stop, with a known slew rate and acceleration.
	FGimbalConfig TestConfig()
	{
		FGimbalConfig Config;
		Config.MinTiltDegrees = -45.f;
		Config.MaxTiltDegrees = 30.f;
		Config.DefaultTiltDegrees = 15.f;
		Config.TiltSlewRate = 20.f;
		Config.TiltSlewAcceleration = 40.f;
		Config.TiltMaxSlewRate = 100.f;
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneGimbalHoldingSlewsTowardCommandedDirection,
	"DroneWorld.Gimbal.HoldingSlewsTowardCommandedDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneGimbalHoldingSlewsTowardCommandedDirection::RunTest(const FString& Parameters)
{
	// A held tilt key nudges the pitch a step in its direction rather than snapping to a stop: holding up
	// raises the tilt, holding down lowers it, and the step is a slew (a fraction of the range), not a jump.
	const FGimbalConfig Config = TestConfig();

	const float Up = ADronePawn::StepGimbalTilt(Config, 0.f, /*Direction*/ 1.f, /*HeldSeconds*/ 0.f, /*Dt*/ 0.1f);
	const float Down = ADronePawn::StepGimbalTilt(Config, 0.f, /*Direction*/ -1.f, /*HeldSeconds*/ 0.f, /*Dt*/ 0.1f);

	TestTrue(TEXT("holding up raises the tilt"), Up > 0.f);
	TestTrue(TEXT("holding down lowers the tilt"), Down < 0.f);
	TestTrue(TEXT("a single step slews rather than jumping to the stop"), Up < Config.MaxTiltDegrees);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneGimbalSlewAcceleratesWithHoldTime,
	"DroneWorld.Gimbal.SlewAcceleratesWithHoldTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneGimbalSlewAcceleratesWithHoldTime::RunTest(const FString& Parameters)
{
	// The longer the key is held, the faster the tilt moves: the same step taken after holding longer
	// covers more degrees than the same step taken at the start of the press.
	const FGimbalConfig Config = TestConfig();

	const float EarlyStep = ADronePawn::StepGimbalTilt(Config, 0.f, 1.f, /*HeldSeconds*/ 0.f, 0.1f);
	const float LateStep = ADronePawn::StepGimbalTilt(Config, 0.f, 1.f, /*HeldSeconds*/ 1.f, 0.1f);

	TestTrue(TEXT("a step later in the hold covers more than one at the start"), LateStep > EarlyStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneGimbalSlewRespectsMaxRate,
	"DroneWorld.Gimbal.SlewRespectsMaxRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneGimbalSlewRespectsMaxRate::RunTest(const FString& Parameters)
{
	// The acceleration tops out: held long enough the slew stops getting faster, so a step at the rate cap
	// is no larger than one taken well past where the cap is reached, and it moves at exactly the cap.
	const FGimbalConfig Config = TestConfig();

	// TiltSlewRate 20 + TiltSlewAcceleration 40 * Held reaches TiltMaxSlewRate 100 at Held = 2s.
	const float AtCap = ADronePawn::StepGimbalTilt(Config, 0.f, 1.f, /*HeldSeconds*/ 2.f, 0.1f);
	const float PastCap = ADronePawn::StepGimbalTilt(Config, 0.f, 1.f, /*HeldSeconds*/ 10.f, 0.1f);

	TestEqual(TEXT("slew is capped at the max rate"), PastCap, AtCap);
	// The cap is 100 deg/s, so a 0.1s step moves 10 degrees and no more.
	TestEqual(TEXT("a capped step moves at the max rate"), AtCap, 10.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneGimbalClampsToRange,
	"DroneWorld.Gimbal.ClampsToRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneGimbalClampsToRange::RunTest(const FString& Parameters)
{
	// However long the key is held, the tilt cannot slew past its stops: held up from near the top it
	// lands exactly at the max, held down from near the bottom exactly at the min.
	const FGimbalConfig Config = TestConfig();

	const float PastMax = ADronePawn::StepGimbalTilt(Config, Config.MaxTiltDegrees - 1.f, 1.f, /*HeldSeconds*/ 5.f, 1.f);
	const float PastMin = ADronePawn::StepGimbalTilt(Config, Config.MinTiltDegrees + 1.f, -1.f, /*HeldSeconds*/ 5.f, 1.f);

	TestEqual(TEXT("slewing up clamps at the max tilt"), PastMax, Config.MaxTiltDegrees);
	TestEqual(TEXT("slewing down clamps at the min tilt"), PastMin, Config.MinTiltDegrees);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneGimbalIdleHoldsTilt,
	"DroneWorld.Gimbal.IdleHoldsTilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneGimbalIdleHoldsTilt::RunTest(const FString& Parameters)
{
	// With no key held the tilt does not move: it stays wherever the pilot left it rather than snapping
	// back to a default.
	const FGimbalConfig Config = TestConfig();

	const float Held = ADronePawn::StepGimbalTilt(Config, 12.f, /*Direction*/ 0.f, /*HeldSeconds*/ 0.f, 0.1f);

	TestEqual(TEXT("an idle gimbal holds its tilt"), Held, 12.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
