#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "View/DroneViewSelection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneViewVrPilotWithHmdGetsPilotStation,
	"DroneWorld.View.LocalPilotWithHmdGetsPilotStation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneViewVrPilotWithHmdGetsPilotStation::RunTest(const FString& Parameters)
{
	// The locally-controlled human pilot wearing an HMD gets the full VR setup: a Pilot Station shows the
	// drone's camera as a Feed in the Void.
	const DroneView::EDroneViewMode Mode = DroneView::SelectViewMode(/*bIsLocallyControlled*/ true, /*bHmdEnabled*/ true);
	TestTrue(TEXT("a local pilot with an HMD views through a Pilot Station"), Mode == DroneView::EDroneViewMode::VrPilotStation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneViewLocalPilotNoHmdViewsOnboard,
	"DroneWorld.View.LocalPilotWithoutHmdViewsOnboardDirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneViewLocalPilotNoHmdViewsOnboard::RunTest(const FString& Parameters)
{
	// A locally-controlled pilot with no HMD flies flatscreen: the view target is the drone's onboard
	// camera directly, so no Feed capture or Pilot Station is created.
	const DroneView::EDroneViewMode Mode = DroneView::SelectViewMode(/*bIsLocallyControlled*/ true, /*bHmdEnabled*/ false);
	TestTrue(TEXT("a local pilot without an HMD views the onboard camera directly"), Mode == DroneView::EDroneViewMode::OnboardDirect);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneViewNonLocalNeverGetsPilotStation,
	"DroneWorld.View.NonLocalPawnNeverGetsPilotStation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneViewNonLocalNeverGetsPilotStation::RunTest(const FString& Parameters)
{
	// AI drones and non-local pawns never stand up the VR machinery: even if an HMD is present on the
	// machine, a pawn that is not locally controlled views the onboard camera directly - no Pilot Station,
	// no Feed capture - so the Void's cost is paid only for the local VR pilot.
	const DroneView::EDroneViewMode Mode = DroneView::SelectViewMode(/*bIsLocallyControlled*/ false, /*bHmdEnabled*/ true);
	TestTrue(TEXT("a non-local pawn never views through a Pilot Station"), Mode == DroneView::EDroneViewMode::OnboardDirect);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
