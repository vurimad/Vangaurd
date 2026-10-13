/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"
#include "resourceGameDepot.h"

#include "resourceDepotUtils.h"
#include "serializationAsyncSourceImpl.h"
#include "../../redReflection/include/serializationAsyncSource.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redMemory/include/sharedPtr.h"
#include "../../archive/include/instance.h"
#include "../../redContainers/include/string/stringUtils.h"
#include "../../redSystem/include/stopWatch.h"
#include "../../redReflection/include/version.h"
#include "../../redCore/include/commandline.h"
#include "../../redCore/include/languageRegion.h"
#include "../../redIO/include/redIOAsyncIO.h"
#include "../../redMemory/include/reportUtils.h"

#include "archiveLanguageMap.h"

#if defined( RED_PLATFORM_ORBIS )
#include "archiveHelperOrbis.h"
#elif defined( RED_PLATFORM_DURANGO )
#include "archiveHelperDurango.h"
#endif

namespace res
{

//------------------------------------------------------------------------------

ResourceGameDepot::ArchiveInfo::ArchiveInfo()
	: asyncHandle( io::INVALID_FILE_HANDLE )
{
}

ResourceGameDepot::ArchiveInfo::~ArchiveInfo()
{
	if ( asyncHandle != io::INVALID_FILE_HANDLE )
	{
		io::GAsyncIO.ReleaseFile( asyncHandle );
		asyncHandle = io::INVALID_FILE_HANDLE;
	}
}

ResourceGameDepot::ArchiveInfo::ArchiveInfo( ArchiveInfo&& other )
	: instance( std::move( other.instance ) )
	, reader( std::move( other.reader ) )
	, asyncHandle( other.asyncHandle )
	, archivePath( std::move( other.archivePath ) )
	, optionalMemoryResidentArchive( std::move( other.optionalMemoryResidentArchive ) )
{
	other.asyncHandle = io::INVALID_FILE_HANDLE;
}

ResourceGameDepot::ArchiveInfo& ResourceGameDepot::ArchiveInfo::operator=( ArchiveInfo&& other )
{
	if ( this != &other )
	{
		instance = std::move( other.instance );
		reader = std::move( other.reader );
		asyncHandle = other.asyncHandle;
		other.asyncHandle = io::INVALID_FILE_HANDLE;
		archivePath = std::move( other.archivePath );
		optionalMemoryResidentArchive = std::move(other.optionalMemoryResidentArchive);
	}
	return *this;
}

//------------------------------------------------------------------------------

ResourceGameDepot::ArchiveSet::ArchiveSet( ArchiveCategory archiveCategory, const red::AbsolutePath& baseSetPath )
	: archives{ red::PoolEngine() }
	, basePath( baseSetPath )
	, category( archiveCategory )
{
}

ResourceGameDepot::ArchiveSet::~ArchiveSet()
{
}

ResourceGameDepot::ArchiveSet::ArchiveSet( ArchiveSet&& other )
	: archives( std::move( other.archives ) )
	, basePath( std::move( other.basePath ) )
	, category( other.category )
{
	other.category = ArchiveCategory::None;
}

ResourceGameDepot::ArchiveSet& ResourceGameDepot::ArchiveSet::operator=( ArchiveSet&& other )
{
	if ( this != &other )
	{
		archives = std::move( other.archives );
		basePath = std::move( other.basePath );
		category = other.category;
		other.category = ArchiveCategory::None;
	}
	return *this;
}

//------------------------------------------------------------------------------

ResourceGameDepot::ResourceGameDepot( const red::AbsolutePath& dataRootPath )
	: m_archives{ red::PoolEngine() }
	, m_installedLanguageArchiveNames{ red::PoolEngine() }
	, m_dataRootPath( dataRootPath )
	, m_modded( false )
{
	OpenArchiveSets();
	ALWAYSENABLED_RED_FATAL_ASSERT( !m_archives.Empty(), "Failed to find any archives in '%hs'", m_dataRootPath.AsChar() );
}

ResourceGameDepot::~ResourceGameDepot() = default;

const char* ResourceGameDepot::GetDebugName() const
{
	return "Game";
}

Uint32 ResourceGameDepot::Debug_GetResourceTotalSizeOnDisk( const res::ResourcePath resourcePath ) const
{
#ifndef RED_READONLY_ARCHIVES
	archive::FileMetadata metadata;
	if ( FindFileMetadata( resourcePath, metadata ) )
	{
		return metadata.GetTotalSizeOnDisk();
	}
#endif
	return 0;
}

Uint32 ResourceGameDepot::Debug_GetResourceObjectSizeOnDisk(const res::ResourcePath resourcePath) const
{
#ifndef RED_READONLY_ARCHIVES
	archive::FileMetadata metadata;
	if (FindFileMetadata(resourcePath, metadata))
	{
		return metadata.GetFirstSegmentSizeOnDisk();
	}
#endif
	return 0;
}

Bool ResourceGameDepot::IsLanguageVoicePackAvailable( const CName languageCodeName ) const
{
#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
	const auto langID = res::ToLanguageID( languageCodeName.AsChar() );
	return m_archiveLanguageMap.IsVoiceLanguageAvailable( langID );
#else
	return true;
#endif
}

Bool ResourceGameDepot::IsLanguageVoicePackInstalled( const CName languageCodeName ) const
{
#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
	const auto langID = res::ToLanguageID( languageCodeName.AsChar() );
	return m_archiveLanguageMap.IsVoiceLanguageInstalled( langID );
#else
	return true;
#endif
}

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
Uint16 ResourceGameDepot::GetChunkForVoicePack( res::LanguageID languageID )
{
	return m_archiveLanguageMap.GetChunkForVoicePack( languageID );
}

LanguagePackStatus ResourceGameDepot::GetVoicePackStatus( res::LanguageID languageID )
{
	return m_archiveLanguageMap.GetVoicePackStatus( languageID );
}

void ResourceGameDepot::ChangeChunksStatus( const red::DynArray< Uint16 > &chunks, const LanguagePackStatus status )
{
	const red::DynArray< red::String > newFiles = m_archiveLanguageMap.ChangeChunksStatus( chunks, status );

	if ( status != LanguagePackStatus::Installed || newFiles.Empty() )
	{
		return;
	}

	const red::AbsolutePath& dataRootPath = m_dataRootPath;
	const red::AbsolutePath basePath = prv::MakeContentArchivesPath( dataRootPath );
	const red::AbsolutePath dlcPath = prv::MakeDLCArchivesPath( dataRootPath );
	const red::AbsolutePath patchPath = prv::MakePatchArchivesPath( dataRootPath );

	const red::DynArray< red::String > currentLangArchives = m_installedLanguageArchiveNames;
	m_installedLanguageArchiveNames = newFiles;

	ArchiveSet *baseSet{};
	ArchiveSet *dlcSet{};
	ArchiveSet *patchSet{};

	for ( auto &set : m_archives )
	{
		if ( set.category == ArchiveCategory::Content )
		{
			baseSet = &set;
		}
		else if ( set.category == ArchiveCategory::DLC )
		{
			dlcSet = &set;
		}
		else if ( set.category == ArchiveCategory::Patch )
		{
			patchSet = &set;
		}
	}

	auto langBaseArchives = FindLanguageArchives( basePath );
	auto langDLCArchives = FindLanguageArchives( dlcPath );
	auto langPatchArchives = FindLanguageArchives( patchPath );

	m_installedLanguageArchiveNames.PushBack( currentLangArchives );
	red::alg::RemoveDuplicates( m_installedLanguageArchiveNames );

	red::DynArray< res::ResourcePath > existingResources{ red::PoolEngine() };
	OpenArchives( *baseSet, langBaseArchives, existingResources );

	if ( dlcSet )
	{
		OpenArchives( *dlcSet, langDLCArchives, existingResources );
	}

	if ( patchSet )
	{
		OpenArchives( *patchSet, langPatchArchives, existingResources );
	}
}

Bool ResourceGameDepot::HaveArchiveMap() const
{
	return m_archiveLanguageMap.IsLoaded();
}

Bool ResourceGameDepot::IsLanguageChunkID( const Uint16 chunkID ) const
{
	return m_archiveLanguageMap.IsLanguageChunkID( chunkID );
}

prv::ArchiveLanguageMap* ResourceGameDepot::GetLanguageMap()
{
	return m_archiveLanguageMap.IsLoaded() ? &m_archiveLanguageMap : nullptr;
}

LanguageID ResourceGameDepot::GetSystemLanguageID() const
{
	return m_archiveLanguageMap.GetSystemLanguageID();
}

#endif

red::SharedPtr<serialization::IAsyncSource> ResourceGameDepot::CreateResourceAsyncSource( const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params )
{
	red::SharedPtr<serialization::IAsyncSource> result;

	const archive::FileID id{ resourcePath.GetHash() };

	io::TFileHandle asyncHandle{ io::INVALID_FILE_HANDLE };
	red::BlobView archiveBlobView;
	archive::FileMetadata metadata;
	const archive::FileTable* table = nullptr;
	const auto& ioHint = params.ioHint;

	for ( const auto& archiveSet : m_archives )
	{
		for ( auto& info : archiveSet.archives )
		{
			const Bool isIdealFile = !info.optionalMemoryResidentArchive.Empty() || (ioHint.fileHandle == io::INVALID_FILE_HANDLE) || (ioHint.fileHandle == info.asyncHandle);
			const Uint64 offset = isIdealFile ? ioHint.offset : 0;
			archive::FileMetadata tmpMetadata;
			if (info.instance->FindFileClosestToOffset( id, offset, tmpMetadata ))
			{
				asyncHandle = info.asyncHandle;
				archiveBlobView = info.optionalMemoryResidentArchive;
				metadata = std::move(tmpMetadata);
				table = &info.instance->GetFileTable();
				if (isIdealFile)
				{
					break;
				}
			}
		}

		if (metadata.IsValid())
		{
			break;
		}
	}

	//RED_LOG_TRACE( "GameResourceDepot: Loaded %hs from %hs archive in %hs set", resourcePath.ToDebugString(), info.archivePath.AsChar(), res::prv::GetArchiveCategoryPathString( archiveSet.category ) );
	if (metadata.IsValid() && table != nullptr)
	{
		RED_FATAL_ASSERT(asyncHandle != io::INVALID_FILE_HANDLE);
		result = serialization::CreateArchiveAsyncSource(archiveBlobView, asyncHandle, metadata, *table);
	}
	return result;
}

bool ResourceGameDepot::ResourceExists( const res::ResourcePath resourcePath ) const
{
	archive::FileMetadata metadata;
	return FindFileMetadata( resourcePath, metadata );
}

bool ResourceGameDepot::ResourceIsReadOnly( const res::ResourcePath resourcePath ) const
{
	return true;
}

Uint64 ResourceGameDepot::GetResourceTimestamp( const res::ResourcePath resourcePath ) const
{
	archive::FileMetadata metadata;
	if ( FindFileMetadata( resourcePath, metadata ) )
	{
		return metadata.GetTimestamp();
	}
	return 0ull;
}

res::ResourcePath ResourceGameDepot::ResolveResourcePath( const res::ResourcePath resourcePath ) const
{
	return resourcePath;
}

void ResourceGameDepot::ResolveResourcePathBulk(const red::ArraySpan<res::ResourcePath>& inOutResourcePaths) const
{
	// Nothing to do
}

//------------------------------------------------------------------------------

// Finds archives in a given directory with a given prefix
static red::DynArray< red::AbsolutePath > FindArchives( const red::AbsolutePath& basePath, const red::String& prefix )
{
	red::DynArray< red::AbsolutePath > archivePaths{ red::PoolEngine() };
#if defined( RED_PLATFORM_ORBIS )
	// On Orbis just find all archives and filter them out after
	red::String pattern = "*.archive";
	GFileManager->FindFiles( basePath, pattern, archivePaths, false );
	red::alg::EraseIf( archivePaths, [&prefix]( const red::AbsolutePath& path ) -> bool
	{
		return ! red::paths::GetFileName( path ).StartsWith( prefix );
	} );
#else
	// Windows API can handle patterns of file iteration
	red::String pattern = prefix + "*.archive";
	GFileManager->FindFiles( basePath, pattern, archivePaths, false );
#endif
	std::sort( archivePaths.Begin(), archivePaths.End() );
	return archivePaths;
}

// Finds patch archives in a given directory with a specific prefix
// Paths archives must have the same name as a content archive
static red::DynArray< red::AbsolutePath > FindPatchArchives( const red::AbsolutePath& patchPath, const red::AbsolutePath& contentPath, const red::String& prefix )
{
	auto contentArchivePaths = FindArchives( contentPath, prefix );
	auto IsNotInContentPaths = [&contentArchivePaths]( const red::AbsolutePath& patchPath ) -> bool
	{
		// Simple linear search as we should only have a dozen or so entries in each array.
		auto contentIter = std::find_if( contentArchivePaths.Begin(), contentArchivePaths.End(), [patchFilename = red::paths::GetFileName( patchPath )]( const red::AbsolutePath& contentPath ) -> bool
		{
			const auto contentFilename = red::paths::GetFileName( contentPath );
			return contentFilename == patchFilename;
		} );
		return contentIter == contentArchivePaths.End();
	};

	auto patchArchivePaths = FindArchives( patchPath, prefix );
	red::alg::EraseIf( patchArchivePaths, IsNotInContentPaths );

	return patchArchivePaths;
}

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )

