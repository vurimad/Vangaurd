/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"

#include "../../redFileSystem/include/file.h"
#include "../../redJobs2/include/jobRunner.h"

#include "serializationMapping.h"
#include "serializationFileTables.h"
#include "serializationLoader.h"
#include "serializationBinaryRuntimeTables.h"
#include "bufferAsyncProxy.h"
#include "resource.h"
#include "gatheredResource.h"
#include "resourceLoader.h"
#include "resourceToken.h"
#include "backendData.h"
#include "../../redJobs2/include/jobBuilder.h"

namespace dd
{
	static red::CrashDataThreadLocal<res::ResourcePath> initialResourcePath("Engine/LoadExports", "InitialResourcePath");
	static red::CrashDataThreadLocal<res::ResourcePath> resourcePath("Engine/LoadExports", "ResourcePath");
}

namespace serialization
{
	RuntimeTables::RuntimeTables( const AsyncSourcePtr& asyncSourceForBuffers )
		: m_asyncSourceForBuffers( asyncSourceForBuffers )
		, m_currentBufferInlineData()
		, m_currentFile( nullptr )
		, m_currentLoadingCounter( nullptr )
		, m_currentThrottler( nullptr )
		, m_currentPriority( io::eAsyncPriority_Normal )
		, m_skipBuffers( false )
		, m_verifyExportTypes( false )
	{
	}

	RuntimeTables::RuntimeTables( NoLoading )
		: m_asyncSourceForBuffers( nullptr )
		, m_currentFile( nullptr )
		, m_currentLoadingCounter( nullptr )
		, m_currentThrottler( nullptr )
		, m_currentPriority( io::eAsyncPriority_Normal )
		, m_skipBuffers( false )
		, m_verifyExportTypes( false )
	{
	}

	RuntimeTables::RuntimeTables( LoadExportsFromFile )
		: m_asyncSourceForBuffers( nullptr )
		, m_currentFile( nullptr )
		, m_currentLoadingCounter( nullptr )
		, m_currentThrottler( nullptr )
		, m_currentPriority( io::eAsyncPriority_Normal )
		, m_skipBuffers( false )
		, m_verifyExportTypes( false )
	{
	}

	void RuntimeTables::Resolve( const LoadingContext& context, const FileTables& fileTables )
	{
		PC_SCOPE( Resolve );

		// Create runtime data
		m_mappedNames.Resize( fileTables.m_names.Size() );
		m_mappedTypes.Resize( fileTables.m_names.Size() );
		m_mappedImports.Resize( fileTables.m_imports.Size() );
		m_mappedExports.Resize( fileTables.m_exports.Size() );
		m_mappedBuffers.Resize( fileTables.m_buffers.Size() );

		// Resolve data
		ResolveNames( context, fileTables );
		ResolveImports( context, fileTables );
		ResolveExports( context, fileTables );
		ResolveBuffers( context, fileTables );
	}

