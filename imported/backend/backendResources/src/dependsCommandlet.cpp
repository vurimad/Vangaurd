/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redContainers/include/blob.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/jsonAllocator.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"

#include "dependencyMap.h"

#include "../../../../external/rapidjson/include/rapidjson/rapidjson.h"
#include "../../../../external/rapidjson/include/rapidjson/prettywriter.h"
#include "../../../../external/rapidjson/include/rapidjson/writer.h"
#include "../../../../external/rapidjson/include/rapidjson/filewritestream.h"

//------------------------------------------------------------------------------

using commandlet::DependencyEntry;
using commandlet::DependencyMap;

struct BadLevelEntry;
class OutputWriter;

class DependsCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( DependsCommandlet );

public:
	DependsCommandlet();
	~DependsCommandlet();

	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

private:
	struct Settings
	{
		red::DynArray< res::ResourcePath > m_resources;

		enum class OutputType
		{
			IMMEDIATE_DEPS_LIST,
			FULL_DEPS_LIST,
			FULL_DEPS_TREE,
			BAD_REFS_LIST,
			REVERSE_DEPENDENCY,
		} m_type;

		enum class OutputFormat
		{
			TEXT,
			COMPACT_JSON,
			PRETTY_JSON,
		} m_format;

		red::AbsolutePath m_outputFile;
	};

	bool Parse( const red::CommandLine& options, Settings& settings );

	red::DynArray< res::ResourcePath > FilterResourceList( const red::DynArray< res::ResourcePath >& paths ) const;

	Settings m_settings;
	FILE* m_stream;
	red::UniquePtr< OutputWriter > m_output;

	// The editor depot we are using (may not be needed)
	res::ResourceEditorDepot* m_editorDepot;

	DependencyMap m_dependencyMap;
};

RTTI_BEGIN_TYPE( DependsCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

DependsCommandlet::DependsCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "depends" ) )
	, m_editorDepot( commandlet::GetEditorDepot() )
	, m_dependencyMap( m_editorDepot )
{
}

DependsCommandlet::~DependsCommandlet()
{
}

const char* DependsCommandlet::GetOneLiner() const
{
	return "Depends";
}

void DependsCommandlet::PrintHelp() const
{
	// TODO print link to confluence here
}

//------------------------------------------------------------------------------

bool DependsCommandlet::Parse( const red::CommandLine& options, Settings& settings )
{
	settings.m_resources = commandlet::ParseResourcePathsFromCommandLine( *m_editorDepot, options, false );
	if ( settings.m_resources.Empty() )
	{
		RED_LOG_ERROR( "No resources specified, please use -all, -dir=<path>, or -file=<path>" );
		return false;
	}

	String param;
	settings.m_format = Settings::OutputFormat::TEXT;
	if ( options.GetFirstOption( "format", param ) )
	{
		if ( param == "json" )
		{
			if ( options.HasOption( "readable" ) )
			{
				settings.m_format = Settings::OutputFormat::PRETTY_JSON;
			}
			else
			{
				settings.m_format = Settings::OutputFormat::COMPACT_JSON;
			}
		}
	}

	if ( options.GetFirstOption( "output", param ) )
	{
		settings.m_outputFile = red::AbsolutePath::ParseFilePath( param );
	}

	settings.m_type = Settings::OutputType::IMMEDIATE_DEPS_LIST;
	if ( options.GetFirstOption( "info", param ) )
	{
		param.ToLower();
		if ( param == "tree" )
		{
			settings.m_type = Settings::OutputType::FULL_DEPS_TREE;
		}
		else if ( param == "full" )
		{
			settings.m_type = Settings::OutputType::FULL_DEPS_LIST;
		}
		else if ( param == "list" )
		{
			settings.m_type = Settings::OutputType::IMMEDIATE_DEPS_LIST;
		}
		else if ( param == "badrefs" )
		{
			settings.m_type = Settings::OutputType::BAD_REFS_LIST;
		}
		else if ( param == "reverse" )
		{
			settings.m_type = Settings::OutputType::REVERSE_DEPENDENCY;
		}
	}

	return true;
}

red::DynArray< res::ResourcePath > DependsCommandlet::FilterResourceList( const red::DynArray< res::ResourcePath >& paths ) const
{
	red::DynArray< res::ResourcePath > result;
	result.Reserve( paths.Size() );

	for ( const auto& path : paths )
	{
		if ( !IsNotResource( path ) )
		{
			result.PushBack( path );
		}
	}

	return result;
}

//------------------------------------------------------------------------------

