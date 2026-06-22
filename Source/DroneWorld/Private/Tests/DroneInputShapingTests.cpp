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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputDisarmedProducesNoMotorCommand,
	"DroneWorld.Input.DisarmedProducesNoMotorCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputDisarmedProducesNoMotorCommand::RunTest(const FString& Parameters)
{
	// A disarmed drone makes no motor command however hard the pilot pushes: full throttle and full
	// stick on every rotational axis all gate to zero, so the motors do not respond until armed.
	FDroneControlIntent Sticks;
	Sticks.bArmed = false;
	Sticks.Throttle = 1.f;
	Sticks.Yaw = 1.f;
	Sticks.Pitch = -1.f;
	Sticks.Roll = 0.5f;

	const FDroneControlIntent Gated = DroneInput::GateMotors(Sticks);

	TestEqual(TEXT("disarmed throttle is cut"), Gated.Throttle, 0.f);
	TestEqual(TEXT("disarmed yaw is cut"), Gated.Yaw, 0.f);
	TestEqual(TEXT("disarmed pitch is cut"), Gated.Pitch, 0.f);
	TestEqual(TEXT("disarmed roll is cut"), Gated.Roll, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputArmedPassesCommandThrough,
	"DroneWorld.Input.ArmedPassesCommandThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputArmedPassesCommandThrough::RunTest(const FString& Parameters)
{
	// Once armed the gate is transparent: the pilot's command reaches the motors untouched, and the
	// mode flags survive the gate in either state.
	FDroneControlIntent Sticks;
	Sticks.bArmed = true;
	Sticks.bHoverEngaged = true;
	Sticks.Throttle = 0.7f;
	Sticks.Yaw = -0.3f;
	Sticks.Pitch = 0.2f;
	Sticks.Roll = 0.1f;

	const FDroneControlIntent Gated = DroneInput::GateMotors(Sticks);

	TestEqual(TEXT("armed throttle passes through"), Gated.Throttle, 0.7f);
	TestEqual(TEXT("armed yaw passes through"), Gated.Yaw, -0.3f);
	TestEqual(TEXT("armed pitch passes through"), Gated.Pitch, 0.2f);
	TestEqual(TEXT("armed roll passes through"), Gated.Roll, 0.1f);
	TestTrue(TEXT("the arm flag survives the gate"), Gated.bArmed);
	TestTrue(TEXT("the hover flag survives the gate"), Gated.bHoverEngaged);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputRateScalesShapedOutput,
	"DroneWorld.Input.RateScalesShapedOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputRateScalesShapedOutput::RunTest(const FString& Parameters)
{
	// The rate scales the shaped stick: doubling the rate doubles the commanded demand for the same
	// stick, while a rate of 1 leaves the deadzone+expo curve exactly as it is.
	FDroneRates Unity;
	Unity.Deadzone = 0.f;
	Unity.Expo = 0.5f;
	Unity.RateScale = 1.f;

	FDroneRates Punchy = Unity;
	Punchy.RateScale = 2.f;

	const float Base = DroneInput::ShapeAxis(0.5f, Unity);
	const float Scaled = DroneInput::ShapeAxis(0.5f, Punchy);

	TestEqual(TEXT("rate of 1 matches the bare deadzone+expo curve"), Base, DroneInput::ShapeAxis(0.5f, 0.f, 0.5f));
	TestEqual(TEXT("doubling the rate doubles the demand"), Scaled, 2.f * Base);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputPerPresetRatesDifferInFeel,
	"DroneWorld.Input.PerPresetRatesDifferInFeel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputPerPresetRatesDifferInFeel::RunTest(const FString& Parameters)
{
	// Two presets with different rates respond with a different feel for the identical stick: the punchy
	// drone commands more than the docile one at the same deflection. This is the headless half of the
	// "visibly different feel" criterion (the visual half is verified in-editor).
	FDroneRates Docile;
	Docile.RateScale = 0.6f;

	FDroneRates Punchy;
	Punchy.RateScale = 1.8f;

	const float Stick = 0.7f;
	const float DocileOut = DroneInput::ShapeAxis(Stick, Docile);
	const float PunchyOut = DroneInput::ShapeAxis(Stick, Punchy);

	TestTrue(TEXT("the punchy preset commands more than the docile one at the same stick"), PunchyOut > DocileOut);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneInputExpoShapeSmallSmallNearFullNearFull,
	"DroneWorld.Input.ExpoShapeSmallSmallNearFullNearFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneInputExpoShapeSmallSmallNearFullNearFull::RunTest(const FString& Parameters)
{
	// The expo curve through a preset's rates keeps a small stick small and a near-full stick near-full:
	// fine control around center without losing authority at the edge. At unit rate the shaped output
	// stays within [0, stick] for a softening curve, with full deflection preserved.
	FDroneRates Rates;
	Rates.Deadzone = 0.f;
	Rates.Expo = 0.6f;
	Rates.RateScale = 1.f;

	const float Small = DroneInput::ShapeAxis(0.1f, Rates);
	const float NearFull = DroneInput::ShapeAxis(0.9f, Rates);
	const float Full = DroneInput::ShapeAxis(1.f, Rates);

	TestTrue(TEXT("a small stick stays small"), Small > 0.f && Small < 0.1f);
	TestTrue(TEXT("a near-full stick stays near full"), NearFull > 0.7f && NearFull < 0.9f);
	TestEqual(TEXT("full deflection is preserved"), Full, 1.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
