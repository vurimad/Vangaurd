/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "serializationMapping.h"
#include "serializable.h"
#include "resource.h"

namespace serialization
{
	class SavingContext;
	class PathResolver;

	/// Structure mapper for binary serialization
	class RED_REFLECTION_API StructureMapper : public IMapper
	{
	public:
		struct ImportInfo
		{
			res::ResourcePath		m_path;					//!< Path of the resource
			Bool					m_soft;					//!< Soft import
		};

		struct BufferInfo
		{
			BufferHandleRO m_inMemoryBuffer; //!< Buffer's data
			BufferHandleRO m_precompressedBufferForCooking; //!< Precompressed buffer data; may be empty
			Uint32 m_key = 0;
			Uint32 m_sizeInMemoryDuringStructureMap = 0;
			Uint32 m_sizePrecompressedForCookingDuringStructureMap = 0;
			Bool m_useCompression = false;
			Bool m_allowNonDefaultCompressionType = false;
			Bool m_magicFlagForTerrain = false;
			Bool m_hintGPUMemory = false;
			Bool m_hintAutoLoadPC = false ;
			Bool m_hintAutoLoadXboxOne = false;
			Bool m_hintAutoLoadPS4 = false;
		};

		struct InplaceResourceInfo
		{
			PathIndex importIndex;
			ObjectIndex exportIndex;
		};

		typedef red::DynArray< THandle< ISerializable > >		TExportTable;
		typedef red::DynArray< ImportInfo >						TImportTable;
		typedef red::DynArray< CName >							TNameTable;
		typedef red::DynArray< const rtti::Property* >			TPropertyTable;
		typedef red::DynArray< BufferInfo >						TBufferTable;
		typedef red::DynArray< InplaceResourceInfo >			InplaceResourceTable;

		StructureMapper();

		/// Map objects from saving set
		void MapObjects( const SavingContext& context );

		/// Mapping interface - implemented
		virtual void MapName( const CName& name, NameIndex& outIndex ) override final;
		virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) override final;
		virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) override final;
		virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) override final;
		virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) override final;
		virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) override final;

		Bool FindObjectIndex( const ISerializable* const object, Int32& outIndex ) const;
		Bool FindImportIndex( const res::ResourcePath& path, Int32& outIndex ) const;
		Bool FindBufferIndex( Uint32 crc, Int32& outIndex ) const;

		Bool IsCloner() const;
		Bool IsCooker() const;

		TExportTable	m_exports{ red::PoolEngine() };				// List of contained object
		TImportTable	m_imports{ red::PoolEngine() };				// List of used external resources
		TNameTable		m_names{ red::PoolEngine() };				// List of used names
		TPropertyTable	m_properties{ red::PoolEngine() };			// List of used properties
		TBufferTable	m_buffers{ red::PoolEngine() };				// List of used buffers
		InplaceResourceTable m_inplaceResources{red::PoolEngine()};


#ifndef RED_CONFIGURATION_FINAL
		static PathResolver* SetPathResolver( PathResolver* filter );
		static PathResolver* s_pathResolver;
#endif

	private:
		typedef red::HashMap< void*, Int32 >					TPointerMap;
		typedef red::HashMap< Uint64, Int32 >					TImportMap;
		typedef red::HashMap< CName, Int32 >					TNameMap;
		typedef red::HashMap< Uint32, Int32 >					TBufferMap;
		typedef red::HashMap< const rtti::Property*, Int32 >	TPropertyMap;

		/// Unmapping interface - not used
		virtual void UnmapName( const NameIndex index, CName& outName ) override final {};
		virtual void UnmapType( const TypeIndex index,const rtti::IType*& outTypeRef ) override final {};
		virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) override final {};
		virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle & token ) override final {};
		virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) override final {};
		virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, BufferAsyncPtr& outBufferAccess ) override final {};

		virtual bool IsRelinking() const override final;
		virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const override final;

		// Reset tables
		void Reset();

		// Determine if we should map given object at all
		Bool ShouldMapObject( const THandle< ISerializable >& object ) const;

		// Determine if we should import or export given object
		Bool ShouldExportObject( const THandle< ISerializable >& object ) const;

		// Map final object, creates sorted export table from unsorted one
		Int32 MapFinalObject( const THandle< ISerializable >& object );

		const SavingContext*	m_context;		// Settings

		TPointerMap		m_objectIndices{ red::PoolEngine() };		// Object remapping indices
		TNameMap		m_nameIndices{ red::PoolEngine() };			// Name remapping indices
		TBufferMap		m_bufferIndices{ red::PoolEngine() };		// Buffer remapping indices
		TPropertyMap	m_propertyIndices{ red::PoolEngine() };		// Property remapping indices
		TImportMap		m_importIndices{ red::PoolEngine() };		// Import mapping table
		
		TExportTable	m_tempExports{ red::PoolEngine() };			// Unsorted list of contained object
		TPointerMap		m_tempObjectIndices{ red::PoolEngine() };	// Unsorted remapping indices
	};

} // serialization
