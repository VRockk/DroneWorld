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

	// The preset that configures this drone, applied on BeginPlay. It is the single source of the drone's
	// tuning - flight model, assist, imperfection, gimbal, rates, crash, and ground friction - so it must
	// be set. The fields below are populated from it by ApplyPreset; they are not authored here.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UDronePreset> Preset;

	// Runtime configuration copied from the Preset by ApplyPreset. These are hidden from the component's
	// details panel because the Preset owns the values - editing them here would only be overwritten on
	// BeginPlay - and are exposed solely for Blueprint reads and the pawn's runtime use.

	// The assist mode the drone flies in when hover is not engaged. Defaults to Angle so it self-levels.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	EDroneAssistMode BaseAssistMode = EDroneAssistMode::Angle;

	// Self-leveling stiffness fed to the Angle-mode correction.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	float LevelingStrength = 6.f;

	// The Flight Model that computes this drone's force and torque.
	UPROPERTY(Instanced, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UDroneFlightModel> FlightModel;

	// The Imperfection Layer tuning - bob, drift, wind susceptibility, motor lag - applied uniformly on
	// top of whatever the flight model computes.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	FImperfectionParams Imperfection;

	// The onboard camera's mount config - tilt range, rest angle, and stabilization. The pawn reads it to
	// drive the gimbal that carries both the flatscreen view and the VR Feed.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	FGimbalConfig Gimbal;

	// The stick shaping - deadzone, expo, and rate scale. The pawn reads it to shape raw sticks into
	// Control Intent, so the drone's feel is a per-preset trait.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	FDroneRates Rates;

	// The closing speed (cm/s) at or above which a contact crashes the drone; below it it is a bump.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	float CrashThreshold = 800.f;

	// The fraction of its impact velocity the drone keeps when it crashes, 0 (dead stop) .. 1 (no loss),
	// so the wreck tumbles off with some momentum rather than stopping dead.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	float CrashMomentumRetention = 0.4f;

	// Ground friction deceleration (cm/s^2) applied to a disarmed or crashed drone resting on a surface,
	// so it scrubs to a stop instead of sliding along forever on the velocity it carried in. A flying
	// drone is held by its motors and feels only aerodynamic drag, so this acts only when the motors are
	// dead and the drone is grounded.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	float GroundFrictionDeceleration = 3000.f;

	// Attitude settling stiffness (1/s^2) for a disarmed or crashed drone resting on the ground: how hard
	// it rotates to lie flat against the slope. Acts only when the motors are dead and the drone is
	// grounded; a drone that landed inverted settles onto its back rather than righting itself.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Drone")
	float GroundSettleStrength = 10.f;

	// Fired when a contact crashes this drone. The GameMode listens to respawn; this component never
	// respawns or scores itself.
	UPROPERTY(BlueprintAssignable, Category = "Drone")
	FOnDroneCrashed OnCrashed;

	// Whether the drone is currently crashed - dead to the sticks, falling and tumbling under gravity.
	UFUNCTION(BlueprintPure, Category = "Drone")
	bool IsCrashed() const { return bCrashed; }

	// Whether the drone is resting on a surface, refreshed from the downward ground probe each tick.
	// More than friction and settling care that the drone is down: while grounded the Imperfection
	// Layer's drift and wind are suppressed so a parked drone is not skated across the floor, and other
	// systems can read it too.
	UFUNCTION(BlueprintPure, Category = "Drone")
	bool IsGrounded() const { return bGrounded; }

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

	// Whether the drone is resting on a surface this tick, refreshed from the downward ground probe at
	// the top of each tick and read by friction, ground settling, and the Imperfection Layer.
	bool bGrounded = false;

	// Enter the Crash state: latch bCrashed, impart a tumble spin, and announce the crash once so the
	// GameMode can respawn. Ignored if already crashed, so a tumbling wreck striking more geometry does
	// not re-fire the event.
	void EnterCrash();

	// Probe straight down for a supporting surface and report its normal in OutNormal. A drone gliding
	// flat across the floor sweeps into nothing horizontally, so the move's own hit misses the contact;
	// this short downward sweep finds the ground whenever the drone is actually resting on it, so ground
	// friction can act. Returns false when the drone is airborne.
	bool FindGroundContact(FVector& OutNormal) const;
};
