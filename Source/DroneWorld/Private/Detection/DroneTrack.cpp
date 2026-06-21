#include "Detection/DroneTrack.h"

namespace
{
	FORCEINLINE void DetCenter(const FDroneDetection& D, float& Cx, float& Cy, float& W, float& H)
	{
		W = D.X2 - D.X1; H = D.Y2 - D.Y1;
		Cx = D.X1 + W * 0.5f; Cy = D.Y1 + H * 0.5f;
	}

	FORCEINLINE void SyncBox(FDroneTrack& T)
	{
		T.X1 = T.Cx - T.W * 0.5f; T.Y1 = T.Cy - T.H * 0.5f;
		T.X2 = T.Cx + T.W * 0.5f; T.Y2 = T.Cy + T.H * 0.5f;
	}

	// The drawn box: anchored to the latest real observation, then LED forward along the track's (smoothed)
	// velocity by `Lead` detection-frames. The lead cancels capture + inference latency so the box sits ON a
	// moving drone instead of trailing it; re-anchoring to every detection (rather than predicting through gaps)
	// keeps it from running away / overshooting on direction changes. Lead 0 = sit exactly on the last detection.
	FORCEINLINE void UpdateDisp(FDroneTrack& T, float Lead)
	{
		const float Ox = T.VxLead * Lead, Oy = T.VyLead * Lead;
		T.DispX1 = T.X1 + Ox; T.DispY1 = T.Y1 + Oy;
		T.DispX2 = T.X2 + Ox; T.DispY2 = T.Y2 + Oy;
	}

	// IoU between a track's current (predicted) box and a detection.
	float IoU(const FDroneTrack& T, const FDroneDetection& D)
	{
		const float ix1 = FMath::Max(T.X1, D.X1), iy1 = FMath::Max(T.Y1, D.Y1);
		const float ix2 = FMath::Min(T.X2, D.X2), iy2 = FMath::Min(T.Y2, D.Y2);
		const float iw = FMath::Max(0.f, ix2 - ix1), ih = FMath::Max(0.f, iy2 - iy1);
		const float inter = iw * ih;
		const float ua = FMath::Max(0.f, T.X2 - T.X1) * FMath::Max(0.f, T.Y2 - T.Y1)
			+ FMath::Max(0.f, D.X2 - D.X1) * FMath::Max(0.f, D.Y2 - D.Y1) - inter;
		return ua > 0.f ? inter / ua : 0.f;
	}

	// OC-SORT momentum: cosine between track velocity and the observation direction (0..1).
	float DirectionBonus(const FDroneTrack& T, const FDroneDetection& D)
	{
		const float Speed = FMath::Sqrt(T.Vx * T.Vx + T.Vy * T.Vy);
		if (Speed < 1e-5f) { return 0.f; }
		float Dcx, Dcy, Dw, Dh; DetCenter(D, Dcx, Dcy, Dw, Dh);
		const float dx = Dcx - T.LastObsCx, dy = Dcy - T.LastObsCy;
		const float Dlen = FMath::Sqrt(dx * dx + dy * dy);
		if (Dlen < 1e-5f) { return 0.f; }
		return FMath::Clamp((T.Vx * dx + T.Vy * dy) / (Speed * Dlen), 0.f, 1.f);
	}

	struct FPair { int32 T; int32 D; float Score; };
}

void FDroneTracker::Reset()
{
	Tracks.Reset();
	NextId = 1;
}

void FDroneTracker::Update(TConstArrayView<FDroneDetection> Dets)
{
	// 1) Predict every track forward one tick (constant velocity).
	for (FDroneTrack& T : Tracks)
	{
		T.Age++;
		T.TimeSinceUpdate++;
		T.Cx += T.Vx; T.Cy += T.Vy;
		SyncBox(T);
	}

	// Split detections into high / low confidence (ByteTrack).
	TArray<int32> High, Low;
	for (int32 i = 0; i < Dets.Num(); ++i)
	{
		(Dets[i].Score >= HighThr ? High : Low).Add(i);
	}

	TArray<bool> TrackMatched; TrackMatched.Init(false, Tracks.Num());
	TArray<bool> DetMatched;   DetMatched.Init(false, Dets.Num());

	auto UpdateTrack = [&](FDroneTrack& T, const FDroneDetection& D)
	{
		float Dcx, Dcy, Dw, Dh; DetCenter(D, Dcx, Dcy, Dw, Dh);
		// OC-SORT observation-centric re-update: recompute velocity over the real gap.
		const int32 Gap = FMath::Max(1, T.Age - T.LastObsAge);
		T.Vx = (Dcx - T.LastObsCx) / Gap;
		T.Vy = (Dcy - T.LastObsCy) / Gap;
		// Smooth the velocity used to lead the drawn box so the lead doesn't jitter with per-detection noise.
		T.VxLead = 0.5f * T.VxLead + 0.5f * T.Vx;
		T.VyLead = 0.5f * T.VyLead + 0.5f * T.Vy;
		T.Cx = Dcx; T.Cy = Dcy; T.W = Dw; T.H = Dh;
		T.LastObsCx = Dcx; T.LastObsCy = Dcy; T.LastObsAge = T.Age;
		T.TimeSinceUpdate = 0;
		T.Hits++;
		T.Score = 0.5f * T.Score + 0.5f * D.Score;
		if (!T.bConfirmed && T.Hits >= MinHits) { T.bConfirmed = true; }
		T.Trail.Add(FVector2D(Dcx, Dcy));
		if (T.Trail.Num() > TrailMax) { T.Trail.RemoveAt(0); }
		SyncBox(T);
		UpdateDisp(T, MotionLead);
	};

	// Greedy association of all unmatched tracks against a detection subset.
	auto Associate = [&](const TArray<int32>& Subset)
	{
		TArray<FPair> Pairs;
		for (int32 ti = 0; ti < Tracks.Num(); ++ti)
		{
			if (TrackMatched[ti]) { continue; }
			for (int32 di : Subset)
			{
				if (DetMatched[di]) { continue; }
				const float Iou = IoU(Tracks[ti], Dets[di]);
				if (Iou < IoUMatch) { continue; }
				Pairs.Add({ ti, di, Iou + OCMWeight * DirectionBonus(Tracks[ti], Dets[di]) });
			}
		}
		Pairs.Sort([](const FPair& A, const FPair& B) { return A.Score > B.Score; });
		for (const FPair& P : Pairs)
		{
			if (TrackMatched[P.T] || DetMatched[P.D]) { continue; }
			TrackMatched[P.T] = true; DetMatched[P.D] = true;
			UpdateTrack(Tracks[P.T], Dets[P.D]);
		}
	};

	Associate(High);   // first pass: high-confidence detections
	Associate(Low);    // second pass (BYTE): recover via low-confidence detections

	// 2) Spawn new tracks only from unmatched HIGH detections.
	for (int32 di : High)
	{
		if (DetMatched[di]) { continue; }
		FDroneTrack T;
		T.Id = NextId++;
		DetCenter(Dets[di], T.Cx, T.Cy, T.W, T.H);
		T.LastObsCx = T.Cx; T.LastObsCy = T.Cy;
		T.Hits = 1; T.Score = Dets[di].Score;
		T.bConfirmed = (MinHits <= 1);
		T.Trail.Add(FVector2D(T.Cx, T.Cy));
		SyncBox(T);
		UpdateDisp(T, MotionLead);
		Tracks.Add(MoveTemp(T));
	}

	// 3) Drop dead tracks.
	Tracks.RemoveAll([&](const FDroneTrack& T) { return T.TimeSinceUpdate > MaxAge; });
}
