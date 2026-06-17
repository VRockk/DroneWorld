#include "DroneMovementComponent.h"
#include "DronePreset.h"
#include "DroneWindSubsystem.h"
#include "Flight/DroneFlightModel.h"
#include "Flight/QuadFlightModel.h"
#include "Flight/DroneAssist.h"
#include "Flight/DroneIntegrator.h"
#include "Flight/DroneImperfection.h"
#include "Flight/DroneCrash.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

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

	// Adopt the preset's crash toughness so a flimsy drone breaks on a knock a rugged one shrugs off,
	// and how much momentum the wreck keeps off the impact.
	CrashThreshold = InPreset->CrashThreshold;
	CrashMomentumRetention = InPreset->CrashMomentumRetention;
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

	// Flight Model -> integrator: turn intent and state into the next state. A crashed drone is dead to
	// the sticks; otherwise the hover toggle overrides the base assist mode while engaged, and failing
	// that the base mode (Acro/Angle) flies.
	FDroneForces Forces;
	if (bCrashed)
	{
		// Cut to a null command so only the flight model's gravity and drag remain - no thrust, no
		// commanded torque - and the drone falls. The tumble spin imparted on impact carries it over as
		// it drops. The Imperfection Layer is skipped: a wreck is no longer actively flying.
		Forces = FlightModel->ComputeForces(FDroneControlIntent(), State);
	}
	else
	{
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

		// The Imperfection Layer perturbs the ideal force and torque after the flight model, uniformly
		// across models: hover bob, coherent-noise drift (in free flight; while hovering it instead wanders
		// the hold point above), the world wind scaled by this drone's susceptibility, and a slight
		// roll/pitch wobble. The world supplies the wind; a level with no wind subsystem simply feels none.
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
	}

	const FDroneFlightState Next = DroneIntegrator::IntegrateStep(State, Forces, FlightModel->GetMass(), DeltaTime);

	// Apply with swept collision so world geometry blocks the drone; slide along blocking surfaces.
	const FVector Delta = Next.Location - State.Location;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, Next.Orientation.Quaternion(), /*bSweep*/ true, Hit);
	bool bJustCrashed = false;
	if (Hit.IsValidBlockingHit())
	{
		// A blocking contact crashes the drone if it strikes hard enough, otherwise it is a bump. The
		// closing speed is the velocity carried into the move projected onto the surface, so a head-on
		// hit at or above the threshold crashes while a glancing brush slides off.
		const float ImpactSpeed = DroneFlight::ComputeImpactSpeed(Next.Velocity, Hit.Normal);
		if (!bCrashed && DroneFlight::ImpactCrashes(ImpactSpeed, CrashThreshold))
		{
			EnterCrash();
			bJustCrashed = true;
		}

		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, /*bHandleImpact*/ true);
	}

	if (bJustCrashed)
	{
		// Keep a fraction of the impact velocity so the wreck tumbles off the crash with some momentum
		// rather than skating frictionlessly along the surface it struck at full speed (which would glide
		// along a wall barely falling). Gravity and drag bleed the rest as it drops, and the tumble spin
		// EnterCrash imparted carries over.
		Velocity = Next.Velocity * CrashMomentumRetention;
	}
	else
	{
		// Recover linear velocity from the distance actually travelled, so a blocked move sheds the
		// velocity it could not realize. Angular velocity carries forward unimpeded.
		Velocity = (UpdatedComponent->GetComponentLocation() - State.Location) / DeltaTime;
		AngularVelocity = Next.AngularVelocity;
	}
	UpdateComponentVelocity();
}

void UDroneMovementComponent::EnterCrash()
{
	if (bCrashed)
	{
		return;
	}

	bCrashed = true;

	// Impart a tumble so the wreck spins as it drops rather than falling flat; the flight model's angular
	// damping bleeds it off over the fall. Yaw is left out so the spin reads as an end-over-end tumble.
	AngularVelocity = FVector(220.f, 160.f, 0.f);

	OnCrashed.Broadcast(GetPawnOwner());
}

void UDroneMovementComponent::RecoverFromCrash()
{
	bCrashed = false;
	Velocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;
	SpooledThrottle = 0.f;
	bWasHovering = false;
	UpdateComponentVelocity();
}