	void RuntimeTables::CreateExports( const LoadingContext& context, const FileTables& fileTables )
	{
		PC_SCOPE( CreateExports );

		for ( Uint32 exportIndex = 0; exportIndex < m_mappedExports.Size(); ++exportIndex )
		{
			const auto& ex = fileTables.m_exports[ exportIndex ];
			auto& runtime = m_mappedExports[ exportIndex ];

			// skip the object
			if ( runtime.m_skip )
				continue;

			// Determine parent object 
			THandle< ISerializable > objectParent = context.m_parent;
			if ( ex.m_parent )
			{
				const auto& parent = m_mappedExports[ ex.m_parent-1 ];
				if( parent.m_object )
				{
					RED_FATAL_ASSERT( parent.m_class != nullptr, "Parent object with no class" ); // should be handled by the skip logic
					objectParent = parent.m_object;
					RED_FATAL_ASSERT( parent.m_object, "Parent missing and no skip flag set" );
				}
				else
				{ 
					// ctremblay Object has a parent but nowhere to be found. However object can have multiple parent. 
					// Create anyway. Worst case object will be delete at the end of loading and discarded next cook or next resource (re)save.
				}
			}

			// Object cannot be already created
			RED_FATAL_ASSERT( !runtime.m_object, "Object already created" );

			// ISerializables are more complicated
			const rtti::ClassType* objectClass = runtime.m_class;

			// Class is transient, skip loading
			if ( objectClass->IsAlwaysTransient() )
			{
				RED_LOG_WARNING( "Core: Skipping loading of transient class '%hs'.", objectClass->GetName().AsChar() );
				runtime.m_skip = true;
				continue;
			}

			THandle< ISerializable > createdObject = objectClass->CreateHandle< ISerializable >();
			RED_FATAL_ASSERT( createdObject, "Unable to create export of class '%hs'", objectClass->GetName().AsChar() );

			if ( exportIndex == 0 )
			{
				// Create first root object, assign it the resource path
				auto resource = Cast< CResource >( createdObject );
				if ( resource )
				{
					resource->Internal_SetPath( context.m_resourcePath );
					resource->Internal_SetCreationId( context.m_creationId );
				}
			}

			// setup parent
			createdObject->SetParent( objectParent.Get() );

			// remember
			runtime.m_object = createdObject;
		}
	}

	void RuntimeTables::LoadExports( Uint64 baseOffset, IFile& file, job::Counter& loadingCounter, const LoadingContext& context, const FileTables& fileTables, LoadingResult& outResult, AsyncSourceReadBuffer bufferInlineData )
	{
		PC_SCOPE( Serialization_LoadExports );

		RED_SET_SCOPED_CRASH_DATA( dd::initialResourcePath, context.m_initialResourcePath);
		RED_SET_SCOPED_CRASH_DATA( dd::resourcePath, context.m_resourcePath);

		m_skipBuffers = context.m_skipBuffers;
		m_verifyExportTypes = context.m_verifyExportTypes;

		m_currentBufferInlineData = std::move(bufferInlineData);
		
		for ( Uint32 exportIndex = 0; exportIndex < m_mappedExports.Size(); ++exportIndex )
		{
			const auto& ex = fileTables.m_exports[ exportIndex ];
			auto& runtime = m_mappedExports[ exportIndex ];

			// No data, skip the object
			if ( !runtime.m_object || runtime.m_skip )
			{
				continue;
			}
#if defined( RED_PLATFORM_CONSOLE ) || defined( RED_CONFIGURATION_FINAL )
			else if( runtime.m_exportParent != -1 )
			{
				if( m_mappedExports[ runtime.m_exportParent ].m_skip )
				{
					runtime.m_skip = true;
					continue;

				}
			}
#endif

			// Sanity checks
			RED_FATAL_ASSERT( ex.m_dataSize > 0, "Export %d in %hs has no data", exportIndex, file.GetFileNameForDebug() );

			// Move to file position
			const Uint64 objectStart = baseOffset + ex.m_dataOffset;
			file.Seek( objectStart );
			file.ClearError();

			// Load object
			const rtti::ClassType* classType = runtime.m_object->GetClass();
			const red::memory::Pool & pool = classType->GetInnerTypeMemoryPool();
 			m_currentFile = &file;
			m_currentLoadingCounter = &loadingCounter;
			m_currentThrottler = context.m_resourceLoader ? context.m_resourceLoader->GetThrottler() : nullptr;
			m_currentPriority = (context.m_priority == io::eAsyncPriority_Streaming) ? io::eAsyncPriority_Normal : context.m_priority; // DDBs shouldn't use streaming priority, since nothing will update them!!
 			m_currentFile->m_mapper = this;
 			m_currentFile->SetInternalMemoryPool( pool );

			{
				PC_SCOPE_INST_OBJ( classType->GetInstrumentationObject(), classType->GetName().AsChar() );
				runtime.m_object->OnSerialize( file );
			}


			m_currentFile->m_mapper = nullptr;
			m_currentPriority = io::eAsyncPriority_Normal;
			m_currentThrottler = nullptr;
			m_currentLoadingCounter = nullptr;
 			m_currentFile = nullptr;

			// Emit exported root
			if ( ex.m_parent == 0 )
			{
				outResult.m_loadedRootObjects.PushBack( runtime.m_object );
			}

			// All object export
			if ( context.m_getAllLoadedObjects )
			{
				outResult.m_loadedObjects.PushBack( runtime.m_object );
			}

#ifndef RED_CONFIGURATION_FINAL
			// Seek past the end of the file ?
			const Uint64 objectEnd = baseOffset + ex.m_dataOffset + ex.m_dataSize;
			if ( file.HasErrors() )
			{
				RED_LOG_ERROR( "Core: Export %d ('%hs') from '%hs' caused file IO errors when reading (start=%d, size=%d)",
					exportIndex, runtime.m_class->GetName().AsChar(), file.GetFileNameForDebug(), objectStart, ex.m_dataSize );
			}
			else if ( file.GetOffset() > objectEnd )
			{
				RED_LOG_ERROR( "Core: Export %d ('%hs') from '%hs' accessed data beyond it's location in file (offset=%d, start=%d, stop=%d)",
					exportIndex, runtime.m_class->GetName().AsChar(), file.GetFileNameForDebug(), file.GetOffset(), objectStart, objectEnd );
			}
#endif
		}

		m_currentBufferInlineData.Reset();
	}

