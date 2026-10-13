/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "../../redContainers/include/blob.h"
#include "package.h"
#include "packageTable.h"

namespace serialization
{
	class PathResolver;
}//

namespace red
{
	class PackageLayoutSaver;
	class PackageWriteStream;
	class PackageTableCompiler;
	class PackageTypeSerializerDictionary;

	struct PackageCompilerParameter
	{
		red::BlobSpan propertyData;
		PackageTable* inputTable = nullptr;
		const PackageTypeSerializerDictionary * serializerDictionary = nullptr;
		Uint32 memoryReserve = RED_KILO_BYTE( 4 );
		bool discardResourcePathString = false;
		bool discardNameString = false; 
	};

	class PackageCompiler
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		PackageCompiler();
		~PackageCompiler();

		void Initialize( const PackageCompilerParameter & param );

		CompiledPackage Compile( const PackageLayoutSaver & saver );
		
		void Internal_SetTableCompiler( red::UniquePtr< PackageTableCompiler > tableCompiler );
						
	#ifndef RED_CONFIGURATION_FINAL
		RED_REFLECTION_API static serialization::PathResolver* Internal_SetPathResolver( serialization::PathResolver* filter );
	#endif

	private:

		struct CompilationContext
		{
			Uint64 startOffset;
			Uint64 resourceOffset;
			Uint64 stringOffset;
			Uint64 objectOffset;
			Uint64 endOffset;
			red::UniqueBuffer result;
		};

		void CompileIntermediateTable();

		CompilationContext BeginCompilation( const PackageLayoutSaver & saver );
		void EndCompilation( CompilationContext & context );

		void CompileResourceTable( CompilationContext& context );
		void CompileStringTable( CompilationContext& context ) ;
		void CompileObjectTable( CompilationContext& context ) ;
		
		Package GeneratePackage( CompilationContext& context );

		PackageTable * m_inputTable;
		PackageTable m_table;
		red::UniquePtr< PackageTableCompiler > m_tableCompiler;
		red::UniqueBuffer m_propertyBuffer;
		red::UniquePtr< PackageWriteStream > m_outputStream;
		red::DynArray< red::StringView > m_nameTable;

		bool m_discardResourcePathString;
		bool m_discardNameString;

	#ifndef RED_CONFIGURATION_FINAL
		void Internal_ResolveResourceTable();
		res::ResourcePath Internal_ResolvePath( const res::ResourcePath path );
		static serialization::PathResolver* s_pathResolver;
	#endif
	};

	red::UniquePtr< PackageCompiler > CreatePackageCompiler( const PackageCompilerParameter & param );
}

