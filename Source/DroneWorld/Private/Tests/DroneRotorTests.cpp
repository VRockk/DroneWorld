#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DronePawn.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneRotorSpinsWhileArmed,
	"DroneWorld.Rotor.SpinsWhileArmed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneRotorSpinsWhileArmed::RunTest(const FString& Parameters)
{
	// Armed, the blades turn at the constant rate over the frame: 360 deg/s for half a second is 180 degrees.
	const float Step = ADronePawn::StepRotorSpin(/*bArmed*/ true, /*Rate*/ 360.f, /*Dt*/ 0.5f);

	TestEqual(TEXT("an armed rotor turns rate * dt this frame"), Step, 180.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneRotorHoldsStillWhileDisarmed,
	"DroneWorld.Rotor.HoldsStillWhileDisarmed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneRotorHoldsStillWhileDisarmed::RunTest(const FString& Parameters)
{
	// Disarmed, the blades do not move however fast they would spin or however long the frame, so a disarmed
	// drone sits with its rotors still.
	const float Step = ADronePawn::StepRotorSpin(/*bArmed*/ false, /*Rate*/ 360.f, /*Dt*/ 0.5f);

	TestEqual(TEXT("a disarmed rotor does not turn"), Step, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneRotorStepScalesWithFrameTime,
	"DroneWorld.Rotor.StepScalesWithFrameTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneRotorStepScalesWithFrameTime::RunTest(const FString& Parameters)
{
	// The spin is framerate-independent: a frame twice as long turns the blades twice as far, so the rotor
	// looks the same speed regardless of how the frames fall.
	const float Short = ADronePawn::StepRotorSpin(true, 1800.f, 0.01f);
	const float Long = ADronePawn::StepRotorSpin(true, 1800.f, 0.02f);

	TestEqual(TEXT("twice the frame time turns twice as far"), Long, Short * 2.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