static red::DynArray< red::String > GetLanguageArchiveNamesToLoad( prv::ArchiveLanguageMap &languageMap )
{
	red::DynArray< red::String > result{ red::PoolEngine() };

	languageMap.RefreshStatus();

	auto installedLanguages = prv::GetInstalledLanguages( languageMap );
	if ( installedLanguages.Empty() )
	{
		// Couldn't find any installed languages therefore either this is a fatal condition or
		// we are in some development mode where the package hasn't been installed.
		return result;
	}

	std::sort( installedLanguages.Begin(), installedLanguages.End() );

	RED_LOG( "Current user language: %hs", red::GetDefaultLanguageCountryCode().AsChar() );
	RED_LOG( "Installed languages: %hs", red::StrJoin( installedLanguages, ", " ).AsChar() );

	result = languageMap.GetFileNamesForLanguages( installedLanguages );

	RED_LOG( "Language archives to load: %hs", red::StrJoin( result, ", " ).AsChar() );

	return result;
}

#endif

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
red::DynArray< red::AbsolutePath > ResourceGameDepot::FindLanguageArchives( const red::AbsolutePath& basePath ) const
{
	if ( m_installedLanguageArchiveNames.Empty() )
	{
		// If we didn't manage to find any installed languages then just assume we're in some
		// development mode so return the old default set of archives
		RED_LOG_ERROR( "Archives: Couldn't find any installed languages so just trying to load all language archives" );
		return FindArchives( basePath, "lang_" );
	}

	red::DynArray< red::AbsolutePath > result{ red::PoolEngine() };
	result.Reserve( m_installedLanguageArchiveNames.Size() );
	for ( const auto& langArchiveName : m_installedLanguageArchiveNames )
	{
		result.PushBack( basePath.AddFilePath( langArchiveName ) );
	}
	RED_ASSERT( std::is_sorted( result.Begin(), result.End() ) );

	// Remove archives which do not exist on disk
	auto allLanguageArchives = FindArchives( basePath, "lang_" );
	std::sort( allLanguageArchives.Begin(), allLanguageArchives.End() );
	auto iter = red::alg::KeepIfIn( result.Begin(), result.End(), allLanguageArchives.Begin(), allLanguageArchives.End() );
	result.Remove( iter, result.End() );

	return result;
}
#endif

