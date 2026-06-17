#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Flight/DroneIntegrator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneIntegrationLinearVelocityAndPositionDeltas,
	"DroneWorld.Integration.LinearVelocityAndPositionDeltas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneIntegrationLinearVelocityAndPositionDeltas::RunTest(const FString& Parameters)
{
	// From rest, a force giving 100 cm/s^2 over 0.5 s yields v = 50 cm/s and (semi-implicit) x = 25 cm.
	const float Mass = 2.f;
	FDroneForces Forces;
	Forces.Force = FVector(0.f, 0.f, 100.f * Mass);

	const FDroneFlightState Next = DroneIntegrator::IntegrateStep(FDroneFlightState(), Forces, Mass, 0.5f);

	TestEqual(TEXT("velocity delta = accel * dt"), Next.Velocity.Z, 50.0);
	TestEqual(TEXT("position uses the updated velocity (semi-implicit)"), Next.Location.Z, 25.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneIntegrationExplicitWouldNotMovePositionFirstStep,
	"DroneWorld.Integration.SemiImplicitMovesPositionOnFirstStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneIntegrationExplicitWouldNotMovePositionFirstStep::RunTest(const FString& Parameters)
{
	// Semi-implicit Euler advances position with the post-update velocity, so a body starting at rest
	// moves on the very first step. Explicit Euler would leave it at the origin this step.
	FDroneForces Forces;
	Forces.Force = FVector(10.f, 0.f, 0.f);

	const FDroneFlightState Next = DroneIntegrator::IntegrateStep(FDroneFlightState(), Forces, 1.f, 1.f);

	TestTrue(TEXT("position advances on the first step"), Next.Location.X > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneIntegrationAngularVelocityDelta,
	"DroneWorld.Integration.AngularVelocityDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneIntegrationAngularVelocityDelta::RunTest(const FString& Parameters)
{
	// Angular acceleration of 100 deg/s^2 over 0.5 s yields 50 deg/s about that axis.
	FDroneForces Forces;
	Forces.Torque = FVector(0.f, 0.f, 100.f);

	const FDroneFlightState Next = DroneIntegrator::IntegrateStep(FDroneFlightState(), Forces, 1.f, 0.5f);

	TestEqual(TEXT("angular velocity delta = angular accel * dt"), Next.AngularVelocity.Z, 50.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneIntegrationOrientationAdvancesWithYaw,
	"DroneWorld.Integration.OrientationAdvancesWithYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneIntegrationOrientationAdvancesWithYaw::RunTest(const FString& Parameters)
{
	// A level drone spinning at 90 deg/s about Z has yawed ~90 degrees after one second.
	FDroneFlightState State;
	State.AngularVelocity = FVector(0.f, 0.f, 90.f);

	const FDroneFlightState Next = DroneIntegrator::IntegrateStep(State, FDroneForces(), 1.f, 1.f);

	TestEqual(TEXT("yaw advances by angular velocity * dt"), Next.Orientation.Yaw, 90.0, 0.01);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
