/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../include/bufferAsyncProxy.h"

#include "../include/serializationMapping.h"
#include "../include/serializationFileTables.h"

#include "../include/serializationAsyncSource.h"

namespace job { class CounterChain; }
namespace res { class Throttler;  }

namespace serialization
{
	class FileTables;
	class LoadingResult;

	/// runtime data mapper, file indices -> actual stuff
	class RED_REFLECTION_API RuntimeTables : public IMapper, red::NonCopyable
	{
	public:
		struct ResolvedImport
		{
			res::ResourcePath	m_path;
			res::ResourceTokenHandle m_loadingToken;
			Bool m_inplaceForDebug{ false };
			bool waitOnImport = false;
		};

		struct ResolvedExport
		{
			const rtti::ClassType* m_class;
			THandle< ISerializable > m_object; // until created
			Bool m_skip;
			Int32 m_inplaceResourceIndex = - 1;
			Int32 m_exportParent = -1;
		};

		struct ResolvedBuffers
		{
			static_assert( sizeof( red::memory::PoolHandle ) == sizeof(Uint32), "PoolHandle size changed, this will affect deserialization" );
			Uint32						m_dataOffset;		// Offset to the buffer data (if resident)
			Uint32						m_dataSizeOnDisk;	// Size of the buffer data on disk (if we ever add native compression)
			Uint32						m_dataSizeInMemory;	// Size of the buffer data in memory
		};

		struct ResolvedInplaceResource 
		{
			res::ResourceTokenHandle m_token;
			job::CompletionDeferral m_deferral;
			Uint32 m_exportIndex;
			bool assignResourceOnPostLoad;
			bool tokenRegistered;
		};

		red::DynArray< CName >						m_mappedNames{ red::PoolEngine() };			// Mapped names (runtime data only)
		red::DynArray< const rtti::IType* >			m_mappedTypes{ red::PoolEngine() };			// Mapped RTTI types (runtime data only)
		red::DynArray< ResolvedImport >				m_mappedImports{ red::PoolEngine() };		// Mapped file imports (runtime data only)
		red::DynArray< ResolvedExport >				m_mappedExports{ red::PoolEngine() };		// Mapped file exports (runtime data only)
		red::DynArray< ResolvedBuffers >			m_mappedBuffers{ red::PoolEngine() };		// Mapped and resolved buffers
		red::DynArray< ResolvedInplaceResource >	m_mappedInplaceResources{ red::PoolEngine() };

		enum NoLoading
		{
			eNoLoading,
		};

		enum LoadExportsFromFile
		{
			eLoadExportsFromFile,
		};

		/// map stuff that is in the tables into actual objects
		explicit RuntimeTables( NoLoading );
		explicit RuntimeTables( LoadExportsFromFile );

		explicit RuntimeTables( const AsyncSourcePtr& asyncSourceForBuffers );

		void Resolve( const LoadingContext& context, const FileTables& fileTables );

		/// create the objects
		void CreateExports( const LoadingContext& context, const FileTables& fileTables );

		/// load the export data from the source data
		void LoadExports( Uint64 baseOffset, IFile& file, job::Counter& loadingCounter, const LoadingContext& context, const FileTables& fileTables, LoadingResult& outResult, AsyncSourceReadBuffer bufferInlineData = AsyncSourceReadBuffer());

		/// wait until all imports are loaded, ACTIVELY! This will loop and wait.
		void WaitUntilImportsAreLoaded_ACTIVELY() const;

		/// post load all objects
		bool PostLoad( const LoadingContext& loadingContext, job::Builder& builder );

		void RegisterInplaceResources( const LoadingContext& context, const FileTables& fileTables, job::Counter& loadingCounter );

	private:
		/// unmapping interface, index -> something
		virtual void UnmapName( const NameIndex index, CName& outName ) override final;
		virtual void UnmapType( const TypeIndex index,const rtti::IType*& outTypeRef ) override final;
		virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) override final;
		virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle& outToken ) override final;
		virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) override final;
		virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, BufferAsyncPtr& outBufferAccess ) override final;

		/// mapping interface - not used
		virtual void MapName( const CName& name, NameIndex& outIndex ) override final {};
		virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) override final {};
		virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) override final {};
		virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) override final {};
		virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) override final {};
		virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) override final {};

		virtual bool IsRelinking() const override final { return false; }
		virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const override final { return path; }

		void ResolveNames( const LoadingContext& context, const FileTables& fileTables );
		void ResolveImports( const LoadingContext& context, const FileTables& fileTables );
		void ResolveExports( const LoadingContext& context, const FileTables& fileTables );
		void ResolveBuffers( const LoadingContext& context, const FileTables& fileTables );

		AsyncSourcePtr m_asyncSourceForBuffers;
		AsyncSourceReadBuffer m_currentBufferInlineData;
		IFile* m_currentFile;
		job::Counter* m_currentLoadingCounter;
		res::ResourceLoaderThrottler* m_currentThrottler;
		io::EAsyncPriority m_currentPriority;

		// Hack to skip all buffer loading when not wanted or desired (i.e. some commandlets)
		bool m_skipBuffers;

		// Value set from the loading context
		bool m_verifyExportTypes;
	};

} // serialization