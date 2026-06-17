#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Flight/DroneImperfection.h"
#include "DroneWindSubsystem.generated.h"

// The level's single source of Wind. Wind is a global property of the world, not of any one drone, so
// it lives here once and every drone's Imperfection Layer reads the same value and scales it by its own
// susceptibility. A level or GameMode sets the wind on BeginPlay; gusts then ebb and flow on their own.
UCLASS()
class DRONEWORLD_API UDroneWindSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Set the world's wind. Called by the level/GameMode; safe to call at runtime to change conditions.
	UFUNCTION(BlueprintCallable, Category = "Drone|Wind")
	void SetWind(const FWorldWindParams& InWind) { Wind = InWind; }

	// The configured wind, before gusts and before per-drone susceptibility.
	UFUNCTION(BlueprintPure, Category = "Drone|Wind")
	FWorldWindParams GetWindParams() const { return Wind; }

	// The instantaneous wind force right now, steady breeze plus gust, sampled at the current world time.
	// This is what each drone's Imperfection Layer scales by its susceptibility.
	UFUNCTION(BlueprintPure, Category = "Drone|Wind")
	FVector GetCurrentWind() const;

private:
	// The level's wind. Defaults to dead calm so a drone in a level that never sets wind simply feels none.
	UPROPERTY()
	FWorldWindParams Wind;
};