void ResourceGameDepot::OpenContentArchives( ArchiveSet& archiveSet, const red::AbsolutePath& baseContentPath, red::DynArray< res::ResourcePath >& existingResources )
{
	// #TBD: could close the file handle after this, but should make sure nothing will break
	auto memoryResidentArchives = FindArchives( baseContentPath, "memoryresident_" );
	OpenArchives( archiveSet, memoryResidentArchives, existingResources );

	auto basegameArchives = FindArchives( baseContentPath, "basegame_" );
	OpenArchives( archiveSet, basegameArchives, existingResources );

	auto audioArchives = FindArchives( baseContentPath, "audio_" );
	OpenArchives( archiveSet, audioArchives, existingResources );

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
	auto langArchives = FindLanguageArchives( baseContentPath );
#else
	auto langArchives = FindArchives( baseContentPath, "lang_" );
#endif
	OpenArchives( archiveSet, langArchives, existingResources );

#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
	auto nonproductionArchives = FindArchives( baseContentPath, "non_production" );
	OpenArchives( archiveSet, nonproductionArchives, existingResources );

	auto quickLaunchArchives = FindArchives( baseContentPath, "quicklaunch" );
	OpenArchives( archiveSet, quickLaunchArchives, existingResources );

	auto multiplayerArchives = FindArchives( baseContentPath, "multiplayer" );
	OpenArchives( archiveSet, multiplayerArchives, existingResources );
#endif
}

