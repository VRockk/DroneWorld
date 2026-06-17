#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Flight/DroneFlightTypes.h"
#include "DroneMovementComponent.generated.h"

class UDroneFlightModel;
class UDronePreset;

// The drone's kinematic movement: a thin orchestrator that, each tick, feeds the current Control
// Intent and motion state through the Flight Model to get force and angular acceleration, advances
// them with a semi-implicit Euler integrator, and applies the result via swept collision. The
// type-specific force law lives in the swappable Flight Model; integration and collision are shared.
UCLASS(ClassGroup = (Drone), meta = (BlueprintSpawnableComponent))
class DRONEWORLD_API UDroneMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UDroneMovementComponent();

	// Set the pilot command consumed on subsequent ticks. The single command surface for every
	// input source - gamepad, keyboard, and the AI all call this.
	UFUNCTION(BlueprintCallable, Category = "Drone")
	void SetControlIntent(const FDroneControlIntent& InIntent) { CurrentIntent = InIntent; }

	// Install a preset's flight model onto this component. The model is copied into the component, so
	// each drone tunes its own instance. Called on BeginPlay when Preset is set, and runtime-callable.
	UFUNCTION(BlueprintCallable, Category = "Drone")
	void ApplyPreset(const UDronePreset* InPreset);

	// The preset applied on BeginPlay. Leave unset to keep the default flight model.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UDronePreset> Preset;

	// The assist mode the drone flies in when hover is not engaged; copied from the preset by
	// ApplyPreset. Defaults to Angle so an unconfigured drone self-levels.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone")
	EDroneAssistMode BaseAssistMode = EDroneAssistMode::Angle;

	// Self-leveling stiffness fed to the Angle-mode correction; copied from the preset by ApplyPreset.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone")
	float LevelingStrength = 6.f;

	// The instanced, inline-editable Flight Model that computes this drone's force and torque.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UDroneFlightModel> FlightModel;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// The latest pilot command; persists across ticks until the next SetControlIntent.
	FDroneControlIntent CurrentIntent;

	// Body-axis angular velocity (deg/s) carried between ticks; linear velocity lives in the
	// inherited Velocity member so other engine systems observe it.
	FVector AngularVelocity = FVector::ZeroVector;

	// The point the drone holds while hovering, captured when hover is first engaged.
	FVector HoverHoldLocation = FVector::ZeroVector;

	// Whether hover was engaged on the previous tick, so the hold point is captured only on the
	// rising edge rather than reset every frame.
	bool bWasHovering = false;
};
