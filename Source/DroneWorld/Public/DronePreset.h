#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Flight/DroneFlightTypes.h"
#include "Flight/DroneImperfection.h"
#include "DronePreset.generated.h"

class UDroneFlightModel;

// A drone definition a designer picks and tunes in the editor. It carries an instanced, inline-
// editable Flight Model subobject that holds the drone's type-specific parameters; the movement
// component installs it via ApplyPreset. Choosing a different Flight Model class surfaces only that
// model's fields, and adding a new model subclass needs no change here.
UCLASS(BlueprintType)
class DRONEWORLD_API UDronePreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UDroneFlightModel> FlightModel;

	// The assist level this drone flies in until the pilot toggles hover. Angle (self-leveling) is the
	// approachable default; choose Acro for a raw, hands-on feel.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone")
	EDroneAssistMode BaseAssistMode = EDroneAssistMode::Angle;

	// Self-leveling stiffness used by Angle mode, 1/s^2. Higher values snap back to level harder when
	// the sticks are released.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ClampMin = "0.0"))
	float LevelingStrength = 6.f;

	// How this drone bobs, drifts, feels the wind, and spools its motors. Shared across all flight
	// models and applied uniformly, so a light drone can be tuned to be shoved harder and to wander more
	// than a heavy one. Susceptibility lives here so drone size genuinely matters in the wind.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ShowOnlyInnerProperties))
	FImperfectionParams Imperfection;

	// The closing speed (cm/s) at which a contact crashes this drone rather than bumping off it. A
	// flimsy small quad sets this low so it breaks on a firm knock; a rugged large one sets it high so
	// it shrugs the same knock off, so drone toughness is a per-preset trait.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ClampMin = "0.0"))
	float CrashThreshold = 800.f;

	// The fraction of its impact velocity a crashing drone keeps, 0 (dead stop) .. 1 (no loss). The
	// wreck carries this much momentum off the impact so it tumbles away rather than stopping dead, while
	// gravity and drag bleed the rest as it falls.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrashMomentumRetention = 0.4f;
};
