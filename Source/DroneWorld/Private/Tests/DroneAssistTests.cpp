#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Flight/DroneAssist.h"
#include "Flight/QuadFlightModel.h"
#include "Flight/FixedWingFlightModel.h"

namespace
{
	// A state banked to the right (positive roll) by the given angle, otherwise level and at rest.
	FDroneFlightState BankedRight(float RollDegrees)
	{
		FDroneFlightState State;
		State.Orientation = FRotator(0.f, 0.f, RollDegrees);
		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistAngleModeLevelsTowardLevel,
	"DroneWorld.Assist.AngleModeLevelsTowardLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistAngleModeLevelsTowardLevel::RunTest(const FString& Parameters)
{
	// In Angle mode with the sticks released, a drone banked to the right gets a left-rolling
	// correction that opposes the bank, pulling it back toward level.
	const FDroneControlIntent Released;
	const FVector Torque = DroneFlight::ComputeLevelingTorque(EDroneAssistMode::Angle, Released, BankedRight(30.f), 6.f);

	TestTrue(TEXT("leveling torque opposes a right bank"), Torque.X < 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistAcroModeDoesNotLevel,
	"DroneWorld.Assist.AcroModeDoesNotLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistAcroModeDoesNotLevel::RunTest(const FString& Parameters)
{
	// Acro is raw rate control: a banked drone gets no self-leveling correction and holds its attitude.
	const FDroneControlIntent Released;
	const FVector Torque = DroneFlight::ComputeLevelingTorque(EDroneAssistMode::Acro, Released, BankedRight(30.f), 6.f);

	TestTrue(TEXT("Acro applies no leveling torque"), Torque.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistAngleModeHeldStickSuppressesLeveling,
	"DroneWorld.Assist.AngleModeHeldStickSuppressesLeveling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistAngleModeHeldStickSuppressesLeveling::RunTest(const FString& Parameters)
{
	// Angle self-levels only on stick release: while the pilot holds full roll, the leveling on the
	// roll axis is suppressed so the stick still commands attitude.
	FDroneControlIntent FullRoll;
	FullRoll.Roll = 1.f;
	const FVector Torque = DroneFlight::ComputeLevelingTorque(EDroneAssistMode::Angle, FullRoll, BankedRight(30.f), 6.f);

	TestTrue(TEXT("held roll stick suppresses roll leveling"), FMath::IsNearlyZero(Torque.X));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistQuadHoverHoldsAtSetpoint,
	"DroneWorld.Assist.QuadHoverHoldsAtSetpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistQuadHoverHoldsAtSetpoint::RunTest(const FString& Parameters)
{
	// Parked exactly on the hold point at rest, hover just cancels gravity: no horizontal force and an
	// upward force equal to weight, so the drone neither drifts nor falls.
	const FDroneControlIntent Released;
	const FQuadFlightParams Params;
	FDroneFlightState State;
	const FVector Hold = State.Location;

	const FDroneForces Forces = DroneFlight::ComputeQuadHoverForces(Released, State, Hold, Params);

	TestTrue(TEXT("no horizontal force when parked on the setpoint"), FMath::IsNearlyZero((float)Forces.Force.X) && FMath::IsNearlyZero((float)Forces.Force.Y));
	TestTrue(TEXT("upward force balances weight"), FMath::IsNearlyEqual((float)Forces.Force.Z, Params.Mass * Params.GravityAccel, 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistQuadHoverPullsTowardSetpoint,
	"DroneWorld.Assist.QuadHoverPullsTowardSetpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistQuadHoverPullsTowardSetpoint::RunTest(const FString& Parameters)
{
	// Displaced from the hold point, hover produces a restoring force toward it: a push back across the
	// horizontal offset and extra lift when below the held altitude, so the gap to the setpoint shrinks.
	const FDroneControlIntent Released;
	const FQuadFlightParams Params;

	const FVector Hold(0.f, 0.f, 0.f);
	FDroneFlightState State;
	State.Location = Hold + FVector(100.f, 0.f, -100.f); // east of and below the hold point

	const FDroneForces Forces = DroneFlight::ComputeQuadHoverForces(Released, State, Hold, Params);

	TestTrue(TEXT("force pushes back toward the hold point horizontally"), Forces.Force.X < 0.f);
	TestTrue(TEXT("force lifts harder than weight when below the hold altitude"), Forces.Force.Z > Params.Mass * Params.GravityAccel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistFixedWingHoverLoiters,
	"DroneWorld.Assist.FixedWingHoverLoiters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistFixedWingHoverLoiters::RunTest(const FString& Parameters)
{
	// A fixed-wing cannot stop, so its hover is a loiter circle: it keeps thrusting forward, banks in,
	// continuously yaws to come around the circle, and holds its altitude rather than dropping.
	const FDroneControlIntent Released;
	const FFixedWingFlightParams Params;
	const FDroneFlightState State; // level, at rest, facing world +X, at the held altitude (Z = 0)

	const FDroneForces Forces = DroneFlight::ComputeFixedWingHoverForces(Released, State, FVector(0.f, 0.f, 0.f), Params);

	TestTrue(TEXT("loiter keeps flying forward under thrust"), Forces.Force.X > 0.f);
	TestTrue(TEXT("loiter banks the aircraft into the turn"), Forces.Torque.X > 0.f);
	TestTrue(TEXT("loiter yaws to come around the circle rather than holding heading"), Forces.Torque.Z > 0.f);
	TestTrue(TEXT("loiter holds the engaged altitude instead of dropping"), FMath::IsNearlyZero((float)Forces.Force.Z, 1.f));

	// It circles rather than chasing a point: shifting the hold point horizontally changes nothing.
	const FDroneForces Shifted = DroneFlight::ComputeFixedWingHoverForces(Released, State, FVector(100000.f, 50000.f, 0.f), Params);
	TestTrue(TEXT("loiter ignores the horizontal hold point"), Shifted.Force.Equals(Forces.Force) && Shifted.Torque.Equals(Forces.Torque));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
