#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Flight/DroneFlightTypes.h"
#include "Flight/DroneImperfection.h"
#include "DroneMovementComponent.generated.h"

class UDroneFlightModel;
class UDronePreset;

// Broadcast once when a contact crashes the drone, carrying the pawn that crashed. The movement
// component only announces the crash; what happens next - respawn, scoring - is the listener's job
// (the GameMode), so movement owns the Crash state but not the recovery policy.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDroneCrashed, APawn*, CrashedPawn);

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

	// The Imperfection Layer tuning - bob, drift, wind susceptibility, motor lag - copied from the
	// preset by ApplyPreset. Applied uniformly on top of whatever the flight model computes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ShowOnlyInnerProperties))
	FImperfectionParams Imperfection;

	// The onboard camera's mount config - tilt range, rest angle, and stabilization - copied from the
	// preset by ApplyPreset. The pawn reads it to drive the gimbal that carries both the flatscreen view
	// and the VR Feed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ShowOnlyInnerProperties))
	FGimbalConfig Gimbal;

	// The closing speed (cm/s) at or above which a contact crashes the drone; below it the contact is a
	// harmless bump. Copied from the preset by ApplyPreset.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ClampMin = "0.0"))
	float CrashThreshold = 800.f;

	// The fraction of its impact velocity the drone keeps when it crashes, 0 (dead stop) .. 1 (no loss),
	// so the wreck tumbles off with some momentum rather than stopping dead. Copied from the preset by
	// ApplyPreset.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrashMomentumRetention = 0.4f;

	// Fired when a contact crashes this drone. The GameMode listens to respawn; this component never
	// respawns or scores itself.
	UPROPERTY(BlueprintAssignable, Category = "Drone")
	FOnDroneCrashed OnCrashed;

	// Whether the drone is currently crashed - dead to the sticks, falling and tumbling under gravity.
	UFUNCTION(BlueprintPure, Category = "Drone")
	bool IsCrashed() const { return bCrashed; }

	// Return a crashed drone to flying: clear the Crash state and its tumble so the pilot has control
	// again. The GameMode calls this after placing the drone at a respawn transform.
	UFUNCTION(BlueprintCallable, Category = "Drone")
	void RecoverFromCrash();

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

	// The realized throttle after motor lag, spooling toward the commanded throttle across ticks so
	// thrust ramps in rather than snapping. Carried between ticks.
	float SpooledThrottle = 0.f;

	// Seconds of flight elapsed, advancing the deterministic bob and drift so the perturbation evolves
	// smoothly over time.
	float ImperfectionTime = 0.f;

	// A per-drone seed so two drones placed side by side bob and drift on independent paths instead of
	// in lockstep. Derived from the instance on BeginPlay.
	int32 ImperfectionSeed = 0;

	// Whether the drone is crashed: the sticks are cut and it falls and tumbles until the GameMode
	// respawns it. Set on a qualifying impact, cleared by RecoverFromCrash.
	bool bCrashed = false;

	// Enter the Crash state: latch bCrashed, impart a tumble spin, and announce the crash once so the
	// GameMode can respawn. Ignored if already crashed, so a tumbling wreck striking more geometry does
	// not re-fire the event.
	void EnterCrash();
};