void ResourceGameDepot::OpenPatchArchives( ArchiveSet& archiveSet, const red::AbsolutePath& basePatchPath, const red::AbsolutePath& baseContentPath, red::DynArray< res::ResourcePath >& existingResources )
{
	auto basegameArchives = FindPatchArchives( basePatchPath, baseContentPath, "basegame_" );
	OpenArchives( archiveSet, basegameArchives, existingResources );

	auto audioArchives = FindPatchArchives( basePatchPath, baseContentPath, "audio_" );
	OpenArchives( archiveSet, audioArchives, existingResources );

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
	auto langArchives = FindLanguageArchives( basePatchPath );
#else
	auto langArchives = FindPatchArchives( basePatchPath, baseContentPath, "lang_" );
#endif
	OpenArchives( archiveSet, langArchives, existingResources );

#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
	auto nonproductionArchives = FindPatchArchives( basePatchPath, baseContentPath, "non_production" );
	OpenArchives( archiveSet, nonproductionArchives, existingResources );

	auto quickLaunchArchives = FindPatchArchives( basePatchPath, baseContentPath, "quicklaunch" );
	OpenArchives( archiveSet, quickLaunchArchives, existingResources );

	auto multiplayerArchives = FindPatchArchives( basePatchPath, baseContentPath, "multiplayer" );
	OpenArchives( archiveSet, multiplayerArchives, existingResources );
#endif
}

