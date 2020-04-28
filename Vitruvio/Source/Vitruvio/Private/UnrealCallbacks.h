#pragma once

#include "Core.h"
#include "Engine/StaticMesh.h"
#include "Modules/ModuleManager.h"
#include "PRTTypes.h"
#include "Codec/Encoder/IUnrealCallbacks.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealCallbacks, Log, All);

class UnrealCallbacks : public IUnrealCallbacks
{
	AttributeMapBuilderUPtr& AttributeMapBuilder;
	
	TMap<UStaticMesh*, TArray<FTransform>> Instances;
	TMap<int32, UStaticMesh*> Meshes;
	
	UMaterial* OpaqueParent;
	UMaterial* MaskedParent;
	UMaterial* TranslucentParent;

	static const int32 NO_PROTOTYPE_INDEX = -1;

public:

	~UnrealCallbacks() override = default;
	UnrealCallbacks(AttributeMapBuilderUPtr& AttributeMapBuilder, UMaterial* OpaqueParent, UMaterial* MaskedParent, UMaterial* TranslucentParent)
		: AttributeMapBuilder(AttributeMapBuilder), OpaqueParent(OpaqueParent), MaskedParent(MaskedParent), TranslucentParent(TranslucentParent)
	{
	}

	const TMap<UStaticMesh*, TArray<FTransform>>& GetInstances() const
	{
		return Instances;
	}

	UStaticMesh* GetModel() const
	{
		return Meshes.Contains(NO_PROTOTYPE_INDEX) ? Meshes[NO_PROTOTYPE_INDEX] : nullptr;
	}
	
	/**
	 * @param name initial shape name, optionally used to create primitive groups on output
	 * @param prototypeId the id of the prototype or -1 of not cached
	 * @param vtx vertex coordinate array
	 * @param vtxSize of vertex coordinate array
	 * @param nrm vertex normal array
	 * @param nrmSize length of vertex normal array
	 * @param faceVertexCounts vertex counts per face
	 * @param faceVertexCountsSize number of faces (= size of faceCounts)
	 * @param vertexIndices vertex attribute index array (grouped by counts)
	 * @param vertexIndicesSize vertex attribute index array
	 * @param uvs array of texture coordinate arrays (same indexing as vertices per uv set)
	 * @param uvsSizes lengths of uv arrays per uv set
	 * @param faceRanges ranges for materials and reports
	 * @param materials contains faceRangesSize-1 attribute maps (all materials must have an identical set of keys and
	 * types)
	 */
	// clang-format off
	void addMesh(const wchar_t* name,
		int32_t prototypeId,
		const double* vtx, size_t vtxSize,
		const double* nrm, size_t nrmSize,
		const uint32_t* faceVertexCounts, size_t faceVertexCountsSize,
		const uint32_t* vertexIndices, size_t vertexIndicesSize,
		const uint32_t* normalIndices, size_t normalIndicesSize,

		double const* const* uvs, size_t const* uvsSizes,
		uint32_t const* const* uvCounts, size_t const* uvCountsSizes,
		uint32_t const* const* uvIndices, size_t const* uvIndicesSizes,
		size_t uvSets,

		const uint32_t* faceRanges, size_t faceRangesSize,
		const prt::AttributeMap** materials
	) override;

	/**
	 * @param prototypeId the prototype id of the instance, must be >= 0
	 * @param transform the transformation matrix of the instance
	 */
	void addInstance(int32_t prototypeId, const double* transform) override;
	// clang-format on

	prt::Status generateError(size_t /*isIndex*/, prt::Status /*status*/, const wchar_t* message) override
	{
		UE_LOG(LogUnrealCallbacks, Error, TEXT("GENERATE ERROR: %s"), message)
		return prt::STATUS_OK;
	}
	prt::Status assetError(size_t /*isIndex*/, prt::CGAErrorLevel /*level*/, const wchar_t* /*key*/, const wchar_t* /*uri*/, const wchar_t* message) override
	{
		UE_LOG(LogUnrealCallbacks, Error, TEXT("ASSET ERROR: %s"), message)
		return prt::STATUS_OK;
	}
	prt::Status cgaError(size_t /*isIndex*/, int32_t /*shapeID*/, prt::CGAErrorLevel /*level*/, int32_t /*methodId*/, int32_t /*pc*/, const wchar_t* message) override
	{
		UE_LOG(LogUnrealCallbacks, Error, TEXT("CGA ERROR: %s"), message)
		return prt::STATUS_OK;
	}
	prt::Status cgaPrint(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* txt) override
	{
		UE_LOG(LogUnrealCallbacks, Display, TEXT("CGA Print: %s"), txt)
		return prt::STATUS_OK;
	}

	prt::Status cgaReportBool(size_t isIndex, int32_t shapeID, const wchar_t* key, bool value) override
	{
		return prt::STATUS_OK;
	}
	prt::Status cgaReportFloat(size_t isIndex, int32_t shapeID, const wchar_t* key, double value) override
	{
		return prt::STATUS_OK;
	}
	prt::Status cgaReportString(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* value) override
	{
		return prt::STATUS_OK;
	}

	prt::Status attrBool(size_t isIndex, int32_t shapeID, const wchar_t* key, bool value) override;
	prt::Status attrFloat(size_t isIndex, int32_t shapeID, const wchar_t* key, double value) override;
	prt::Status attrString(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* value) override;
	prt::Status attrBoolArray(size_t isIndex, int32_t shapeID, const wchar_t* key, const bool* ptr, size_t size) override;
	prt::Status attrFloatArray(size_t isIndex, int32_t shapeID, const wchar_t* key, const double* ptr, size_t size) override;
	prt::Status attrStringArray(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* const* ptr, size_t size) override;
};