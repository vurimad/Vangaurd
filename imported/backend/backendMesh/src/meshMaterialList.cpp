#include "build.h"
#include "meshMaterialList.h"
#include "meshMultilayerMaterialProxy.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialParameter.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"

using red::DynArray;
using red::String;

namespace tools
{
	static String GetShader( red::SharedPtr<mesh::DataView> view, Uint32 index )
	{
		auto data = view->GetMaterialData( index );

		if (data && data->GetMaterialDefinition())
			return data->GetMaterialDefinition()->GetShaderName();

		return String();
	}

	static Bool IsMultilayer( red::SharedPtr<mesh::DataView> view, Uint32 index, Uint32* outNumLayers = nullptr )
	{
		const String shaderName = GetShader( view, index );
		if (!shaderName.Empty() && shaderName.Contains( "multilayered" ))
		{
			return true;
		}
		return false;
	};

	static Bool IsForwardLit( red::SharedPtr<mesh::DataView> view, Uint32 index )
	{
		const THandle<IMaterial>& material = view->GetMaterialData( index );
		if ( material == nullptr )
			return false;
		if ( const IMaterialDefinition* materialDefinition = material->GetMaterialDefinition() )
		{
			for ( Uint32 i = 0; i < materialDefinition->GetNumTechniques(); ++i )
			{
				EngineStagePassDataCollection passData;
				materialDefinition->GetPasses( i, passData );
				for ( const auto& passDataEntry : passData )
				{
					const red::StringView stageName = passDataEntry.m_stageNameRegular.AsStringView();
					const Bool isTransparent = stageName.Find( "transparent" ) != red::StringView::npos;
					if ( isTransparent )
					{
						return true;
					}
					const Bool isForward = stageName.Find( "forward" ) != red::StringView::npos;
					if ( isForward )
					{
						return true;
					}
				}
			}
		}
		return false;
	};

	static void GetChunkInfo( red::SharedPtr<mesh::DataView> view, Uint32 chunkIndex, Uint32 materialIndex, String& chunkInfo, String& chunkLODInfo )
	{
		for( Uint32 i=0; i<view->GetNumLODs(); ++i )
		{
			red::DynArray< Uint16 > lodIndices = view->GetLODChunkIndices( i );
			
			if( lodIndices.Exist( chunkIndex ) )
			{
				chunkInfo = String::Printf( "%u: %hs", chunkIndex, GetShader( view, materialIndex ).AsChar() );
				chunkLODInfo = String::Printf( "%u", i );
				return;
			}
		}

		chunkInfo = String::Printf( "<chunk error> %hs", GetShader( view, materialIndex ).AsChar() );
		chunkLODInfo = "<LOD error>";
	}