class OutputWriter
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	OutputWriter( FILE* output, const DependencyMap& dependencyMap )
		: m_output( output )
		, m_dependencyMap( dependencyMap )
	{}

	virtual ~OutputWriter()
	{}

	virtual void Start() = 0;
	virtual void End() = 0;

	virtual void OutputDependencyList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries ) = 0;
	virtual void OutputBadRefsList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries ) = 0;
	virtual void OutputDependencyTree( const res::ResourcePath& path ) = 0;

protected:
	FILE* m_output;

	const DependencyMap& m_dependencyMap;
};


void SortDependencyEntryListByPath( red::DynArray<const DependencyEntry*>& paths )
{
	std::sort( paths.Begin(), paths.End(), 
		[]( const DependencyEntry* left, const DependencyEntry* right ) -> bool
	{
		return left->Path().ToStringView() < right->Path().ToStringView();
	} );
}

//------------------------------------------------------------------------------

namespace helper
{

class Indent
{
public:
	explicit Indent( const char* level )
		: m_level( level )
	{}

	void Push()
	{
		m_indent += m_level;
	}

	void Pop()
	{
		m_indent.Resize( m_indent.Length() - m_level.Length() );
	}

	const char* AsChar()
	{
		return m_indent.AsChar();
	}

private:
	String m_indent;
	String m_level;
};

} // helper

class TextOutputWriter : public OutputWriter
{
public:
	TextOutputWriter( FILE* output, const DependencyMap& dependencyMap )
		: OutputWriter( output, dependencyMap )
		, m_indent( "\t" )
	{}

	virtual void Start() override
	{}

	virtual void End() override
	{}

	virtual void OutputDependencyList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries ) override;
	virtual void OutputBadRefsList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries ) override;
	virtual void OutputDependencyTree( const res::ResourcePath& path ) override;

private:
	void PrintEntry( const DependencyEntry* entry );
	void PrintNotOKChildEntry( const res::ResourcePath& path );

	helper::Indent m_indent;
};

void TextOutputWriter::PrintEntry( const DependencyEntry* entry )
{
	fprintf( m_output, m_indent.AsChar() );
	if ( ! entry->IsOK() )
	{
		const char* errorText = entry->GetStatusMessage();
		fprintf( m_output, "(%hs) ", errorText );
	}
	fprintf( m_output, "%hs", entry->Path().ToDebugString() );
	if ( entry->IsLink() )
	{
		fprintf( m_output, " (.link)" );
	}
	fprintf( m_output, "\n" );
}

void TextOutputWriter::PrintNotOKChildEntry( const res::ResourcePath& path )
{
	fprintf( m_output, "%hs(Parent error) %hs\n", m_indent.AsChar(), path.ToDebugString() );
}

void TextOutputWriter::OutputDependencyList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries )
{
	PrintEntry( entry );
	m_indent.Push();
	for ( const DependencyEntry* depEntry : entries )
	{
		PrintEntry( depEntry );
	}
	m_indent.Pop();
}

void TextOutputWriter::OutputBadRefsList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries )
{
	OutputDependencyList( entry, entries );
}

void TextOutputWriter::OutputDependencyTree( const res::ResourcePath& path )
{
	const auto* entry = m_dependencyMap.Get( path );
	PrintEntry( entry );
	m_indent.Push();
	if ( entry->IsOK() )
	{
		for ( const auto& depPath : entry->Dependencies() )
		{
			OutputDependencyTree( depPath );
		}
	}
	else
	{
		for ( const auto& depPath : entry->Dependencies() )
		{
			PrintNotOKChildEntry( depPath );
		}

	}
	m_indent.Pop();
}

//------------------------------------------------------------------------------

template < typename Writer >
class JsonOutputWriter : public OutputWriter
{
public:
	JsonOutputWriter( FILE* output, const DependencyMap& dependencyMap )
		: OutputWriter( output, dependencyMap )
		, m_fileBuffer( static_cast<Uint32>( 64_KB ) )
		, m_stream( output, m_fileBuffer.Pointer<char>(), m_fileBuffer.Size() )
		, m_writer( m_stream )
	{}

	virtual void Start() override;
	virtual void End() override;

	virtual void OutputDependencyList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries ) override;
	virtual void OutputBadRefsList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries ) override;
	virtual void OutputDependencyTree( const res::ResourcePath& path ) override;

protected:
	bool WriteResourcePath( const res::ResourcePath& path );

	red::Blob m_fileBuffer;
	rapidjson::FileWriteStream m_stream;
	Writer m_writer;
};

template < typename Writer >
bool JsonOutputWriter<Writer>::WriteResourcePath( const res::ResourcePath& path )
{
	const red::StringView name = path.ToStringView();
	return m_writer.String( name.Data(), name.Length() );
}

