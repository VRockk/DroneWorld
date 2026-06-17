#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Flight/DroneCrash.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneCrashHardImpactCrashes,
	"DroneWorld.Crash.HardImpactAtOrAboveThresholdCrashes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneCrashHardImpactCrashes::RunTest(const FString& Parameters)
{
	// A contact at or above the threshold ends the flight: the drone crashes rather than bumping off.
	TestTrue(TEXT("an impact exactly at the threshold crashes"), DroneFlight::ImpactCrashes(800.f, 800.f));
	TestTrue(TEXT("an impact above the threshold crashes"), DroneFlight::ImpactCrashes(1200.f, 800.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneCrashGentleContactBumps,
	"DroneWorld.Crash.GentleContactBelowThresholdBumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneCrashGentleContactBumps::RunTest(const FString& Parameters)
{
	// A contact below the threshold is a harmless bump: the drone keeps flying rather than crashing.
	TestFalse(TEXT("a slow contact below the threshold does not crash"), DroneFlight::ImpactCrashes(200.f, 800.f));
	TestFalse(TEXT("a touch just shy of the threshold does not crash"), DroneFlight::ImpactCrashes(799.f, 800.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneCrashThresholdIsPerPreset,
	"DroneWorld.Crash.ThresholdIsPerPreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneCrashThresholdIsPerPreset::RunTest(const FString& Parameters)
{
	// The same impact decides differently per preset: a flimsy drone with a low threshold crashes on a
	// contact a rugged drone with a high threshold shrugs off, so drone toughness genuinely matters.
	const float ImpactSpeed = 600.f;
	const float FlimsyThreshold = 400.f;
	const float RuggedThreshold = 900.f;

	TestTrue(TEXT("the flimsy drone crashes at this impact"), DroneFlight::ImpactCrashes(ImpactSpeed, FlimsyThreshold));
	TestFalse(TEXT("the rugged drone survives the same impact"), DroneFlight::ImpactCrashes(ImpactSpeed, RuggedThreshold));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneCrashHeadOnImpactReadsFullSpeed,
	"DroneWorld.Crash.HeadOnImpactReadsFullClosingSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneCrashHeadOnImpactReadsFullSpeed::RunTest(const FString& Parameters)
{
	// Flying straight into a wall, the impact speed is the full travel speed: the velocity is entirely
	// directed into the surface, so all of it counts toward the crash decision.
	const FVector Velocity(1000.f, 0.f, 0.f);   // heading +X
	const FVector WallNormal(-1.f, 0.f, 0.f);   // wall faces back along -X

	const float Speed = DroneFlight::ComputeImpactSpeed(Velocity, WallNormal);
	TestTrue(TEXT("a head-on impact reads the full travel speed"), FMath::IsNearlyEqual(Speed, 1000.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneCrashGlancingSlideReadsNearZeroSpeed,
	"DroneWorld.Crash.GlancingSlideReadsNearZeroSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneCrashGlancingSlideReadsNearZeroSpeed::RunTest(const FString& Parameters)
{
	// Sliding fast along a wall is not an impact: the velocity is parallel to the surface, so none of it
	// is directed into the wall and the closing speed is ~zero. A fast graze stays a harmless bump.
	const FVector Velocity(1000.f, 0.f, 0.f);   // moving along +X, parallel to the wall
	const FVector WallNormal(0.f, 1.f, 0.f);    // wall faces +Y

	const float Speed = DroneFlight::ComputeImpactSpeed(Velocity, WallNormal);
	TestTrue(TEXT("a glancing slide along the wall reads ~zero closing speed"), FMath::IsNearlyEqual(Speed, 0.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneCrashMovingAwayReadsZeroSpeed,
	"DroneWorld.Crash.MovingAwayReadsZeroSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneCrashMovingAwayReadsZeroSpeed::RunTest(const FString& Parameters)
{
	// Velocity pointing away from the surface is not a closing contact at all; the speed clamps to zero
	// rather than going negative, so a separating touch never reads as an impact.
	const FVector Velocity(-1000.f, 0.f, 0.f);  // moving away from a wall that faces -X
	const FVector WallNormal(-1.f, 0.f, 0.f);

	const float Speed = DroneFlight::ComputeImpactSpeed(Velocity, WallNormal);
	TestTrue(TEXT("separating motion clamps to zero closing speed"), FMath::IsNearlyEqual(Speed, 0.f, 0.01f));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
