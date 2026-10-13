/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageCompiler.h"
#include "packageWriteStream.h"
#include "packageTableCompiler.h"
#include "packageLayoutSaver.h"
#include "packageVersion.h"
#include "packageReadWriteStream.h"

#ifndef RED_CONFIGURATION_FINAL
	#include "pathResolver.h"
#endif

namespace red
{
	const Uint32 c_packageCompilerStreamInitialSize = RED_KILO_BYTE( 4 );

	class PackageSpanStream : public PackageStream
	{
	public:
		PackageSpanStream( red::BlobSpan data );
		~PackageSpanStream();

	private:
		virtual Uint64 OnRead( void * data, Uint64 size, Uint64 position ) override final;
		virtual Uint64 OnWrite( const void * data, Uint64 size, Uint64 position ) override final;

		virtual Uint64 OnSeek( Uint64 positionRequest ) override final;

		red::BlobSpan m_data;
	};

	PackageSpanStream::PackageSpanStream( red::BlobSpan data )
		: m_data( data )
	{}

	PackageSpanStream::~PackageSpanStream()
	{}

	Uint64 PackageSpanStream::OnRead( void * data, Uint64 size, Uint64 position ) 
	{
		RED_FATAL( "PackageSpanStream only open for Write" );
		return 0;
	}

	Uint64 PackageSpanStream::OnWrite( const void * data, Uint64 size, Uint64 position )
	{
		RED_FATAL_ASSERT( size + position <= m_data.Size(), "Out of Range Write request." );
		red::Memcpy( m_data.Data( static_cast< Uint32 >( position ) ), data, static_cast< Uint32 >( size ) );
		return size;
	}

	Uint64 PackageSpanStream::OnSeek( Uint64 positionRequest )
	{
		return positionRequest;
	}


	PackageCompiler::PackageCompiler()
		: m_inputTable( nullptr )
		, m_nameTable( red::PoolEngine() )
		, m_discardResourcePathString( false )
		, m_discardNameString( false )
	{}

	PackageCompiler::~PackageCompiler()
	{
	}

	void PackageCompiler::Initialize( const PackageCompilerParameter & param )
	{
		m_inputTable = param.inputTable;
		m_discardResourcePathString = param.discardResourcePathString;

		m_table.rootObjectTable.Reserve( m_inputTable->rootObjectTable.Size() );
		m_table.objectLookup.Reserve( m_inputTable->objectLookup.Size() );
		m_table.objectTable.Reserve( m_inputTable->objectTable.Size() );
		m_table.stringLookup.Reserve( m_inputTable->stringLookup.Size() );
		m_table.stringTable.Reserve( m_inputTable->stringTable.Size() );
		m_table.resourceTable.Reserve( m_inputTable->resourceTable.Size() );
		
		PackageTableCompilerParameter tableCompilerParam = 
		{
			m_inputTable,
			&m_table,
			param.propertyData,
			param.serializerDictionary,
			param.memoryReserve
		};

		m_tableCompiler = CreatePackageTableCompiler( tableCompilerParam );
	}

	CompiledPackage PackageCompiler::Compile( const PackageLayoutSaver & saver )
	{
		PC_SCOPE( PackageCompiler_Compile );

		CompileIntermediateTable();
	#ifndef RED_CONFIGURATION_FINAL
		Internal_ResolveResourceTable();
	#endif

		CompilationContext context = BeginCompilation( saver );
			CompileResourceTable( context );
			CompileStringTable( context );
			CompileObjectTable( context );
		EndCompilation( context );

		Package package = GeneratePackage( context );

		PackageSpanStream stream( red::BlobSpan( context.result.Get(), context.startOffset ) );
		saver.WriteLayout( package, stream );

		return { package, std::move( context.result ) };
	}

	void PackageCompiler::CompileIntermediateTable()
	{
		m_propertyBuffer = m_tableCompiler->Execute();
	}

	PackageCompiler::CompilationContext PackageCompiler::BeginCompilation( const PackageLayoutSaver & saver )
	{
		PackageCompiler::CompilationContext context = {};

		const Uint32 layoutSize = saver.GetLayoutSize( m_table );
		const Uint32 streamSize = layoutSize + ComputePackageDataSize( m_table, m_nameTable, m_discardResourcePathString );
	
		m_outputStream = CreateWriteStream( streamSize );

		m_outputStream->Skip( layoutSize );

		context.startOffset = m_outputStream->GetPosition();
		return context;
	}

	void PackageCompiler::EndCompilation( CompilationContext & context )
	{
		context.endOffset = m_outputStream->GetPosition();
		context.result = m_outputStream->ReleaseBuffer();
	}

	void PackageCompiler::CompileResourceTable( CompilationContext& context )
	{
		context.resourceOffset = m_outputStream->GetPosition();

		red::ArraySpan< ResourceDescriptor > resourceTable( static_cast< ResourceDescriptor* >( m_outputStream->GetWriteCursor() ), m_table.resourceTable.Size() );

		m_outputStream->Skip( resourceTable.SizeInBytes() );

		for( Uint32 index = 0, end = m_table.resourceTable.Size(); index != end; ++index )
		{
			const PackageTableResourceDescriptor & descriptor = m_table.resourceTable[ index ]; 
			const res::ResourcePath path = descriptor.path;
			const Uint64 offset = m_outputStream->GetPosition() - context.startOffset;

			if( !m_discardResourcePathString )
			{
				const red::StringView pathView = path.ToStringView();
				const Uint32 size = pathView.Length();
				resourceTable[ index ] = { static_cast< Uint32 >( offset ), size, static_cast< Uint32 >( descriptor.importType ) };
				m_outputStream->Write( pathView.Data(), size );
			}
			else
			{
				const Uint64 pathHash = path.GetHash();
				const Uint32 size = sizeof( pathHash );
				resourceTable[ index ] = { static_cast< Uint32 >( offset ), size, static_cast< Uint32 >( descriptor.importType ) };
				m_outputStream->Write( &pathHash, size );
			}
		}
	}
	