	bool RuntimeTables::PostLoad( const LoadingContext& loadingContext, job::Builder& builder )
	{
		PC_SCOPE( Serialization_PostLoad );

		PostLoadContext postLoadContext{loadingContext.m_postLoadFlags};
		postLoadContext.hackLoadingContext = &loadingContext;

		for( auto iter = m_mappedExports.RBegin(), end = m_mappedExports.REnd(); iter != end; ++iter )
		{
			auto& runtime = *iter;

			if( runtime.m_object && !runtime.m_skip )
			{
				if( !loadingContext.m_skipPostLoad )
				{
					Bool isResponsibleForPostLoad = true;
					if (runtime.m_inplaceResourceIndex != -1)
					{
						ResolvedInplaceResource& inplaceResource = m_mappedInplaceResources[runtime.m_inplaceResourceIndex];

						// We didn't use our own "inplaceResource" token, so not our responsibility
						// we already have a loading dependency on the existing token
						if (!inplaceResource.m_token)
						{
							isResponsibleForPostLoad = false;
						}
					}

					if (isResponsibleForPostLoad)
					{
						runtime.m_object->OnPostLoad( postLoadContext );
					}
				}
				
				if( runtime.m_inplaceResourceIndex != -1 )
				{
					auto resource = Cast< CResource >( runtime.m_object );
					if( resource )
					{
						ResolvedInplaceResource& inplaceResource = m_mappedInplaceResources[ runtime.m_inplaceResourceIndex ];
						if( inplaceResource.tokenRegistered )
						{
							if( inplaceResource.assignResourceOnPostLoad )
							{
								// ctremblay: this resource manage to register token first. No one should set resource in this token.
								// However, another resource could have manage to register resource before us!
								// Try to register created resource.
								auto registeredResource = loadingContext.m_resourceLoader->Internal_TryRegisterResource( inplaceResource.m_token->GetPath(), resource );
								inplaceResource.m_token->Internal_AssignLoadedResource( registeredResource, std::move( inplaceResource.m_deferral ) );
							}
							else
							{
								// ctremblay: We registered token AND resource already existed. We are done here.
							}
						}
						else
						{
							// ctremblay: We did not registered token. Another resource did! But I might be ready first !
							auto registeredResource = loadingContext.m_resourceLoader->Internal_TryRegisterResource( inplaceResource.m_token->GetPath(), resource );
							inplaceResource.m_token->Internal_AssignLoadedResource( registeredResource, std::move( inplaceResource.m_deferral ) );
						}
					
						const job::Counter* counter = resource->HACK_GetPostLoadWaitCounter();
						if( counter )
						{
							builder.DispatchWait( *counter );
						}
					}
				}
			}
		}
		
		return !loadingContext.m_skipPostLoad;
	}

