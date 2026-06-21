#pragma once

#include "Containers/ArrayView.h"
#include "Misc/Guid.h"

// TensorRT RTX engine file model data

namespace UE::NNERuntimeTRT::Private
{
class FModelData
{
public:
	bool Load(TConstArrayView64<uint8> Data);
	bool Store(TArray64<uint8>& Data);

	static bool IsSameGuidAndVersion(TConstArrayView64<uint8> Data, FGuid Guid, int32 Version);

	FGuid GUID;
	int32 Version;
	int32 TensorRTVersion;

	// Non-owning view into Engine file data. On saving, copy of file data is written. When loading, points to source passed to Load().
	TConstArrayView64<uint8> EngineFileDataView;
};

} // namespace UE::NNERuntimeTRT::Private