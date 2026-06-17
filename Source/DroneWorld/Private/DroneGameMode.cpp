#include "DroneGameMode.h"
#include "DronePawn.h"
#include "DroneMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

void ADroneGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Bind the drones already placed in the level, then catch any spawned later (e.g. AI drones), so
	// every drone's crash is handled centrally without the pawn knowing about the GameMode.
	for (TActorIterator<ADronePawn> It(World); It; ++It)
	{
		RegisterDrone(*It);
	}

	World->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ADroneGameMode::OnActorSpawned));
}

void ADroneGameMode::OnActorSpawned(AActor* SpawnedActor)
{
	if (ADronePawn* Drone = Cast<ADronePawn>(SpawnedActor))
	{
		RegisterDrone(Drone);
	}
}

void ADroneGameMode::RegisterDrone(ADronePawn* Drone)
{
	if (!Drone)
	{
		return;
	}

	UDroneMovementComponent* Movement = Drone->FindComponentByClass<UDroneMovementComponent>();
	if (!Movement)
	{
		return;
	}

	// Remember where the drone started so a crash returns it there, and bind its crash once.
	StartTransforms.Add(Drone, Drone->GetActorTransform());
	if (!Movement->OnCrashed.IsAlreadyBound(this, &ADroneGameMode::HandleDroneCrashed))
	{
		Movement->OnCrashed.AddDynamic(this, &ADroneGameMode::HandleDroneCrashed);
	}
}

void ADroneGameMode::HandleDroneCrashed(APawn* CrashedPawn)
{
	if (!CrashedPawn)
	{
		return;
	}

	// Let the wreck fall and tumble for a moment before respawning, so the crash reads rather than the
	// drone snapping back instantly. Respawn immediately if no delay is configured.
	if (RespawnDelay <= 0.f)
	{
		RespawnDrone(CrashedPawn);
		return;
	}

	FTimerHandle Handle;
	FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &ADroneGameMode::RespawnDrone, CrashedPawn);
	GetWorldTimerManager().SetTimer(Handle, Delegate, RespawnDelay, /*bLoop*/ false);
}

void ADroneGameMode::RespawnDrone(APawn* Drone)
{
	if (!Drone)
	{
		return;
	}

	const FTransform* Start = StartTransforms.Find(Drone);
	if (!Start)
	{
		return;
	}

	// Put the drone back where it started and hand control back to the pilot. Teleport the physics so
	// no residual sweep drags it through the world from the crash site.
	Drone->SetActorTransform(*Start, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	if (UDroneMovementComponent* Movement = Drone->FindComponentByClass<UDroneMovementComponent>())
	{
		Movement->RecoverFromCrash();
	}
}