	static void GetInfo( red::SharedPtr<mesh::DataView> view, const Uint32 chunkIndex, const Uint32 materialIndex, String& chunkInfo, String& chunkLODInfo )
	{
		GetChunkInfo( view, chunkIndex, materialIndex, chunkInfo, chunkLODInfo );
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////
	void MeshMaterialsList::SetMesh(const THandle<CMesh>& mesh)
	{
		m_mesh = mesh;
		m_view.Reset(RED_NEW( mesh::DataView )(*mesh));
	}

	Uint32 MeshMaterialsList::GetNumMaterials() const
	{
		return m_view->GetNumMaterials();
	}

	String MeshMaterialsList::GetMaterialName(Uint32 index) const
	{
		return m_view->GetMaterialName(index).AsChar();
	}

	Bool MeshMaterialsList::RenameMaterial(Uint32 index, const String& newName)
	{
		m_view->SetMaterialName(index, RED_NAME(newName));
		m_view->Apply();
		return true;
	}

	String MeshMaterialsList::GetMaterialShader(Uint32 index) const
	{
		return GetShader( m_view, index );
	}

	Int32 MeshMaterialsList::GetMaterialIndex(const String& name) const
	{
		const auto materialName = RED_NAME_NOREG( name );
		const auto numMaterials = m_view->GetNumMaterials();
		for (Uint32 i = 0; i < numMaterials; ++i)
			if (m_view->GetMaterialName(i) == materialName)
				return i;

		return -1;
	}

	Int32 MeshMaterialsList::GetMaterialIndex( const IMaterial* material ) const
	{
		return m_view->GetMaterialIndex( material );
	}

	ISerializable* MeshMaterialsList::GetMaterialData( Uint32 index ) const
	{
		return m_view->GetMaterialData( index ).Get();
	}

	THandle<IMaterial> MeshMaterialsList::GetMaterial( Uint32 index ) const
	{
		return m_view->GetMaterialData( index );
	}

	THandle<IMaterial> MeshMaterialsList::GetMaterialAppliedOnChunk( const Uint32 index, Uint32& outMaterialIndex, String& outMaterialName, const Uint32 appearanceIndex ) const
	{
		outMaterialIndex = GetMaterialIndexAppliedOnChunk( index, appearanceIndex );
		outMaterialName = m_view->GetMaterialName( outMaterialIndex ).AsChar();

		return m_view->GetMaterialData( outMaterialIndex );
	}

	Uint32 MeshMaterialsList::GetMaterialIndexAppliedOnChunk( const Uint32 index, const Uint32 appearanceIndex ) const
	{	
		if( appearanceIndex >= m_view->GetNumAppearances() )
			return 0;

		const CName chunkMaterialName = m_view->GetAppearanceData( appearanceIndex )->GetChunkMaterialName( index );
		return m_view->GetMaterialIndex( chunkMaterialName );
	}

	void MeshMaterialsList::SetChunkMaterial( const Uint32 index, const CName& materialName, const Uint32 appearanceIndex ) const
	{
		m_view->SetChunkMaterial( materialName, index, appearanceIndex );
	}

	Bool MeshMaterialsList::IsMaterialLocalInstance(Uint32 index) const
	{
		return m_view->IsMaterialLocalInstance( index );
	}

	Bool MeshMaterialsList::IsMaterialTemplate( Uint32 index ) const
	{
		return m_view->IsMaterialTemplate( index );
	}

	Bool MeshMaterialsList::IsMaterialMultilayer( Uint32 index, Uint32* outNumLayers ) const
	{
		return IsMultilayer( m_view, index, outNumLayers );
	}

	Bool MeshMaterialsList::IsMaterialForwardLit( Uint32 index ) const
	{
		return IsForwardLit( m_view, index );
	}

	void MeshMaterialsList::GetMaterialInfo( const Uint32 chunkIndex, const Uint32 materialIndex, String& chunkInfo, String& chunkLODInfo ) const
	{
		GetInfo( m_view, chunkIndex, materialIndex, chunkInfo, chunkLODInfo );
	}

	void MeshMaterialsList::NotifyThatAppearancesChanged()
	{
	}

	void MeshMaterialsList::UpdateMaterialRenderResource( Uint32 index )
	{
		const THandle<IMaterial> material = m_view->GetMaterialData( index );
		if ( material != nullptr )
			material->UpdateRenderResource();
	}

	Bool MeshMaterialsList::ReimportLayersFromJSON( Uint32 index, const red::String& jsonFilePath )
	{
		return false;
	}

	Bool MeshMaterialsList::CreateMaterialLocalInstance(Uint32 index)
	{
		auto currentMaterial = m_view->GetMaterialData(index);
		if ( currentMaterial && !IsMaterialLocalInstance(index) )
		{
			THandle< CMaterialInstance > instance = CreateHandle< CMaterialInstance >( m_mesh.Get(), currentMaterial );
			m_view->SetMaterialData(index, instance);
			instance->AutoAssignTextures();
			m_view->Apply();
			return true;
		}
		return false;
	}

	Bool MeshMaterialsList::RemoveMaterialLocalInstance(Uint32 index)
	{
		auto instance = Cast< CMaterialInstance >(m_view->GetMaterialData(index));
		if ( instance && IsMaterialLocalInstance( index ) )
		{
			auto base = instance->GetBaseMaterial();
			if (base)
			{
				m_view->SetMaterialData( index, HandleFromPtr( base ) );
				m_view->Apply();
				return true;
			}
		}
		return false;
	}

	Bool MeshMaterialsList::AddMaterial(const String& name, const Uint32 baseMaterialInd, bool copy /* = false */)
	{
		THandle< IMaterial > matExample = m_view->GetMaterialData(baseMaterialInd);
		if (matExample == nullptr || matExample->GetMaterialDefinition() == nullptr)
			return false;

		THandle < IMaterial > baseMaterial = HandleFromPtr( matExample->GetBaseMaterial() );
		if( baseMaterial == nullptr )
		{
			baseMaterial = matExample;
		}
		if (THandle< CMaterialInstance > newMaterial = CreateHandle< CMaterialInstance >(m_mesh.Get(), baseMaterial))
		{
			CMaterialInstance* matExampleInstance = Cast< CMaterialInstance >(matExample.Get());
			if (copy && matExampleInstance != nullptr)
			{
				for (const MaterialParameterInstance& param : matExampleInstance->GetParameters())
				{
					newMaterial->WriteParameterRaw(param.GetName(), param.GetType(), param.GetData());
				}
			}

			return m_view->AddMaterial(newMaterial, RED_NAME(name));
		}

		return false;
	}

	Bool MeshMaterialsList::AddMaterial( const THandle<IMaterial>& material, const String& name, Int32 index /*=-1*/ )
	{
		if ( material == nullptr || GetMaterialIndex( name ) != -1 )
			return false;

		return m_view->AddMaterial( material, RED_NAME( name ), index );
	}

	Bool MeshMaterialsList::RemoveMaterial(Uint32 index)
	{
		return m_view->RemoveMaterial(index);
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////
	void MeshAppearanceMaterialsList::SetMesh( const THandle<CMesh>& mesh )
	{
		m_mesh = mesh;
		m_view.Reset( RED_NEW( mesh::DataView )(*mesh) );
	}

	Uint32 MeshAppearanceMaterialsList::GetNumMaterials() const
	{
		return m_appearance->GetNumChunks();
	}

	String MeshAppearanceMaterialsList::GetMaterialName( Uint32 index ) const
	{
		return m_appearance->GetChunkMaterialName( index ).AsChar();
	}

	Bool MeshAppearanceMaterialsList::RenameMaterial( Uint32 index, const String& newName )
	{
		return false;        // We can't rename materials in appearance materials mode.
	}

	String MeshAppearanceMaterialsList::GetMaterialShader( Uint32 index ) const
	{
		return GetShader( m_view, index );
	}

	Int32 MeshAppearanceMaterialsList::GetMaterialIndex( const String& name ) const
	{
		return -1;        // We can't get index by name in appearance materials mode.
	}

	Int32 MeshAppearanceMaterialsList::GetMaterialIndex( const IMaterial* material ) const
	{
		return -1;						/// @todo DP: correct this? maybe don't bother, this method will be removed by Jowita's CL anyway
	}

	ISerializable* MeshAppearanceMaterialsList::GetMaterialData( Uint32 index ) const
	{
		return m_view->GetMaterialDataByName( m_appearance->GetChunkMaterialName( index ) ).Get();
	}

	THandle<IMaterial> MeshAppearanceMaterialsList::GetMaterial( Uint32 index ) const
	{
		return m_view->GetMaterialDataByName( m_appearance->GetChunkMaterialName( index ) );
	}

	THandle<IMaterial> MeshAppearanceMaterialsList::GetMaterialAppliedOnChunk( const Uint32 index, Uint32& outMaterialIndex, String& outMaterialName, const Uint32 appearanceIndex ) const
	{
		const CName chunkMaterialName = m_view->GetChunkMaterialName( index, appearanceIndex );
		outMaterialIndex = m_view->GetMaterialIndex( chunkMaterialName );
		outMaterialName = m_view->GetMaterialName( outMaterialIndex ).AsChar();

		return m_view->GetMaterialDataByName( chunkMaterialName );
	}

	Uint32 MeshAppearanceMaterialsList::GetMaterialIndexAppliedOnChunk( const Uint32 index, const Uint32 appearanceIndex ) const
	{
		const CName chunkMaterialName = m_view->GetAppearanceData( appearanceIndex )->GetChunkMaterialName( index );
		return m_view->GetMaterialIndex( chunkMaterialName );
	}

	void MeshAppearanceMaterialsList::SetChunkMaterial( const Uint32 index, const CName& materialName, const Uint32 appearanceIndex ) const
	{
		m_view->SetChunkMaterial( materialName, index, appearanceIndex );
	}

	Bool MeshAppearanceMaterialsList::IsMaterialMultilayer( Uint32 index, Uint32* outNumLayers ) const
	{
		return IsMultilayer( m_view, index, outNumLayers );
	}

	Bool MeshAppearanceMaterialsList::IsMaterialForwardLit( Uint32 index ) const
	{
		return IsForwardLit( m_view, index );
	}

	Bool MeshAppearanceMaterialsList::IsMaterialLocalInstance( Uint32 index ) const
	{
		const THandle<IMaterial>& material = m_view->GetMaterialDataByName( m_appearance->GetChunkMaterialName( index ) );
		return (material != nullptr) && m_view->IsMaterialLocalInstance( material.Get() );
	}

	Bool MeshAppearanceMaterialsList::IsMaterialTemplate( Uint32 index ) const
	{
		Int32 materialIndex = m_view->GetMaterialIndex( m_appearance->GetChunkMaterialName( index ) );
		return (materialIndex >= 0) ? m_view->IsMaterialTemplate( materialIndex ) : false;
	}

	void MeshAppearanceMaterialsList::GetMaterialInfo( const Uint32 chunkIndex, const Uint32 materialIndex, String& chunkInfo, String& chunkLODInfo ) const
	{
		GetInfo( m_view, chunkIndex, materialIndex, chunkInfo, chunkLODInfo );
	}

	void MeshAppearanceMaterialsList::NotifyThatAppearancesChanged()
	{
		ClearCachedMultilayerMaterialProxies();            // Delete temporary multilayer materials, they may be outdated.
	}

	void MeshAppearanceMaterialsList::UpdateMaterialRenderResource( Uint32 index )
	{
		if ( THandle< IMaterial > material = m_view->GetMaterialDataByName( m_appearance->GetChunkMaterialName( index ) ) )
		{
			material->UpdateRenderResource();
		}
	}

	Bool MeshAppearanceMaterialsList::ReimportLayersFromJSON( Uint32 index, const red::String& jsonFilePath )
	{
		if ( !IsMaterialMultilayer( index ) )
			return false;
		Bool result = m_view->ReimportLayersFromJSON( m_appearance->GetName(), index, jsonFilePath );
		ClearCachedMultilayerMaterialProxies();
		if ( m_layerSelectionChangedCallback )
			m_layerSelectionChangedCallback();
		return result;
	}

	Bool MeshAppearanceMaterialsList::CreateMaterialLocalInstance( Uint32 index )
	{
		return false;        // We can't create/remove instances in appearance materials mode.
	}

	Bool MeshAppearanceMaterialsList::RemoveMaterialLocalInstance( Uint32 index )
	{
		return false;        // We can't create/remove instances in appearance materials mode.
	}

	Bool MeshAppearanceMaterialsList::AddMaterial( const String& name, const Uint32 baseMaterialInd, bool copy )
	{
		return false;        // We can't add new materials in appearance materials mode.
	}

	Bool MeshAppearanceMaterialsList::AddMaterial( const THandle<IMaterial>& material, const String& name, Int32 index /*=-1*/ )
	{
		return false;        // We can't add new materials in appearance materials mode.
	}

	Bool MeshAppearanceMaterialsList::RemoveMaterial( Uint32 index )
	{
		return false;        // We can't remove materials in appearance materials mode.
	}

	void MeshAppearanceMaterialsList::ClearCachedMultilayerMaterialProxies()
	{
		m_multilayerMaterialProxies.Clear();            // Delete temporary multilayer materials, they may be outdated.
	}

} //tools
