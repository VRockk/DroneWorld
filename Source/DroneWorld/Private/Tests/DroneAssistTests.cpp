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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistGroundSettleLevelsOnFlatGround,
	"DroneWorld.Assist.GroundSettleLevelsOnFlatGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistGroundSettleLevelsOnFlatGround::RunTest(const FString& Parameters)
{
	// On flat ground (normal straight up) an upright drone banked to the right gets a left-rolling
	// correction that settles it flat - the same direction self-leveling pulls - with no commanded pitch.
	const FVector FlatUp(0.f, 0.f, 1.f);
	const FVector Torque = DroneFlight::ComputeGroundSettleTorque(FRotator(0.f, 0.f, 30.f), FVector::ZeroVector, FlatUp, 10.f);

	TestTrue(TEXT("settle torque opposes a right bank on flat ground"), Torque.X < 0.f);
	TestTrue(TEXT("settle commands no pitch when only rolled"), FMath::IsNearlyZero((float)Torque.Y, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistGroundSettleMatchesSlopeNotWorldLevel,
	"DroneWorld.Assist.GroundSettleMatchesSlopeNotWorldLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistGroundSettleMatchesSlopeNotWorldLevel::RunTest(const FString& Parameters)
{
	// Settling conforms to the slope, not to world level. A drone whose attitude already matches a tilted
	// ground plane is settled - near-zero torque - even though it is not world-level. Self-leveling, which
	// pulls toward world level, would instead fight that attitude, so the two disagree on a slope.
	const FVector SlopeNormal = FVector(FMath::Sin(FMath::DegreesToRadians(20.f)), 0.f, FMath::Cos(FMath::DegreesToRadians(20.f)));
	const FRotator MatchingSlope = FQuat::FindBetweenNormals(FVector::UpVector, SlopeNormal).Rotator();

	const FVector Settle = DroneFlight::ComputeGroundSettleTorque(MatchingSlope, FVector::ZeroVector, SlopeNormal, 10.f);
	TestTrue(TEXT("a drone aligned to the slope is settled (near-zero torque)"), Settle.Size() < 1.f);

	FDroneFlightState OnSlope;
	OnSlope.Orientation = MatchingSlope;
	const FVector Leveling = DroneFlight::ComputeLevelingTorque(EDroneAssistMode::Angle, FDroneControlIntent(), OnSlope, 6.f);
	TestTrue(TEXT("world self-leveling would fight the slope attitude settle accepts"), Leveling.Size() > 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistGroundSettleKeepsFlippedDroneFlipped,
	"DroneWorld.Assist.GroundSettleKeepsFlippedDroneFlipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistGroundSettleKeepsFlippedDroneFlipped::RunTest(const FString& Parameters)
{
	// A drone that came to rest inverted settles against the face it landed on rather than righting itself:
	// fully upside-down on flat ground it is already settled (near-zero torque), and a mostly-inverted
	// drone is pushed the rest of the way over onto its back, not back toward upright.
	const FVector FlatUp(0.f, 0.f, 1.f);

	const FVector Inverted = DroneFlight::ComputeGroundSettleTorque(FRotator(0.f, 0.f, 180.f), FVector::ZeroVector, FlatUp, 10.f);
	TestTrue(TEXT("a fully inverted drone resting flat is already settled"), Inverted.Size() < 1.f);

	// At 160 degrees of roll the nearest rest is fully inverted (180), not upright (0). The correction must
	// drive roll further over (positive roll torque) rather than back toward level (negative).
	const FVector MostlyInverted = DroneFlight::ComputeGroundSettleTorque(FRotator(0.f, 0.f, 160.f), FVector::ZeroVector, FlatUp, 10.f);
	TestTrue(TEXT("a mostly-inverted drone settles deeper into the flip, not back upright"), MostlyInverted.X > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneAssistGroundSettleZeroWithoutUsableInput,
	"DroneWorld.Assist.GroundSettleZeroWithoutUsableInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneAssistGroundSettleZeroWithoutUsableInput::RunTest(const FString& Parameters)
{
	// No ground normal and no strength both mean nothing to settle toward, so the torque is zero - an
	// airborne drone (no contact) keeps whatever attitude it has.
	TestTrue(TEXT("a zero normal yields no settle torque"),
		DroneFlight::ComputeGroundSettleTorque(FRotator(0.f, 0.f, 30.f), FVector::ZeroVector, FVector::ZeroVector, 10.f).IsNearlyZero());
	TestTrue(TEXT("zero strength yields no settle torque"),
		DroneFlight::ComputeGroundSettleTorque(FRotator(0.f, 0.f, 30.f), FVector::ZeroVector, FVector(0.f, 0.f, 1.f), 0.f).IsNearlyZero());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
