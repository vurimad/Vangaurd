/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once
#include "../../../common/redContainers/include/staticArray.h"
#include "serializable.h"

#ifndef NO_EDITOR
namespace job
{
class Builder;
}
#endif

class ISerializable;

namespace serialization
{
	// Resource saving context
	class RED_REFLECTION_API SavingContext
	{
	public:
		typedef red::DynArray< SerializableHandle >	TSerializablesToMap;

		SavingContext( const SerializableHandle& singleObject );
		
		// Roots of the object trees to save
		TSerializablesToMap m_initialExports{ red::PoolBackend() };

		Uint32 m_gpuSize; // What is the video memory size of the resource? If 0 - treat as not a GPU resource at all.
		ECookingPlatform m_cookingPlatform;
		Bool m_isCooker; // Are we saving from a cooker
		Bool m_isCloner; // Are we saving from a cloner
		Bool m_isGenerator; // Are we saving from a generator
		Bool m_debugVerifyBufferCompression; // Should we verify that compressed buffers can be decompressed again. Can be costly, but to be on the safe side for now.
		Bool m_magicFlagForTerrain;
		Bool m_magicFlagForAudioDefaultObject; // Should rtti::ClassType::GetDefaultObject be used for serialization? External audio tools depend on DefaultObject definition format.
	};

	class ISaver;
	typedef red::SharedPtr<ISaver> SaverPtr;

	// Dependency saver interface
	class RED_REFLECTION_API ISaver
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		ISaver();
		virtual ~ISaver();

		// Save objects, returns true on success
		// Content is saved at the current file pointer, 
		// After saving file pointer is scrolled to the END of the saved data
		virtual Bool SaveObjects( IFile& file, const SavingContext& context ) const = 0;

#ifndef NO_EDITOR
		virtual Bool SaveObjects( IFile& file, const SavingContext& context, job::Builder& builder, bool* saveResult = nullptr ) const; // TODO(dg): reconsider pointer to bool as result
#endif

	public:	
		template< typename TSaver >
		static void RegisterCustomFactory( const AnsiChar* constImmutableExt )
		{
			static_assert( std::is_base_of< ISaver, TSaver >::value, "Must be an ISaver" );
			RegisterCustomFactory( constImmutableExt, []() { return RED_NEW( TSaver ); } );
		}

		/// Create best serialization saver for given file extension
		/// If no extension is passed then a default binary saver is created
		static SaverPtr CreateSaver( const AnsiChar* formatExtension = nullptr );

		/// Register factory
		typedef std::function< ISaver*( ) >		TSaverFactory;
		static void RegisterCustomFactory( const AnsiChar* constImmutableExt, const TSaverFactory& factoryFunc );

		static const Uint32 MAX_FORMATS = 17;

		struct FormatPair
		{
			const AnsiChar*		m_constImmutableExt;
			TSaverFactory		m_saverFactory;
		};
		
		static red::StaticArray< FormatPair, MAX_FORMATS >		st_formats;
	};

	/// Helper method to save into memory (binary serialization only)
	/// Done out of symmetry with LoadFromMemory
	extern RED_REFLECTION_API const Bool SaveToMemory( const SavingContext& context, IFile& memoryWriter );

} // serialization