void ResourceGameDepot::OpenDLCArchives( ArchiveSet& archiveSet, const red::AbsolutePath& baseDLCPath, red::DynArray< res::ResourcePath >& existingResources )
{
	//auto dlcNames = GFileManager->FindDirectories( baseDLCPath );
	//for ( const auto& dlcName : dlcNames )
	//{
	//	red::AbsolutePath dlcPath = baseDLCPath.AddDirPath( dlcName );
	//	auto dlcArchives = FindArchives( dlcPath, "dlc_" );
	//	OpenArchives( archiveSet, dlcArchives, existingResources );
	//}
}

void ResourceGameDepot::OpenModArchives( ArchiveSet& archiveSet, const red::AbsolutePath& baseModPath, red::DynArray< res::ResourcePath >& existingResources )
{
	const auto modArchives = FindArchives( baseModPath, "" );
	OpenArchives( archiveSet, modArchives, existingResources );

	m_modded = !archiveSet.archives.Empty();
	if ( m_modded )
	{
		RED_LOG_INFO( "ResourceGameDepot: Game is modded, %u Mod archives loaded.", archiveSet.archives.Size() );
	}
}

void ResourceGameDepot::OpenArchives( ArchiveSet& archiveSet, const red::DynArray< red::AbsolutePath >& archivePaths, red::DynArray< res::ResourcePath >& existingResources )
{
	for ( const auto& archivePath : archivePaths )
	{
		RED_LOG( "Loading archive %hs", archivePath.AsChar() );

		archive::SyncFileReader reader = archive::SyncFileReader::Open( archivePath );
		ALWAYSENABLED_RED_FATAL_ASSERT( reader.Opened(), "Could not open archive %hs for reading", archivePath.AsChar() );

		// Opening again with another handle for async access this should be good because the system allows read sharing
#if defined(RED_PLATFORM_DURANGO)
		// Allow unbuffered reads on Xbox, as we are already doing the work to 4KB align the reads
		io::TFileHandle asyncHandle = io::GAsyncIO.OpenFile( archivePath.AsChar(), io::eAsyncFlag_Unbuffered );
#else
		io::TFileHandle asyncHandle = io::GAsyncIO.OpenFile( archivePath.AsChar(), io::eAsyncFlag_None );
#endif
		ALWAYSENABLED_RED_FATAL_ASSERT( asyncHandle != io::INVALID_FILE_HANDLE, "Could not open archive %hs for async reading of contents", archivePath.AsChar() );

		auto instance = red::CreateUniquePtr< archive::Instance >( archive::OpenMode::READING_ONLY, reader, m_nullWriter );
		ALWAYSENABLED_RED_FATAL_ASSERT( instance, "Failed to create the archive %hs", archivePath.AsChar() );

		auto result = instance->Load( existingResources );
		ALWAYSENABLED_RED_FATAL_ASSERT( result == archive::LoadingResult::SUCCESSFUL, "Failed to load archive %hs, error %u, try regenerating archives!", archivePath.AsChar(), result );

		RED_LOG( "Successfully loaded %hs", archivePath.AsChar() );

		red::Blob blob;
		if (StringHelpers::WildcardMatch(archivePath.AsChar(), "*\\memoryresident_*.archive"))
		{
			RED_VERIFY(instance->LoadEntireArchiveIntoMemory(blob));		
		}

		ArchiveInfo info;
		info.reader = std::move( reader );
		info.asyncHandle = asyncHandle;
		info.instance = std::move( instance );
		info.archivePath = archivePath;
		info.optionalMemoryResidentArchive = std::move(blob);

		archiveSet.archives.PushBack( std::move( info ) );
	}
}

