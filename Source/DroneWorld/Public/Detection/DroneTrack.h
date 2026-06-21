// Multi-object tracker for the drone detector: an OC-SORT-style motion core
// (observation-centric velocity + re-update across gaps + momentum/direction) combined
// with ByteTrack's dual-threshold association (recovers weak/low-confidence detections).
// Appearance-free (single class). Operates per inference tick. Written from scratch.
#pragma once

#include "CoreMinimal.h"
#include "DroneTrack.generated.h"

// One raw detection from the model, normalized [0..1] xyxy.
USTRUCT(BlueprintType)
struct FDroneDetection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Drone") float X1 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float Y1 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float X2 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float Y2 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float Score = 0.f;
};

// A persistent track. X1..Y2 is the current (predicted/observed) box for drawing.
USTRUCT(BlueprintType)
struct FDroneTrack
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Drone") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float X1 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float Y1 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float X2 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float Y2 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") float Score = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") int32 Hits = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") int32 TimeSinceUpdate = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Drone") bool bConfirmed = false;

	// Center-velocity model (per inference tick), observation-centric (OC-SORT).
	float Cx = 0.f, Cy = 0.f, W = 0.f, H = 0.f;
	float Vx = 0.f, Vy = 0.f;
	float VxLead = 0.f, VyLead = 0.f;   // smoothed velocity, used only to lead the drawn box (cancels capture+infer lag)
	float LastObsCx = 0.f, LastObsCy = 0.f;
	int32 Age = 0;
	int32 LastObsAge = 0;

	// Smoothed, observation-only box actually drawn (decoupled from the predicted box used for matching).
	float DispX1 = 0.f, DispY1 = 0.f, DispX2 = 0.f, DispY2 = 0.f;

	TArray<FVector2D> Trail;
};

// The tracker. Plain struct member of the detector actor (no UObject refs -> no GC needed).
struct FDroneTracker
{
	// Tunables (set from the actor).
	float HighThr = 0.25f;     // ByteTrack: high-confidence gate (first association pass)
	float IoUMatch = 0.2f;     // minimum IoU to associate
	float OCMWeight = 0.2f;    // OC-SORT momentum (direction-consistency) bonus
	int32 MinHits = 3;         // hits before a track is "confirmed"
	int32 MaxAge = 30;         // remove a track after this many missed inferences
	float MotionLead = 1.5f;   // draw the box this many detection-frames ahead along its velocity (cancels lag)
	int32 TrailMax = 30;

	TArray<FDroneTrack> Tracks;
	int32 NextId = 1;

	// One step. Dets must already be filtered to >= the low (detect) threshold.
	void Update(TConstArrayView<FDroneDetection> Dets);
	void Reset();
};