	void RuntimeTables::UnmapName( const NameIndex index, CName& outName )
	{
#ifndef RED_CONFIGURATION_FINAL
		if ( index >= m_mappedNames.Size() )
		{
			RED_LOG_ERROR( "Core: Invalid name index %d (of %d). Mapping to None.", index, m_mappedNames.Size());
			outName = CName();
			return;
		}
#endif

		outName = m_mappedNames[ index ];
	}

	void RuntimeTables::UnmapType( const TypeIndex index,const rtti::IType*& outTypeRef )
	{
#ifndef RED_CONFIGURATION_FINAL
		if ( index >= m_mappedNames.Size() )
		{
			RED_LOG_ERROR( "Core: Invalid type ref index %d (of %d). Mapping to NULL.", index, m_mappedNames.Size());
			outTypeRef = nullptr;
			return;
		}
#endif

		if ( !m_mappedTypes[ index ] )
		{
			const auto typeName = m_mappedNames[ index ];
			m_mappedTypes[ index ] = GetRttiSystem().FindType( typeName );

			if ( m_verifyExportTypes )
			{
				if ( !m_mappedTypes[ index ] )
				{
					RED_FATAL( "Core: Missing referenced type '%hs'", typeName.AsChar() );
				}
			}
		}


		outTypeRef = m_mappedTypes[ index ];
	}