template < typename Writer >
void JsonOutputWriter<Writer>::Start()
{
	m_writer.StartObject();
	m_writer.Key( "resources" );

	m_writer.StartArray();
}

template < typename Writer >
void JsonOutputWriter<Writer>::End()
{
	m_writer.EndArray();
	m_writer.EndObject();
}

template < typename Writer >
void JsonOutputWriter<Writer>::OutputDependencyList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries )
{
	m_writer.StartObject();
	m_writer.Key( "resource" );
	WriteResourcePath( entry->Path() );

	if ( !entries.Empty() )
	{
		m_writer.Key( "dependencies" );
		m_writer.StartArray();
		for ( const auto* depEntry : entries )
		{
			WriteResourcePath( depEntry->Path() );
		}
		m_writer.EndArray();
	}

	m_writer.EndObject();
}

template < typename Writer >
void JsonOutputWriter<Writer>::OutputBadRefsList( const DependencyEntry* entry, red::ArraySpan<const DependencyEntry*> entries )
{
	m_writer.StartObject();
	m_writer.Key( "resource" );
	WriteResourcePath( entry->Path() );

	red::DynArray<const DependencyEntry*> notFoundEntries;
	red::DynArray<const DependencyEntry*> notResourceEntries;
	red::DynArray<const DependencyEntry*> loadFailEntries;
	red::DynArray<const DependencyEntry*> badLinkEntries;
	for ( const auto* e : entries )
	{
		auto status = e->GetStatus();
		if ( status == DependencyEntry::Status::FILE_NOT_FOUND )
		{
			notFoundEntries.PushBack( e );
		}
		else if ( status == DependencyEntry::Status::NOT_A_RESOURCE )
		{
			notResourceEntries.PushBack( e );
		}
		else if ( status != DependencyEntry::Status::OK )
		{
			loadFailEntries.PushBack( e );
		}
		else
		{
			badLinkEntries.PushBack( e );
		}
	}

	if ( !notFoundEntries.Empty() )
	{
		m_writer.Key( "File not found" );
		m_writer.StartArray();
		for ( const auto* e : notFoundEntries )
		{
			WriteResourcePath( e->Path() );
		}
		m_writer.EndArray();
	}

	if ( !notResourceEntries.Empty() )
	{
		m_writer.Key( "Not a resource" );
		m_writer.StartArray();
		for ( const auto* e : notResourceEntries )
		{
			WriteResourcePath( e->Path() );
		}
		m_writer.EndArray();
	}

	if ( !loadFailEntries.Empty() )
	{
		m_writer.Key( "Load failed" );
		m_writer.StartArray();
		for ( const auto* e : loadFailEntries )
		{
			WriteResourcePath( e->Path() );
		}
		m_writer.EndArray();
	}

	if ( !badLinkEntries.Empty() )
	{
		m_writer.Key( "Bad link" );
		m_writer.StartArray();
		for ( const auto* e : badLinkEntries )
		{
			WriteResourcePath( e->Path() );
		}
		m_writer.EndArray();
	}

	m_writer.EndObject();
}

template < typename Writer >
void JsonOutputWriter<Writer>::OutputDependencyTree( const res::ResourcePath& path )
{
	const auto* entry = m_dependencyMap.Get( path );

	m_writer.StartObject();
	m_writer.Key( "resource" );
	WriteResourcePath( path );

	const auto dependencies = entry->Dependencies();
	if ( !dependencies.Empty() )
	{
		m_writer.Key( "dependencies" );
		m_writer.StartArray();
		if ( entry->IsOK() )
		{
			for ( const auto& depPath : dependencies )
			{
				OutputDependencyTree( depPath );
			}
		}
		else
		{
			for ( const auto& depPath : dependencies )
			{
				WriteResourcePath( depPath );
			}
		}
		m_writer.EndArray();
	}

	m_writer.EndObject();
}

using CompactJsonOutputWriter = JsonOutputWriter< rapidjson::Writer< rapidjson::FileWriteStream, rapidjson::UTF8<>, rapidjson::UTF8<>, res::json::JSONAllocator > >;

class PrettyJsonOutputWriter : public JsonOutputWriter< rapidjson::PrettyWriter< rapidjson::FileWriteStream, rapidjson::UTF8<>, rapidjson::UTF8<>, res::json::JSONAllocator > >
{
public:
	PrettyJsonOutputWriter( FILE* output, const DependencyMap& dependencyMap )
		: JsonOutputWriter( output, dependencyMap )
	{
		m_writer.SetIndent( '\t', 1 );
	}
};