	void PackageCompiler::CompileStringTable( CompilationContext& context )
	{
		context.stringOffset = m_outputStream->GetPosition();
	
		red::ArraySpan< StringDescriptor > stringTable( static_cast< StringDescriptor* >( m_outputStream->GetWriteCursor() ), m_table.stringTable.Size() );

		m_outputStream->Skip( stringTable.SizeInBytes() );

		for( Uint32 index = 0, end = m_table.stringTable.Size(); index != end; ++index )
		{
			const auto str =  m_nameTable[ index ];
			const Uint64 offset = m_outputStream->GetPosition() - context.startOffset;
			const Uint32 size = str.Length() + 1;
			stringTable[ index ] = { static_cast< Uint32 >( offset ), size };
			m_outputStream->Write( str.Data(), size );
		}
	}
	
	void PackageCompiler::CompileObjectTable( CompilationContext& context )
	{
		context.objectOffset = m_outputStream->GetPosition();

		red::ArraySpan< ObjectDescriptor > objectTable( static_cast< ObjectDescriptor* >( m_outputStream->GetWriteCursor() ), m_table.objectTable.Size() );
		red::BlobSpan propertyData = MakeBlobSpan( m_propertyBuffer );
		m_outputStream->Skip( objectTable.SizeInBytes() );
		const Uint64 dataPosition = m_outputStream->GetPosition();

		for( Uint32 index = 0, end = m_table.objectTable.Size(); index != end; ++index )
		{
			const PackageTableObjectDescriptor & input = m_table.objectTable[ index ];
			const red::BlobView inputPropertyData = propertyData.Range( input.propertyDataOffset, input.propertyDataSize );
			const Uint64 outputStreamPosition = m_outputStream->GetPosition() - context.startOffset;
			
			m_outputStream->Write( inputPropertyData.Data(), inputPropertyData.Size() );
				
			ObjectDescriptor & output = objectTable[ index ];
			output = { input.nameIndex, 0, static_cast< Uint32 >( outputStreamPosition ) };
		}
	}

	Package PackageCompiler::GeneratePackage( CompilationContext& context )
	{
		red::BlobSpan bufferSpan( context.result.Get(), context.result.GetSize() );
		
		Package::ObjectTable objectTable = 
			red::MakeArraySpan( bufferSpan.Pointer< ObjectDescriptor >( static_cast< Uint32 >( context.objectOffset ) ), m_table.objectTable.Size() );

		Package::StringTable stringTable = 
			red::MakeArraySpan( bufferSpan.Pointer< StringDescriptor >( static_cast< Uint32 >( context.stringOffset ) ), m_table.stringTable.Size() );

		Package::ResourceTable resourceTable =  
			red::MakeArraySpan( bufferSpan.Pointer< ResourceDescriptor >( static_cast< Uint32 >( context.resourceOffset ) ), m_table.resourceTable.Size() );

		Package package;
		package.version = c_packageCurrentVersion;
		package.rootObjectTable = objectTable.Left( m_table.rootObjectTable.Size() );
		package.objectTable = objectTable;
		package.stringTable = stringTable;
		package.resourceTable = resourceTable;
		package.buffer = bufferSpan.Range( static_cast< Uint32 >( context.startOffset ) );

		return package;
	}

	void PackageCompiler::Internal_SetTableCompiler( red::UniquePtr< PackageTableCompiler > tableCompiler )
	{
		m_tableCompiler = std::move( tableCompiler );
	}

	red::UniquePtr< PackageCompiler > CreatePackageCompiler( const PackageCompilerParameter & param )
	{
		red::UniquePtr< PackageCompiler > compiler = red::CreateUniquePtr< PackageCompiler >();
		compiler->Initialize( param );
		return compiler;
	}

#ifndef RED_CONFIGURATION_FINAL
	void PackageCompiler::Internal_ResolveResourceTable()
	{
		if( !s_pathResolver )
			return;

		for( Uint32 index = 0, end = m_table.resourceTable.Size(); index != end; ++index )
		{
			PackageTableResourceDescriptor & descriptor = m_table.resourceTable[index];
			const auto resolvedPath = Internal_ResolvePath( descriptor.path );

			descriptor.path = resolvedPath;
		}
	}

	serialization::PathResolver* PackageCompiler::Internal_SetPathResolver( serialization::PathResolver* filter )
	{
		auto* oldResolver = s_pathResolver;
		s_pathResolver = filter;
		return oldResolver;
	}

	serialization::PathResolver* PackageCompiler::s_pathResolver = nullptr;

	res::ResourcePath PackageCompiler::Internal_ResolvePath( const res::ResourcePath path )
	{
		if( !s_pathResolver )
			return path;

		return s_pathResolver->ResolvePath( path );
	}
#endif
}
