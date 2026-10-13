#pragma once

#include "meshMaterialListInterface.h"
#include "meshDataView.h"

namespace tools
{
	class MeshMultilayerMaterialProxy;

	//////////////////////////////////////////////////////////////////////////////////////////////
	/// List of all materials in the mesh, do not take appearances into account.
	class MeshMaterialsList : public mesh::IMaterialsListInterface, public red::NonCopyable
	{
	public:
		MeshMaterialsList(const THandle< CMesh >& mesh)
		{
			SetMesh(mesh);
		}

		virtual void SetMesh(const THandle<CMesh>& mesh) override;

		virtual ~MeshMaterialsList() override {};

		virtual Uint32 GetNumMaterials() const override;
		virtual red::String GetMaterialName(Uint32 index) const override;
		virtual Bool RenameMaterial(Uint32 index, const red::String& newName) override;
		virtual red::String GetMaterialShader(Uint32 index) const override;
		virtual Int32 GetMaterialIndex(const red::String& name) const override;
		virtual Int32 GetMaterialIndex( const IMaterial* material ) const override;

		virtual ISerializable* GetMaterialData( Uint32 index ) const override;
		virtual THandle<IMaterial> GetMaterial( Uint32 index ) const override;

		virtual Bool IsMaterialMultilayer( Uint32 index, Uint32* outNumLayers = nullptr ) const override;
		virtual Bool IsMaterialForwardLit( Uint32 index ) const override;
		virtual Bool IsMaterialLocalInstance(Uint32 index) const override;
		virtual Bool IsMaterialTemplate( Uint32 index ) const override;
		virtual void GetMaterialInfo( const Uint32 chunkIndex, const Uint32 materialIndex, String& chunkInfo, String& chunkLODInfo ) const override;

		virtual Bool CreateMaterialLocalInstance(Uint32 index) override;
		virtual Bool RemoveMaterialLocalInstance(Uint32 index) override;
		virtual Bool AddMaterial(const red::String& name, const Uint32 baseMaterial, bool copy = false) override;
		virtual Bool AddMaterial( const THandle<IMaterial>& material, const String& name, Int32 index = -1 ) override;
		virtual Bool RemoveMaterial(Uint32 index) override;

		virtual THandle<IMaterial> GetMaterialAppliedOnChunk( const Uint32 index, Uint32& outMaterialIndex, String& outMaterialName, const Uint32 appearanceIndex = 0 ) const override;
		virtual Uint32 GetMaterialIndexAppliedOnChunk( const Uint32 index, const Uint32 appearanceIndex = 0 ) const override;
		virtual void SetChunkMaterial( const Uint32 index, const CName& materialName, const Uint32 appearanceIndex ) const override;

		virtual void NotifyThatAppearancesChanged() override;

		virtual void UpdateMaterialRenderResource( Uint32 index ) override;

		virtual Bool ReimportLayersFromJSON( Uint32 index, const red::String& jsonFilePath ) override;

	private:
		THandle< CMesh > m_mesh;
		red::SharedPtr<mesh::DataView> m_view;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	/// List of materials for a single appearance, allows for multilayer material view.
	class MeshAppearanceMaterialsList : public mesh::IMaterialsListInterface, public red::NonCopyable
	{
	public:
		MeshAppearanceMaterialsList( const THandle< CMesh >& mesh, const THandle<mesh::MeshAppearance>& appearance, const std::function< void() >& layerSelectionChangedCallback )
			: m_layerSelectionChangedCallback( layerSelectionChangedCallback )
			, m_multilayerMaterialProxies( red::PoolBackend() )
		{
			SetMesh( mesh );
			m_appearance = appearance;
		}

		virtual void SetMesh( const THandle<CMesh>& mesh ) override;

		virtual ~MeshAppearanceMaterialsList() override {};

		virtual Uint32 GetNumMaterials() const override;
		virtual red::String GetMaterialName( Uint32 index ) const override;
		virtual Bool RenameMaterial( Uint32 index, const red::String& newName ) override;
		virtual red::String GetMaterialShader( Uint32 index ) const override;
		virtual Int32 GetMaterialIndex( const red::String& name ) const override;
		virtual Int32 GetMaterialIndex( const IMaterial* material ) const override;

		virtual ISerializable* GetMaterialData( Uint32 index ) const override;
		virtual THandle<IMaterial> GetMaterial( Uint32 index ) const override;

		virtual Bool IsMaterialMultilayer( Uint32 index, Uint32* outNumLayers = nullptr ) const override;
		virtual Bool IsMaterialForwardLit( Uint32 index ) const override;
		virtual Bool IsMaterialLocalInstance( Uint32 index ) const override;
		virtual Bool IsMaterialTemplate( Uint32 index ) const override;
		virtual void GetMaterialInfo( const Uint32 chunkIndex, const Uint32 materialIndex, String& chunkInfo, String& chunkLODInfo ) const override;

		virtual Bool CreateMaterialLocalInstance( Uint32 index ) override;
		virtual Bool RemoveMaterialLocalInstance( Uint32 index ) override;
		virtual Bool AddMaterial( const red::String& name, const Uint32 baseMaterial, bool copy = false ) override;
		virtual Bool AddMaterial( const THandle<IMaterial>& material, const String& name, Int32 index = -1 ) override;
		virtual Bool RemoveMaterial( Uint32 index ) override;

		virtual THandle<IMaterial> GetMaterialAppliedOnChunk( const Uint32 index, Uint32& outMaterialIndex, String& outMaterialName, const Uint32 appearanceIndex = 0 ) const override;
		virtual Uint32 GetMaterialIndexAppliedOnChunk( const Uint32 index, const Uint32 appearanceIndex = 0 ) const override;
		virtual void SetChunkMaterial( const Uint32 index, const CName& materialName, const Uint32 appearanceIndex ) const override;

		virtual void NotifyThatAppearancesChanged() override;

		virtual void UpdateMaterialRenderResource( Uint32 index ) override;

		virtual Bool ReimportLayersFromJSON( Uint32 index, const red::String& jsonFilePath ) override;

	private:
		void ClearCachedMultilayerMaterialProxies();

		THandle< CMesh > m_mesh;
		red::SharedPtr<mesh::DataView> m_view;
		THandle<mesh::MeshAppearance> m_appearance;
		mutable red::DynArray< THandle<MeshMultilayerMaterialProxy> > m_multilayerMaterialProxies;   // Temporary proxies for multilayer materials are kept here.
		std::function< void() > m_layerSelectionChangedCallback;
	};

} //tools
