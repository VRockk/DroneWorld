#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Flight/DroneFriction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFrictionScrubsVelocityAlongSurface,
	"DroneWorld.Friction.ScrubsVelocityAlongSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFrictionScrubsVelocityAlongSurface::RunTest(const FString& Parameters)
{
	// Sliding across flat ground (normal up), friction bleeds the horizontal speed: the drone moves the
	// same direction but slower, so it scrubs toward a stop instead of gliding on forever.
	const FVector Sliding(600.f, 0.f, 0.f);
	const FVector Up(0.f, 0.f, 1.f);

	const FVector After = DroneFlight::ApplySurfaceFriction(Sliding, Up, /*FrictionDeceleration*/ 1000.f, /*Dt*/ 0.1f);

	TestTrue(TEXT("friction reduces the slide speed"), After.Size() < Sliding.Size());
	TestTrue(TEXT("the slide keeps its direction while it has speed"), After.X > 0.f);
	// 600 cm/s losing 1000 cm/s^2 * 0.1s = 100 cm/s leaves 500 cm/s.
	TestEqual(TEXT("constant deceleration removes decel*dt of speed"), After.X, 500.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFrictionPreservesIntoSurfaceComponent,
	"DroneWorld.Friction.PreservesIntoSurfaceComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFrictionPreservesIntoSurfaceComponent::RunTest(const FString& Parameters)
{
	// Friction acts only across the surface, never along its normal: the component pressing into the
	// ground (gravity pulling the drone down onto it) passes through untouched, so friction cannot make
	// the drone sink into or lift off the surface.
	const FVector Velocity(600.f, 0.f, -200.f);
	const FVector Up(0.f, 0.f, 1.f);

	const FVector After = DroneFlight::ApplySurfaceFriction(Velocity, Up, /*FrictionDeceleration*/ 1000.f, /*Dt*/ 0.1f);

	TestEqual(TEXT("the into-surface (vertical) component is preserved"), After.Z, -200.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFrictionStopsRatherThanReversing,
	"DroneWorld.Friction.StopsRatherThanReversing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFrictionStopsRatherThanReversing::RunTest(const FString& Parameters)
{
	// Friction can only bring a slide to rest, never drive it backwards: a deceleration that exceeds the
	// remaining tangential speed in this step settles it to exactly zero rather than flinging it the
	// other way.
	const FVector Crawling(50.f, 0.f, 0.f);
	const FVector Up(0.f, 0.f, 1.f);

	const FVector After = DroneFlight::ApplySurfaceFriction(Crawling, Up, /*FrictionDeceleration*/ 1000.f, /*Dt*/ 0.1f);

	TestEqual(TEXT("an over-strong scrub stops the slide dead"), After.X, 0.0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
