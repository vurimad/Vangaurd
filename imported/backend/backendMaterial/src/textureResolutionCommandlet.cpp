/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"

enum ComparisonRule
{
	LessThan,
	Equal,
	GreaterThan
};

struct TextureData
{
	Bool m_isMarked{ false };
	Uint32 m_width{};
	Uint32 m_height{};
};

class TextureResolutionCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( TextureResolutionCommandlet );

public:
	TextureResolutionCommandlet()
		: ICommandlet( RED_NAME_CONSTEXPR( "query-textures" ) )
	{
	}

	~TextureResolutionCommandlet() = default;

	// ICommandlet interface
	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual void PrintHelp() const override;
	virtual const char* GetOneLiner() const override { return ""; }
	virtual bool Execute( const red::CommandLine& options ) override;

	Bool MatchesCriteria( const THandle< CBitmapTexture >& texture );

private:
	Bool ParseParams( const red::CommandLine& options );

	commandlet::EngineInstance m_engineInstance;

	String m_outputFilePathStr;
	red::AbsolutePath m_outputFilePath;

	ComparisonRule m_verticalRule{ GreaterThan };
	Uint32 m_verticalValue{ 0 };

	ComparisonRule m_horizontalRule{ GreaterThan };
	Uint32 m_horizontalValue{ 0 };
};


RTTI_BEGIN_TYPE( TextureResolutionCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

Bool TextureResolutionCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void TextureResolutionCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

void TextureResolutionCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( "  query-textures <input options> [-output=<file>]" );
	RED_LOG( "Parameters:" );
	RED_LOG( "Input options:" );
	RED_LOG( "  -all								- process all resources" );
	RED_LOG( "  -file=<file1> <file2>				- list of files to be processed" );
	RED_LOG( "  -dir=<dir1> <dir2>					- directories to scan" );
	RED_LOG( "  -filelist=<filelist>				- file that contains list of files to be processed" );
	RED_LOG( "Query options:" );
	RED_LOG( "  -width=[rule]<value>				- e.g. -width=1024, -width=\"<1024\", -width=\">1024\"" );
	RED_LOG( "  -height=[rule]<value>				- the same as above" );
	RED_LOG( "  -output=<file>						- place to store a list of files that have been processed" );
}

void ParseSizeParam( String& param, ComparisonRule& outRule, Uint32& outValue )
{
	if ( param.BeginsWith( "<" ) )
	{
		outRule = LessThan;
		param = param.StringAfter( "<" );
	}
	else if ( param.BeginsWith( ">" ) )
	{
		outRule = GreaterThan;
		param = param.StringAfter( ">" );
	}
	else
	{
		outRule = Equal;
	}

	outValue = FromStringDirect< Uint32 >( param );
}

Bool TextureResolutionCommandlet::ParseParams( const red::CommandLine& options )
{
	if ( !options.GetFirstParam( "output", m_outputFilePathStr ) )
	{
		return false;
	}

	String param;
	if ( options.GetFirstParam( "width", param ) )
	{
		ParseSizeParam( param, m_verticalRule, m_verticalValue );
	}

	if ( options.GetFirstParam( "height", param ) )
	{
		ParseSizeParam( param, m_horizontalRule, m_horizontalValue );
	}

	return true;
}

bool TextureResolutionCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseParams( options ) )
	{
		return false;
	}

	const red::DynArray< String > patterns{ "*.xbm" };

	RED_LOG_INFO( "TextureResolutionCommandlet: Gathering list of resources (.xbm) to process.." );
	red::DynArray< res::ResourcePath > paths = commandlet::ParseResourcePathsFromCommandLine( *commandlet::GetEditorDepot(), options, patterns );

	if ( paths.Empty() )
	{
		RED_LOG_ERROR( "TextureResolutionCommandlet: No resources found" );
		return true;
	}

	RED_LOG_INFO( "TextureResolutionCommandlet: Sorting %u resources before processing", paths.Size() );

	std::sort( paths.Begin(), paths.End(), res::SortByPathString );

	m_outputFilePath = red::AbsolutePath::ParseFilePath( m_outputFilePathStr.AsChar() );
	red::SaveStringToFile( m_outputFilePath, "" );

	red::DynArray< TextureData > collectedData;
	collectedData.Resize( paths.Size() );

	red::Atomic< Uint32 > toGo = paths.Size();

	job::Builder builder;
	builder.DispatchParallelForJob( "TextureResolutionCommandlet/ProcessPaths", [ &paths ]() { return paths.Size(); },
		[ this, &paths, &collectedData, &toGo ]( Uint32 index, const job::RunContext& runContext )
	{
		const res::ResourcePath& path = paths[ index ];
		TextureData& data = collectedData[ index ];

		res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( path );

		job::Builder depBuilder( runContext );
		depBuilder.DispatchWait( token->GetWaitCounter() );

		depBuilder.DispatchJob( "TextureResolutionCommandlet/ProcessPaths/ResourceLoaded", [ this, token, &data, &toGo ]( const job::RunContext& )
		{
			if ( !token->IsLoaded() )
			{
				return;
			}

			THandle< CBitmapTexture > texture = Cast< CBitmapTexture >( token->GetResource() );
			RED_ASSERT( texture );

			if ( MatchesCriteria( texture ) )
			{
				data.m_isMarked = true;
				data.m_width = texture->GetSizeDesc().width;
				data.m_height = texture->GetSizeDesc().height;
			}

			texture->ReleaseRenderResource();

			Uint32 value = toGo.PostDecrement();
			if ( value % 100 == 0 )
			{
				RED_LOG_INFO( "%d textures left", value );
			}
		} );
	} );

	job::FlushCounter( builder.ExtractWaitCounter() );

	red::StringBuilder<String> stringBuilder;
	stringBuilder.Append( "Path;Width;Height" );
	for ( Uint32 index : paths.Indices() )
	{
		if ( collectedData[ index ].m_isMarked )
		{
			stringBuilder.Appendf( "%hs;%d;%d\n", paths[ index ].ToDebugString(), collectedData[ index ].m_width, collectedData[ index ].m_height );
		}
	}

	red::SaveStringToFile( m_outputFilePath, stringBuilder.ToString() );

	return true;
}

Bool CheckValue( ComparisonRule rule, Uint32 testValue, Uint32 value )
{
	switch ( rule )
	{
	case LessThan: return testValue < value;
	case Equal: return testValue == value;
	case GreaterThan: return testValue > value;
	}

	return false;
}

Bool TextureResolutionCommandlet::MatchesCriteria( const THandle< CBitmapTexture >& texture )
{
	Bool result = true;
	ITexture::SizeDesc sizeDesc = texture->GetSizeDesc();

	result &= CheckValue( m_verticalRule, sizeDesc.width, m_verticalValue );
	result &= CheckValue( m_horizontalRule, sizeDesc.height, m_horizontalValue );

	return result;
}
