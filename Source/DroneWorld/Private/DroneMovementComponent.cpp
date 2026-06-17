#include "DroneMovementComponent.h"
#include "DronePreset.h"
#include "DroneWindSubsystem.h"
#include "Flight/DroneFlightModel.h"
#include "Flight/QuadFlightModel.h"
#include "Flight/DroneAssist.h"
#include "Flight/DroneIntegrator.h"
#include "Flight/DroneImperfection.h"
#include "Engine/World.h"

UDroneMovementComponent::UDroneMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// A quadcopter with reasonable defaults so a freshly placed drone flies without further setup.
	FlightModel = CreateDefaultSubobject<UQuadFlightModel>(TEXT("FlightModel"));
}

void UDroneMovementComponent::ApplyPreset(const UDronePreset* InPreset)
{
	if (!InPreset || !InPreset->FlightModel)
	{
		return;
	}

	// Copy the preset's model into this component so each drone owns and tunes its own instance,
	// rather than sharing the asset's subobject across every drone built from the same preset.
	FlightModel = DuplicateObject<UDroneFlightModel>(InPreset->FlightModel, this);

	// Adopt the preset's assist behavior so the drone flies in its chosen mode with its leveling tuning.
	BaseAssistMode = InPreset->BaseAssistMode;
	LevelingStrength = InPreset->LevelingStrength;

	// Adopt the preset's imperfection tuning so this drone bobs, drifts, feels the wind, and spools its
	// motors to its own character.
	Imperfection = InPreset->Imperfection;
}

void UDroneMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// Seed the bob and drift from this instance so drones placed together do not wander in lockstep.
	ImperfectionSeed = (int32)GetUniqueID();

	if (Preset)
	{
		ApplyPreset(Preset);
	}
}

void UDroneMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!UpdatedComponent || !FlightModel || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	// Read the current motion state from the component being moved.
	FDroneFlightState State;
	State.Location = UpdatedComponent->GetComponentLocation();
	State.Orientation = UpdatedComponent->GetComponentRotation();
	State.Velocity = Velocity;
	State.AngularVelocity = AngularVelocity;

	// Spool the realized throttle toward the command so thrust ramps in rather than snapping, then fly
	// the flight model on the lagged throttle. The stick demand on the other axes is unaffected.
	SpooledThrottle = DroneFlight::StepMotorLag(SpooledThrottle, CurrentIntent.Throttle, Imperfection.MotorLagTau, DeltaTime);
	FDroneControlIntent Intent = CurrentIntent;
	Intent.Throttle = SpooledThrottle;

	// Advance the imperfection clock once per tick so the hover wander and the additive perturbation
	// below read the same moment.
	ImperfectionTime += DeltaTime;

	// Flight Model -> integrator: turn intent and state into the next state. The hover toggle overrides
	// the base assist mode while engaged; otherwise the base mode (Acro/Angle) flies.
	FDroneForces Forces;
	if (Intent.bHoverEngaged)
	{
		// Capture the hold point on the rising edge so hover parks where it was engaged, not where the
		// drone has since drifted. The Flight Model decides what holding means for its airframe.
		if (!bWasHovering)
		{
			HoverHoldLocation = State.Location;
			bWasHovering = true;
		}

		// Wander the hold point with the coherent-noise drift so a hovering drone gently circles its
		// spot rather than freezing rigidly on the setpoint. The position-hold spring tracks this moving
		// target instead of fighting a drift force, so horizontal drift is applied here rather than as a
		// force in the Imperfection Layer below.
		const FVector DriftingHold = HoverHoldLocation + DroneFlight::ComputeDriftOffset(Imperfection, ImperfectionSeed, ImperfectionTime);
		Forces = FlightModel->ComputeHoverForces(Intent, State, DriftingHold);
	}
	else
	{
		bWasHovering = false;
		Forces = FlightModel->ComputeForces(Intent, State);

		// Angle mode adds a self-leveling torque on top of the pilot's command; Acro adds nothing.
		Forces.Torque += DroneFlight::ComputeLevelingTorque(BaseAssistMode, Intent, State, LevelingStrength);
	}

	// The Imperfection Layer perturbs the ideal force and torque after the flight model, uniformly across
	// models: hover bob, coherent-noise drift (in free flight; while hovering it instead wanders the hold
	// point above), the world wind scaled by this drone's susceptibility, and a slight roll/pitch wobble.
	// The world supplies the wind; a level with no wind subsystem simply feels none.
	FVector WorldWind = FVector::ZeroVector;
	if (const UWorld* World = GetWorld())
	{
		if (const UDroneWindSubsystem* WindSubsystem = World->GetSubsystem<UDroneWindSubsystem>())
		{
			WorldWind = WindSubsystem->GetCurrentWind();
		}
	}
	const FDroneForces Imperfect = DroneFlight::ComputeImperfectionForces(Imperfection, ImperfectionSeed, ImperfectionTime, WorldWind, Intent.bHoverEngaged);
	Forces.Force += Imperfect.Force;
	Forces.Torque += Imperfect.Torque;

	const FDroneFlightState Next = DroneIntegrator::IntegrateStep(State, Forces, FlightModel->GetMass(), DeltaTime);

	// Apply with swept collision so world geometry blocks the drone; slide along blocking surfaces.
	const FVector Delta = Next.Location - State.Location;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, Next.Orientation.Quaternion(), /*bSweep*/ true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, /*bHandleImpact*/ true);
	}

	// Recover linear velocity from the distance actually travelled, so a blocked move sheds the
	// velocity it could not realize. Angular velocity carries forward unimpeded.
	Velocity = (UpdatedComponent->GetComponentLocation() - State.Location) / DeltaTime;
	AngularVelocity = Next.AngularVelocity;
	UpdateComponentVelocity();
}
