#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DroneGameMode.generated.h"

class APawn;
class ADronePawn;

// The mode that wires the scene and owns crash recovery. It listens for every drone's Crashed event
// and respawns that drone at the transform it started from, so the movement component can stay out of
// the respawn business entirely. Drones are discovered automatically - those already in the level and
// any spawned later - so a placed or AI-spawned drone needs no manual hook-up.
UCLASS()
class DRONEWORLD_API ADroneGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	// Seconds a crashed drone is left to fall and tumble before it respawns, so the crash is seen rather
	// than snapping the drone back instantly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone", meta = (ClampMin = "0.0"))
	float RespawnDelay = 2.f;

	// Record a drone's start transform and listen for its crash. Safe to call more than once for the
	// same drone; the listener is bound only once.
	UFUNCTION(BlueprintCallable, Category = "Drone")
	void RegisterDrone(ADronePawn* Drone);

private:
	UFUNCTION()
	void HandleDroneCrashed(APawn* CrashedPawn);

	void OnActorSpawned(AActor* SpawnedActor);

	// Respawn one drone at its recorded start transform and return it to flight.
	void RespawnDrone(APawn* Drone);

	// Where each registered drone started, so a crash returns it there.
	UPROPERTY()
	TMap<TObjectPtr<APawn>, FTransform> StartTransforms;
};