//------------------------------------------------------------------------------

bool DependsCommandlet::Execute( const red::CommandLine& options )
{
	if ( !Parse( options, m_settings ) )
	{
		return false;
	}

	// Setup output
	m_stream = stdout;
	if ( !m_settings.m_outputFile.Empty() )
	{
		FILE* stream = fopen( m_settings.m_outputFile.AsChar(), "w" );
		if ( stream != nullptr )
		{
			m_stream = stream;
		}
	}

	// Compute and filter the input resources
	red::DynArray< res::ResourcePath > resourcePaths;
	if ( m_settings.m_type == Settings::OutputType::REVERSE_DEPENDENCY )
	{
		// For this we want to scan the whole depot regardless
		auto allResources = commandlet::ScanResourceDepotDirectories( *m_editorDepot, "" );
		resourcePaths = FilterResourceList( allResources );
		m_settings.m_resources = FilterResourceList( m_settings.m_resources );
	}
	else
	{
		// Take the settings resources as the input list 
		resourcePaths = FilterResourceList( m_settings.m_resources );
	}

	// Scan the files
	for ( const auto& path : resourcePaths )
	{
		m_dependencyMap.ScanResourceRecursive( path );
	}
	// At this moment the dependency map should be read only
	m_dependencyMap.PrepareRecursiveDependencies();

	if ( m_settings.m_format == Settings::OutputFormat::COMPACT_JSON )
	{
		m_output = red::CreateUniquePtr< CompactJsonOutputWriter >( m_stream, m_dependencyMap );
	}
	else if ( m_settings.m_format == Settings::OutputFormat::PRETTY_JSON )
	{
		m_output = red::CreateUniquePtr< PrettyJsonOutputWriter >( m_stream, m_dependencyMap );
	}
	else // Settings::OutputFormat::TEXT
	{
		m_output = red::CreateUniquePtr< TextOutputWriter >( m_stream, m_dependencyMap );
	}

	// Processing and Output
	m_output->Start();
	if ( m_settings.m_type == Settings::OutputType::IMMEDIATE_DEPS_LIST )
	{
		for ( const auto& path : resourcePaths )
		{
			// Print out a single level of resource references for each resource
			auto entry = m_dependencyMap.Get( path );
			auto depEntries = m_dependencyMap.Get( entry->Dependencies() );
			SortDependencyEntryListByPath( depEntries );
			m_output->OutputDependencyList( entry, depEntries );
		}
	}
	else if ( m_settings.m_type == Settings::OutputType::FULL_DEPS_LIST )
	{
		for ( const auto& path : resourcePaths )
		{
			// Gather and print out a recursive list of all dependencies for each resource
			auto entry = m_dependencyMap.Get( path );
			auto depPaths = m_dependencyMap.CollectRecursive( path );
			auto depEntries = m_dependencyMap.Get( depPaths.Elements() );
			SortDependencyEntryListByPath( depEntries );
			m_output->OutputDependencyList( entry, depEntries );
		}
	}
	else if ( m_settings.m_type == Settings::OutputType::FULL_DEPS_TREE )
	{
		for ( const auto& path : resourcePaths )
		{
			m_output->OutputDependencyTree( path );
		}
	}
	else if ( m_settings.m_type == Settings::OutputType::BAD_REFS_LIST )
	{
		// Collate and print out all of the bad references for each resource and all its dependencies
		red::Set<res::ResourcePath> allPaths;
		for ( const auto& path : resourcePaths )
		{
			allPaths.Union( m_dependencyMap.CollectRecursive( path ) );
		}

		for ( const auto& path : allPaths.Elements() )
		{
			auto badRefPaths = m_dependencyMap.DetectBadLevelReferences( path );
			if ( !badRefPaths.Empty() )
			{
				auto badEntries = m_dependencyMap.Get( badRefPaths );
				SortDependencyEntryListByPath( badEntries );
				m_output->OutputBadRefsList( m_dependencyMap.Get( path ), badEntries );
			}
		}
	}
	else if ( m_settings.m_type == Settings::OutputType::REVERSE_DEPENDENCY )
	{
		for ( const auto& path : m_settings.m_resources )
		{
			auto reversePaths = m_dependencyMap.DetectReverseReferences( path );
			auto entry = m_dependencyMap.Get( path );
			auto reverseEntries = m_dependencyMap.Get( reversePaths );
			SortDependencyEntryListByPath( reverseEntries );
			m_output->OutputDependencyList( entry, reverseEntries );
		}
	}
	m_output->End();

	fflush( m_stream );

	if ( m_stream != stdout )
	{
		fclose( m_stream );
	}

	return true;
}
