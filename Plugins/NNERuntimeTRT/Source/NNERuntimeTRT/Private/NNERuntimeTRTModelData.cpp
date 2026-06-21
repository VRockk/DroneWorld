#include "NNERuntimeTRTModelData.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace UE::NNERuntimeTRT::Private
{

bool FModelData::Load(TConstArrayView64<uint8> InData)
{
	FMemoryReaderView Reader(InData, /*bIsPersistent*/true);
	Reader << GUID;
	Reader << Version;
	Reader << TensorRTVersion;

	const int64 Offset = Reader.Tell();
	if (Reader.IsError() || Offset > InData.Num())
	{
		return false;
	}

	EngineFileDataView = InData.Slice(Offset, InData.Num() - Offset);
	
	return true;
}

bool FModelData::Store(TArray64<uint8>& OutData)
{
	FMemoryWriter64 Writer(OutData, /*bIsPersistent*/true);
	Writer << GUID;
	Writer << Version;
	Writer << TensorRTVersion;
	Writer.Serialize((void*)EngineFileDataView.GetData(), EngineFileDataView.Num());
	
	return !Writer.IsError();
}

bool FModelData::IsSameGuidAndVersion(TConstArrayView64<uint8> Data, FGuid Guid, int32 Version)
{
	FModelData ModelData{};
	if (!ModelData.Load(Data))
	{
		return false;
	}
	
	return Guid == ModelData.GUID && Version == ModelData.Version;
}

} // namespace UE::NNERuntimeTRT::Private