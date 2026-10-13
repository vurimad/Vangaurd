/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class IMaterial;
class CMesh;

namespace mesh
{
	class DataView;
	struct ChunkMaterials;
}

namespace tools
{
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/// A proxy class representing view on one multilayer material used by mesh.
	/// We don't have actual multilayer material class, we represent multilayer as an array of IMaterial objects.
	/// This class helps to group those individual materials into one view, with global (common) parameters separated etc.
	///
	/// Objects of this class are temporary, created locally in Mesh Editor during material list edition.
	/// They do not hold any data persistently, then just cache some properties and propagate them to the
	/// underlying list of IMaterial's.
	/// They can be destroyed and recreated in mesh editor multiple times (when appearance was changed etc).
	class BACKEND_MESH_API MeshMultilayerMaterialProxy : public ISerializable
	{
		RTTI_DECLARE_TYPE( MeshMultilayerMaterialProxy );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MeshMultilayerMaterialProxy();
		MeshMultilayerMaterialProxy( const THandle< CMesh >& mesh, const mesh::ChunkMaterials& chunkMaterials,
		                             const std::function< void() >& layerSelectionChangedCallback = std::function< void() >() );
		virtual ~MeshMultilayerMaterialProxy();

		// Get currently selected layer.
		RED_INLINE const THandle< IMaterial >& GetCurrentMaterial() const { return m_currentMaterial; }

		// Dynamic editable properties - for materials, blocks with dynamic content, etc.
		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;

		// Read dynamic property
		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const override;

		// Write dynamic property
		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue ) override;

	protected:
		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		/// Test if given material is a local instance in this mesh.
		Bool IsMaterialLocalInstance( const THandle<IMaterial>& material ) const;

		/// Test if the property with given name is a global material property.
		static Bool IsGlobalMaterialProperty( CName propertyName );
		/// Test if the property with given name is a layer material property.
		static Bool IsLayerMaterialProperty( CName propertyName );
		/// Test if the property with given name is editable.
		static Bool IsMaterialPropertyEditable( CName propertyName );

	public:
		THandle< CMesh > m_mesh;
		red::UniquePtr< mesh::DataView > m_view;

		red::DynArray< THandle<IMaterial> > m_layers;
		red::DynArray< String > m_layerNames;
		CName m_currentLayer;
		THandle< IMaterial > m_currentMaterial;

		std::function< void() > m_layerSelectionChangedCallback;
	};

} // tools
