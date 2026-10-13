#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/resourceDepot.h"

#include "../../../common/localization/include/localizationString.h"

#include "../../../common/core/include/commandlet.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"

class LocalizationStringExtractorCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE(LocalizationStringExtractorCommandlet);

public:
	LocalizationStringExtractorCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
protected:
	LocalizationStringExtractorCommandlet( CName name );

private:
	bool IgnoreResource( const res::ResourcePath& path );
	void ProcessFile ( const res::ResourcePath& path );

	struct Settings
	{
		red::DynArray< res::ResourcePath > inputResources;
		red::AbsolutePath outputPath;

		Settings();
		bool Parse(const red::CommandLine& options);

	private:
		bool ParseInputFileList();
	};
	Settings m_settings;
	
	commandlet::EngineInstance m_engineInstance;
	red::UniqueBuffer m_buffer;
};


RTTI_BEGIN_TYPE( LocalizationStringExtractorCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

LocalizationStringExtractorCommandlet::LocalizationStringExtractorCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "LocalizationStringExtractorCommandlet" ) )
{ 
}

LocalizationStringExtractorCommandlet::LocalizationStringExtractorCommandlet(CName name)
	: ICommandlet(name)
{
}

void LocalizationStringExtractorCommandlet::PrintHelp() const
{
	RED_LOG("WCC: Usage:");
	RED_LOG("WCC:   LocalizationStringExtractor ");
	RED_LOG("WCC: Parameters:");
	RED_LOG("WCC:   -outputPath=<path>      - path to the file where you want the results");
}

const char* LocalizationStringExtractorCommandlet::GetOneLiner() const
{
	return "Extracts the content of the Localization String Attributes from every object in the project";
}

bool LocalizationStringExtractorCommandlet::Execute(const red::CommandLine& options)
{
	if (!m_settings.Parse(options))
	{
		return false;
	}

	red::DynArray< String > m_foundStrings;

	#ifdef RED_PLATFORM_WIN64
	loc::LocalizationString::OnStringConstructionSetCallback
	(
		[&](String s) -> void
		{
			for( const String & locString : m_foundStrings )
			{
				if ( s == locString )
					return;
			}
			m_foundStrings.PushBack( s );
		} 
	);
	#endif

	String fileContents;

	for (auto idx : m_settings.inputResources.Indices())
	{
		const auto& resource = m_settings.inputResources[idx];
		if (IsNotResource( resource ) || (IsGeneratedResource( resource )))
		{
			continue;
		}

		if (IgnoreResource( resource ))
		{
			continue;
		}
		
		m_foundStrings.Clear();
		ProcessFile( resource );

		if ( m_foundStrings.Empty() )
			continue;

		fileContents += "***";
		fileContents += resource.ToString();
		fileContents += "\r\n";

		for( const String & s : m_foundStrings )
		{
			fileContents += s;
			fileContents += "\r\n";
		}
	}
	return red::SaveStringToFile( m_settings.outputPath, fileContents );
}

bool LocalizationStringExtractorCommandlet::IgnoreResource( const res::ResourcePath& path )
{
	static const red::StringView notResourceExts[] = {
		"xbm", "mp4", "xlsm", "xlsx", "gidata", "w2mesh", "mesh", "mt", "remt", "envprobe"
		};

	const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	for ( const auto ext : notResourceExts )
	{
		if ( extension == ext )
		{
			return true;
		}
	}

	if (!path.ToString().BeginsWith("base"))
	{
		return true;
	}

	return false;
}

void LocalizationStringExtractorCommandlet::ProcessFile ( const res::ResourcePath& path )
{
	// Skip on post load
	res::LoadingOptions options;
	options.skipPostLoad = true;

	THandle< CResource > res = Cast< CResource >( GResourceLoader->IssueLoadingRequest( Convert( path, options ) )->WaitUntilLoaded() );
	if( !res )
	{
		RED_LOG_ERROR( "WCC: Cant load resource" );
		RED_LOG_ERROR( path.ToString().AsChar() );
		return;
	}
}

//////////////////////////////////////////////////////////////////////////

LocalizationStringExtractorCommandlet::Settings::Settings() = default;

bool LocalizationStringExtractorCommandlet::Settings::Parse(const red::CommandLine& options)
{
	String str;
	if (options.GetFirstParam( "outputPath", str ))
	{
		outputPath = red::AbsolutePath::ParseFilePath(str);
	}
	else
	{
		RED_LOG_ERROR("WCC: Require -outputPath to be specified");
		return false;
	}

	if ( !ParseInputFileList() )
	{
		return false;
	}

	return true;
}

bool LocalizationStringExtractorCommandlet::Settings::ParseInputFileList()
{
	inputResources = commandlet::ScanResourceDepotDirectories( *commandlet::GetEditorDepot(), "" );

	if ( inputResources.Size() == 0 )
	{
		RED_LOG_ERROR("WCC: No input files found there");
		return false;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////

Bool LocalizationStringExtractorCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void LocalizationStringExtractorCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}