void ResourceGameDepot::OpenArchiveSets()
{
	const red::AbsolutePath& dataRootPath = m_dataRootPath;

	ALWAYSENABLED_RED_FATAL_ASSERT( !dataRootPath.Empty(), "Invalid root path for game data! %hs", dataRootPath.AsChar() );

	// Temporary sorted list of resources that we've already loaded so we don't waste memory
	// storing resources which we will never access as they have been overridden in a Mod, DLC, or Patch
	red::DynArray< res::ResourcePath > existingResources{ red::PoolEngine() };
	existingResources.Reserve( 1000000 ); // We have less than 700,000 files but leaving a bit of extra space here so we never reallocate

	// TODO: Might want to call this on another thread or job and have it started before we get here
	// as the SDK functions called by this will be blocking and may take many milliseconds to complete
#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
	m_archiveLanguageMap.Load( dataRootPath );
	m_installedLanguageArchiveNames = GetLanguageArchiveNamesToLoad( m_archiveLanguageMap );
#endif

#if defined( RED_PLATFORM_WINPC )
	// Mod Archives
	const red::AbsolutePath modsPath = prv::MakeModArchivesPath( dataRootPath );
	auto& modSet = m_archives.EmplaceBack( ArchiveCategory::Mod, modsPath );
	OpenModArchives( modSet, modsPath, existingResources );
#endif

	const red::AbsolutePath basePath = prv::MakeContentArchivesPath( dataRootPath );

#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
	// Local override Archives
	// NOTE: Only allowed in non final builds
	const red::AbsolutePath localPath = prv::MakeLocalArchivesPath( dataRootPath );
	auto& localSet = m_archives.EmplaceBack( ArchiveCategory::Local, localPath );
	OpenPatchArchives( localSet, localPath, basePath, existingResources );
#endif

	// Patch Archives
	const red::AbsolutePath patchPath = prv::MakePatchArchivesPath( dataRootPath );
	auto& patchSet = m_archives.EmplaceBack( ArchiveCategory::Patch, patchPath );
	OpenPatchArchives( patchSet, patchPath, basePath, existingResources );

	// DLC Archives
	const red::AbsolutePath baseDLCPath = prv::MakeDLCArchivesPath( dataRootPath );
	auto& dlcSet = m_archives.EmplaceBack( ArchiveCategory::DLC, baseDLCPath );
	OpenDLCArchives( dlcSet, baseDLCPath, existingResources );

	// Base Content Archives
	auto& baseSet = m_archives.EmplaceBack( ArchiveCategory::Content, basePath );
	OpenContentArchives( baseSet, basePath, existingResources );
	ALWAYSENABLED_RED_FATAL_ASSERT( !baseSet.archives.Empty(), "Failed to find any archives in the base data directory '%hs'", basePath.AsChar() );
	red::memory::LogFullReport();
}

