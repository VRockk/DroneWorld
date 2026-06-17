#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DronePreset.h"
#include "DroneMovementComponent.h"
#include "Flight/QuadFlightModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDronePresetAppliesFlightModelToMovementComponent,
	"DroneWorld.Preset.AppliesFlightModelToMovementComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDronePresetAppliesFlightModelToMovementComponent::RunTest(const FString& Parameters)
{
	// A preset carries an instanced flight model; applying it installs that model on the movement
	// component so the component flies by the preset's parameters.
	UDronePreset* Preset = NewObject<UDronePreset>(GetTransientPackage());
	UQuadFlightModel* Model = NewObject<UQuadFlightModel>(Preset);
	Model->Params.Mass = 12.34f;
	Preset->FlightModel = Model;

	UDroneMovementComponent* Movement = NewObject<UDroneMovementComponent>(GetTransientPackage());
	Movement->ApplyPreset(Preset);

	TestNotNull(TEXT("movement component has a flight model after ApplyPreset"), Movement->FlightModel.Get());
	if (Movement->FlightModel)
	{
		TestEqual(TEXT("installed model reports the preset's mass"), Movement->FlightModel->GetMass(), 12.34f);
	}

	// The installed model is the component's own instance, not a shared reference to the preset's,
	// so tuning one drone at runtime cannot bleed into another sharing the preset.
	TestTrue(TEXT("installed model is a distinct instance from the preset's"), Movement->FlightModel.Get() != Model);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDronePresetDefaultsToAngleAssist,
	"DroneWorld.Preset.DefaultsToAngleAssist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDronePresetDefaultsToAngleAssist::RunTest(const FString& Parameters)
{
	// Self-leveling flight is the approachable default, so a fresh preset is in Angle mode until a
	// designer chooses otherwise.
	UDronePreset* Preset = NewObject<UDronePreset>(GetTransientPackage());

	TestEqual(TEXT("a new preset defaults to Angle assist"), Preset->BaseAssistMode, EDroneAssistMode::Angle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDronePresetAppliesAssistConfigToMovementComponent,
	"DroneWorld.Preset.AppliesAssistConfigToMovementComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDronePresetAppliesAssistConfigToMovementComponent::RunTest(const FString& Parameters)
{
	// Applying a preset configures the component's assist behavior, not just its flight model, so the
	// drone flies in the preset's chosen mode with its leveling tuning.
	UDronePreset* Preset = NewObject<UDronePreset>(GetTransientPackage());
	Preset->FlightModel = NewObject<UQuadFlightModel>(Preset);
	Preset->BaseAssistMode = EDroneAssistMode::Acro;
	Preset->LevelingStrength = 42.f;

	UDroneMovementComponent* Movement = NewObject<UDroneMovementComponent>(GetTransientPackage());
	Movement->ApplyPreset(Preset);

	TestEqual(TEXT("component adopts the preset's base assist mode"), Movement->BaseAssistMode, EDroneAssistMode::Acro);
	TestEqual(TEXT("component adopts the preset's leveling strength"), Movement->LevelingStrength, 42.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDronePresetAppliesImperfectionConfigToMovementComponent,
	"DroneWorld.Preset.AppliesImperfectionConfigToMovementComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDronePresetAppliesImperfectionConfigToMovementComponent::RunTest(const FString& Parameters)
{
	// Applying a preset hands the component its Imperfection Layer tuning, so this drone bobs, drifts,
	// feels the wind, and spools its motors with the character the designer set - a light drone tuned to
	// be shoved harder than a heavy one carries that susceptibility onto the component.
	UDronePreset* Preset = NewObject<UDronePreset>(GetTransientPackage());
	Preset->FlightModel = NewObject<UQuadFlightModel>(Preset);
	Preset->Imperfection.WindSusceptibility = 3.5f;
	Preset->Imperfection.MotorLagTau = 0.25f;

	UDroneMovementComponent* Movement = NewObject<UDroneMovementComponent>(GetTransientPackage());
	Movement->ApplyPreset(Preset);

	TestEqual(TEXT("component adopts the preset's wind susceptibility"), Movement->Imperfection.WindSusceptibility, 3.5f);
	TestEqual(TEXT("component adopts the preset's motor lag"), Movement->Imperfection.MotorLagTau, 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDronePresetAppliesGimbalConfigToMovementComponent,
	"DroneWorld.Preset.AppliesGimbalConfigToMovementComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDronePresetAppliesGimbalConfigToMovementComponent::RunTest(const FString& Parameters)
{
	// Applying a preset hands the component its gimbal config, so the onboard camera tilts within this
	// drone's range and rests at its default - a steep FPV uptilt on one drone, a gentler look-ahead on
	// another.
	UDronePreset* Preset = NewObject<UDronePreset>(GetTransientPackage());
	Preset->FlightModel = NewObject<UQuadFlightModel>(Preset);
	Preset->Gimbal.MaxTiltDegrees = 60.f;
	Preset->Gimbal.DefaultTiltDegrees = 25.f;

	UDroneMovementComponent* Movement = NewObject<UDroneMovementComponent>(GetTransientPackage());
	Movement->ApplyPreset(Preset);

	TestEqual(TEXT("component adopts the preset's max tilt"), Movement->Gimbal.MaxTiltDegrees, 60.f);
	TestEqual(TEXT("component adopts the preset's default tilt"), Movement->Gimbal.DefaultTiltDegrees, 25.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
