/**
* Copyright © 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class IMaterial;
class CMesh;

namespace mesh
{

	class IMaterialsListInterface
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IMaterialsListInterface() {};

		virtual void SetMesh( const THandle<CMesh>& mesh ) = 0;

		virtual Uint32 GetNumMaterials() const = 0;
		virtual red::String GetMaterialName(Uint32 index) const = 0;
		virtual Bool RenameMaterial(Uint32 index, const red::String& newName) = 0;
		virtual red::String GetMaterialShader(Uint32 index) const = 0;
		virtual Int32 GetMaterialIndex(const red::String& name) const = 0;
		virtual Int32 GetMaterialIndex( const IMaterial* item ) const = 0;

		virtual ISerializable* GetMaterialData( Uint32 index ) const = 0;  // Returns ISerializable not IMaterial so we can cheat a little for multilayer.
		virtual THandle<IMaterial> GetMaterial( Uint32 index ) const = 0;

		virtual Bool IsMaterialMultilayer( Uint32 index, Uint32* outNumLayers = nullptr ) const = 0;
		virtual Bool IsMaterialForwardLit( Uint32 index ) const = 0;
		virtual Bool IsMaterialLocalInstance( Uint32 index ) const = 0;
		virtual Bool IsMaterialTemplate( Uint32 index ) const = 0;
		virtual void GetMaterialInfo( const Uint32 chunkIndex, const Uint32 materialIndex, String& chunkInfo, String& chunkLODInfo ) const = 0;

		virtual Bool CreateMaterialLocalInstance(Uint32 index) = 0;
		virtual Bool RemoveMaterialLocalInstance(Uint32 index) = 0;
		virtual Bool AddMaterial(const red::String& name, const Uint32 baseMaterial, bool copy = false) = 0;
		virtual Bool AddMaterial( const THandle<IMaterial>& material, const String& name, Int32 index = -1 ) = 0;
		virtual Bool RemoveMaterial(Uint32 index) = 0;

		virtual THandle<IMaterial> GetMaterialAppliedOnChunk( const Uint32 index, Uint32& outMaterialIndex, String& outMaterialName, const Uint32 appearanceIndex = 0 ) const = 0;
		virtual Uint32 GetMaterialIndexAppliedOnChunk( const Uint32 index, const Uint32 appearanceIndex = 0 ) const = 0;
		virtual void SetChunkMaterial( const Uint32 index, const CName& materialName, const Uint32 appearanceIndex ) const = 0;

		virtual void NotifyThatAppearancesChanged() = 0;

		virtual void UpdateMaterialRenderResource( Uint32 index ) = 0;

		virtual Bool ReimportLayersFromJSON( Uint32 index, const red::String& jsonFilePath ) = 0;
	};

} // mesh