bool ResourceGameDepot::FindFileMetadata( const res::ResourcePath resourcePath, archive::FileMetadata& outMetadata ) const
{
	archive::FileID id( resourcePath.GetHash() );

	for ( const auto& archiveSet : m_archives )
	{
		for ( auto& info : archiveSet.archives )
		{
			if ( info.instance->FindFileClosestToOffset( id, 0, outMetadata ) )
			{
				return true;
			}
		}
	}
	return false;
}

//------------------------------------------------------------------------------

const red::DynArray< red::String >& ResourceGameDepot::GetDLCRootPaths() const
{
	static red::DynArray< red::String > s_tempResult{ red::PoolEngine() };
	RED_LOG_ERROR( "ResourceGameDepot: DLC are currently disabled when using Archives!" );
	return s_tempResult;
}

//------------------------------------------------------------------------------

namespace prv
{

red::String GetArchiveCategoryPathString( ArchiveCategory category )
{
	switch ( category )
	{
	case ArchiveCategory::Content:
		return "content";
	case ArchiveCategory::Patch:
		return "patch";
	case ArchiveCategory::Local:
		return "local";
	case ArchiveCategory::DLC:
		return "dlc";
	case ArchiveCategory::Mod:
		return "mod";

	case ArchiveCategory::None:
	default:
		return "";
	};
}

red::AbsolutePath MakeArchiveCategoryPath( const red::AbsolutePath& rootArchivePath, ArchiveCategory category )
{
	return rootArchivePath.AddDirPath( GetArchiveCategoryPathString( category ) );
}

red::AbsolutePath MakeContentArchivesPath( const red::AbsolutePath& rootArchivePath )
{
	return MakeArchiveCategoryPath( rootArchivePath, ArchiveCategory::Content );
}

red::AbsolutePath MakePatchArchivesPath( const red::AbsolutePath& rootArchivePath )
{
	return MakeArchiveCategoryPath( rootArchivePath, ArchiveCategory::Patch );
}

red::AbsolutePath MakeLocalArchivesPath( const red::AbsolutePath& rootArchivePath )
{
	return MakeArchiveCategoryPath( rootArchivePath, ArchiveCategory::Local );
}

red::AbsolutePath MakeDLCArchivesPath( const red::AbsolutePath& rootArchivePath )
{
	return MakeArchiveCategoryPath( rootArchivePath, ArchiveCategory::DLC );
}

red::AbsolutePath MakeModArchivesPath( const red::AbsolutePath& rootArchivePath )
{
	return MakeArchiveCategoryPath( rootArchivePath, ArchiveCategory::Mod );
}

} // prv

} // res
