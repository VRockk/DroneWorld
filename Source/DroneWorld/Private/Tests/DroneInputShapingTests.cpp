#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Input/DroneInputShaping.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputDeadzoneZeroesSmallInput,
	"DroneWorld.Input.DeadzoneZeroesSmallInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputDeadzoneZeroesSmallInput::RunTest(const FString& Parameters)
{
	// Inside the deadzone the axis reads zero; just outside it begins to respond.
	TestEqual(TEXT("small positive input is zeroed"), DroneInput::ShapeAxis(0.05f, 0.1f, 0.f), 0.f);
	TestEqual(TEXT("small negative input is zeroed"), DroneInput::ShapeAxis(-0.05f, 0.1f, 0.f), 0.f);
	TestTrue(TEXT("input beyond the deadzone is non-zero"), DroneInput::ShapeAxis(0.5f, 0.1f, 0.f) > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputNoDeadzoneNoExpoIsLinear,
	"DroneWorld.Input.NoDeadzoneNoExpoIsLinear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputNoDeadzoneNoExpoIsLinear::RunTest(const FString& Parameters)
{
	// With no deadzone and no expo the axis passes through unchanged, including full deflection and sign.
	TestEqual(TEXT("half stick stays half"), DroneInput::ShapeAxis(0.5f, 0.f, 0.f), 0.5f);
	TestEqual(TEXT("full stick stays full"), DroneInput::ShapeAxis(1.f, 0.f, 0.f), 1.f);
	TestEqual(TEXT("full negative stick stays full negative"), DroneInput::ShapeAxis(-1.f, 0.f, 0.f), -1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputExpoSoftensMidStick,
	"DroneWorld.Input.ExpoSoftensMidStick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputExpoSoftensMidStick::RunTest(const FString& Parameters)
{
	// Full expo is a pure cubic: half stick reads 0.125, while full deflection is preserved at 1.
	TestEqual(TEXT("full expo at half stick is cubic"), DroneInput::ShapeAxis(0.5f, 0.f, 1.f), 0.125f);
	TestEqual(TEXT("full expo preserves full deflection"), DroneInput::ShapeAxis(1.f, 0.f, 1.f), 1.f);
	TestTrue(TEXT("expo softens mid-stick vs linear"),
		DroneInput::ShapeAxis(0.5f, 0.f, 1.f) < DroneInput::ShapeAxis(0.5f, 0.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputMode2AxisMapping,
	"DroneWorld.Input.Mode2AxisMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputMode2AxisMapping::RunTest(const FString& Parameters)
{
	// Mode 2: left stick = throttle (Y) + yaw (X); right stick = pitch (Y) + roll (X).
	const FDroneControlIntent Intent = DroneInput::MapMode2(/*LeftX*/ 0.2f, /*LeftY*/ 1.f, /*RightX*/ 0.3f, /*RightY*/ 0.4f);

	TestEqual(TEXT("left X drives yaw"), Intent.Yaw, 0.2f);
	TestEqual(TEXT("right Y drives pitch"), Intent.Pitch, 0.4f);
	TestEqual(TEXT("right X drives roll"), Intent.Roll, 0.3f);
	TestEqual(TEXT("full-up left Y is full throttle"), Intent.Throttle, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputMode2ThrottleMapping,
	"DroneWorld.Input.Mode2ThrottleMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputMode2ThrottleMapping::RunTest(const FString& Parameters)
{
	// The throttle stick rests at zero and is pushed up to add thrust: centered and downward give no
	// throttle, so releasing the stick means no thrust (the drone descends, a fixed-wing slows). The
	// upper half of travel maps linearly to [0, 1].
	TestEqual(TEXT("centered throttle stick is zero"), DroneInput::MapMode2(0.f, 0.f, 0.f, 0.f).Throttle, 0.f);
	TestEqual(TEXT("downward throttle stick is zero"), DroneInput::MapMode2(0.f, -1.f, 0.f, 0.f).Throttle, 0.f);
	TestEqual(TEXT("half-up throttle stick is half"), DroneInput::MapMode2(0.f, 0.5f, 0.f, 0.f).Throttle, 0.5f);
	TestEqual(TEXT("full-up throttle stick is full"), DroneInput::MapMode2(0.f, 1.f, 0.f, 0.f).Throttle, 1.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