	void RuntimeTables::UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef )
	{
		RED_FATAL_ASSERT( index >= 0, "THandle of resource cannot point to disk resource. Use TResRef instead." );

		if ( index > 0 )
		{
			const Int32 exportIndex = (index-1);
#ifndef RED_CONFIGURATION_FINAL
			if ( exportIndex >= (Int32)m_mappedExports.Size() )
			{
				RED_LOG_ERROR( "Core: Invalid export index %d (of %d). Mapping to NULL.", exportIndex, m_mappedExports.Size() );

				outObjectRef = nullptr;
			}
			else
#endif
			{
				const auto& data = m_mappedExports[ exportIndex ];
				outObjectRef = data.m_object;
			}
		}
		else
		{
			// NULL object
			outObjectRef = nullptr;
		}
	}

	void RuntimeTables::UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle & outToken )
	{
		// NULL resource
		if ( index == 0 )
		{
			outPath = res::ResourcePath();
			outToken = nullptr;
			return;
		}

		RED_FATAL_ASSERT( index != 0, "The index of 0 should be mapped to null resource." );
		const Uint32 importIndex = index - 1U;

#ifndef RED_CONFIGURATION_FINAL
		if ( importIndex >= m_mappedImports.Size() )
		{
			RED_LOG_ERROR( "Core: Invalid resource reference index %d (of %d). Mapping to None.", importIndex, m_mappedImports.Size());
			outPath = res::ResourcePath();
			outToken = nullptr;
			return;
		}
#endif

		const auto& info = m_mappedImports[ importIndex ];
		outPath = info.m_path;

		if( info.m_loadingToken )
		{
			outToken = info.m_loadingToken;
		}
	}

	void RuntimeTables::UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath )
	{
		// NULL resource
		if ( index == 0 )
		{
			outPath = res::ResourcePath();
			return;
		}

		RED_FATAL_ASSERT( index != 0, "The index of 0 should be mapped to null resource." );
		const Uint32 importIndex = index - 1U;

#ifndef RED_CONFIGURATION_FINAL
		if ( importIndex >= m_mappedImports.Size() )
		{
			RED_LOG_ERROR( "Core: Invalid resource reference index %d (of %d). Mapping to None.", importIndex, m_mappedImports.Size());
			outPath = res::ResourcePath();
			return;
		}
#endif

		const auto& info = m_mappedImports[ importIndex ];
		outPath = info.m_path;
	}

	void RuntimeTables::UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, BufferAsyncPtr& outBufferAccess )
	{
		outBufferAccess.Reset();

		// empty buffer
		if ( index != 0 && !m_skipBuffers )
		{
#ifndef RED_CONFIGURATION_FINAL
			// validate buffer index
			if ( index > m_mappedBuffers.Size() )
			{
				RED_LOG_ERROR( "Core: Invalid buffer index %d (of %d). Mapping to None.", index, m_mappedBuffers.Size() );
			}
			else
#endif
			{
				// get buffer description from buffer table
				const Int16 bufferIndex = index - 1;
				const auto& bufferInfo = m_mappedBuffers[ bufferIndex ];

				if ( m_asyncSourceForBuffers )
				{
					AsyncSourceBufferAsyncProxySetup setup;
					{
						setup.source = m_asyncSourceForBuffers;
						setup.fileOffset = bufferInfo.m_dataOffset;
						setup.sizeInMemory = bufferInfo.m_dataSizeInMemory;
						setup.sizeOnDisk = bufferInfo.m_dataSizeOnDisk;
					}

					outBufferAccess = IBufferAsyncProxy::CreateFromAsyncSource( setup );
					// Kick off loading before finished unmapping! Need to pass in the buffer access since it hasn't been set yet.
					if ( token.isAutoload )
					{
						RED_FATAL_ASSERT( token.callback );
						MapperLoadBufferParam callbackParam;
						{
							callbackParam.throttler = m_currentThrottler;
							callbackParam.userData = token.userData;
							callbackParam.priority = m_currentPriority;
							callbackParam.inlineData = &m_currentBufferInlineData;
						}

						*m_currentLoadingCounter += token.callback(callbackParam, *outBufferAccess).GetWaitCounter();
					}
				}
				else if ( m_currentFile )
				{						
					RED_FATAL_ASSERT( m_currentFile->IsMemoryBased() || m_currentFile->IsCooker(), "Use an asyncSource for files in the game!" );
					// We must load the buffer now, regardless of whether it's autoload or not. We don't own the memory backed in the file.
					// Note: current file offset will remain unchanged after this
					MemoryFileBufferAsyncProxySetup setup;
					{
						setup.file = m_currentFile;
						setup.fileOffset = bufferInfo.m_dataOffset;
						setup.sizeInMemory = bufferInfo.m_dataSizeInMemory;
						setup.sizeOnDisk = bufferInfo.m_dataSizeOnDisk;
					}

					outBufferAccess = IBufferAsyncProxy::CreateFromMemoryFile( setup );
				}
				else
				{
					// #tbd:
					RED_LOG_WARNING( "Can't load DDB. Skipping!");
				}
			}
		}
	}

	void RuntimeTables::ResolveNames( const LoadingContext& context, const FileTables& fileTables )
	{
		// Map names
		for ( Uint32 i = 0; i < m_mappedNames.Size(); ++i )
		{
			// get text
			const AnsiChar* string = &fileTables.m_strings[ fileTables.m_names[i].m_string ];
			m_mappedNames[i] = RED_NAME( string );

			// types are not mapped until requested (slow)
			m_mappedTypes[i] = nullptr;
		}
	}

	void RuntimeTables::ResolveImports( const LoadingContext& context, const FileTables& fileTables )
	{
		// Map imports
		for (Uint32 index = 0, end = fileTables.m_inplace.Size(); index != end; ++index)
		{
			const FileTables::InplaceResource& inplaceResouce = fileTables.m_inplace[index];
			RED_FATAL_ASSERT(!m_mappedImports[inplaceResouce.m_importIndex - 1].m_inplaceForDebug);
			m_mappedImports[inplaceResouce.m_importIndex - 1].m_inplaceForDebug = true;
		}

		for ( Uint32 i = 0; i < m_mappedImports.Size(); ++i )
		{
			const auto& data = fileTables.m_imports[i];
			ResolvedImport& resolvedImport = m_mappedImports[i];

			// resolve path
			const AnsiChar* string = &fileTables.m_strings[ data.m_path ];
			resolvedImport.m_path = res::ResourcePath::Build( string );	

			// #todo: clean this up in another CL when can make sure it's 100% safe to do so.
			// !!! NOTE: easy to misinterpret flags check here, and should probably really have been an enum.
			// Flags is all zero for hard deps and we're just skipping if eImportFlags_Soft is set. E.g., skip if TResAsyncRef.
			// NOTHING seems to set or use eImportFlags_Obligatory set anymore. Probably some W3 legacy.

			if ( !context.m_skipImports && 0 == (data.m_flags & ( FileTables::eImportFlags_Soft | FileTables::eImportFlags_Inplace ) ) )
			{
				// start loading (if not a soft import)
				const auto importPath = resolvedImport.m_path;
				RED_FATAL_ASSERT( context.m_resourcePath != importPath, "Trying to load ourselves as a dependency '%hs'", importPath.ToDebugString() );
				auto param = res::ResourceLoader::GetImportLoadingRequestParameter( importPath, context );
				if (!resolvedImport.m_inplaceForDebug)
				{
					resolvedImport.m_loadingToken = context.m_resourceLoader->IssueLoadingRequest( param );
					resolvedImport.waitOnImport = true;
				}
			}
		}
	}

	void RuntimeTables::WaitUntilImportsAreLoaded_ACTIVELY() const
	{
		while (true)
		{
			Bool allImportsAreFinished = true;
			for ( Uint32 i = 0; i < m_mappedImports.Size(); ++i )
			{
				if ( ( m_mappedImports[i].m_loadingToken != nullptr ) && !m_mappedImports[i].m_loadingToken->HasFinished() )
				{
					allImportsAreFinished = false;
					break;
				}
			}
			if ( allImportsAreFinished )
			{
				break;
			}
		}
	}

	void RuntimeTables::ResolveExports( const LoadingContext& context, const FileTables& fileTables )
	{
		// Map exports
		for ( Uint32 i = 0; i < m_mappedExports.Size(); ++i )
		{
			const auto& data = fileTables.m_exports[i];

			// map export type
			const rtti::IType* type = nullptr;
			UnmapType( data.m_className, type );

			if ( m_verifyExportTypes )
			{
				if ( !type )
				{
					CName typeName;
					UnmapName( data.m_className, typeName );

					RED_FATAL( "Core: Unknown type for export %d ('%hs')", i, typeName.AsChar() );
				}
				else if ( type->GetType() != RT_Class )
				{
					RED_FATAL( "Core: Non class type for export %d ('%hs')", i, type->GetName().AsChar() );
					type = nullptr;
				}
			}

			// store
			auto& mapped = m_mappedExports[i];
			mapped.m_class = static_cast< const rtti::ClassType* >( type );
			mapped.m_exportParent = data.m_parent - 1;
			if( type )
			{
				mapped.m_skip = false;

				if( i != 0 && context.m_filterClassType )
				{
					mapped.m_skip = !mapped.m_class->IsA( context.m_filterClassType );
				}
				else if( context.m_skipBackendData )
				{
					mapped.m_skip = mapped.m_class->IsA< IBackendData >(); 
				}
			}
			else
			{
				mapped.m_skip = true;
			}
		}
	}		

	void RuntimeTables::ResolveBuffers( const LoadingContext& context, const FileTables& fileTables )
	{
		// Map exports
		for ( Uint32 i = 0; i < m_mappedBuffers.Size(); ++i )
		{
			const auto& bufferInfo = fileTables.m_buffers[i];

			// buffer is empty - this should not happen
			RED_FATAL_ASSERT( bufferInfo.m_dataOffset != 0, "Buffer %d is marked as external. Not supported.", i );
			RED_FATAL_ASSERT( bufferInfo.m_dataSizeOnDisk > 0, "Buffer %d with a valid index has no size", i );
			RED_FATAL_ASSERT( bufferInfo.m_crc > 0, "Buffer %d has invalid CRC even though it's not empty (%d)", i, bufferInfo.m_dataSizeInMemory );

			auto& mapped = m_mappedBuffers[i];
			mapped.m_dataOffset = bufferInfo.m_dataOffset;
			mapped.m_dataSizeOnDisk = bufferInfo.m_dataSizeOnDisk;
			mapped.m_dataSizeInMemory = bufferInfo.m_dataSizeInMemory;
		}
	}

	void RuntimeTables::RegisterInplaceResources( const LoadingContext& context, const FileTables& fileTables, job::Counter& loadingCounter )
	{
		m_mappedInplaceResources.Resize( fileTables.m_inplace.Size() );

		for( Uint32 index = 0, end = fileTables.m_inplace.Size(); index != end; ++index )
		{
			const FileTables::InplaceResource& inplaceResouce = fileTables.m_inplace[ index ];
			res::ResourcePath path;
			UnmapResourceDeferredReference( inplaceResouce.m_importIndex, path );
			if( path.IsValid() )
			{
				auto& resolvedExport = m_mappedExports[ inplaceResouce.m_exportIndex ];
				auto& resolvedImport = m_mappedImports[ inplaceResouce.m_importIndex - 1 ];
				auto& resolvedInplaceResource = m_mappedInplaceResources[ index ];

				const THandle< CResource > resource = Cast< CResource >( resolvedExport.m_object );

				if( resource )
				{
					resource->Internal_SetPath( path );
					std::pair< res::ResourceTokenHandle, job::CompletionDeferral > pair = res::CreateResourceToken( path );
					res::ResourceTokenHandle registeredToken = context.m_resourceLoader->Internal_TryRegisterResourceToken( pair.first );
					if( registeredToken == pair.first )
					{
						resolvedInplaceResource.tokenRegistered = true;

						THandle< CResource > registeredResource = context.m_resourceLoader->TryAcquiringLoadedResource( path );
						if( !registeredResource )
						{
							resolvedInplaceResource.m_deferral = std::move( pair.second );
							resolvedInplaceResource.assignResourceOnPostLoad = true;
						}
						else
						{
							registeredToken->Internal_AssignLoadedResource( registeredResource, std::move( pair.second ) );
							resolvedExport.m_skip = true;
						}
					}
					else
					{
						if( registeredToken->IsLoaded() )
						{
							// ctremblay: we did not win token Registration. But resource is loaded already!
							// Not need to deserialize or wait on result. 
							resolvedExport.m_skip = true;
						}
						else
						{
							// ctremblay: we did not win token Registration. We will need to wait on it.
							// Because we can introduce circular dependency when loading data that is inplace in multiple resource,
							// we still need to go forward with serialization and make decision at later stage.
							resolvedInplaceResource.assignResourceOnPostLoad = true;
						}
					}

					resolvedInplaceResource.m_token = registeredToken;
					resolvedInplaceResource.m_exportIndex = inplaceResouce.m_exportIndex;

					resolvedImport.m_loadingToken = registeredToken;
					resolvedExport.m_inplaceResourceIndex = index;
				}
			}
			else
			{ /* ctremblay: If path is not valid, file is most likely corrupted. But it can be gracefully handled in most case. */ }
		}
	}

} // serialization
