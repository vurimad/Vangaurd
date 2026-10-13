/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../redReflection/include/resourcePath.h"
#include "../../redReflection/include/resourceDepot.h"

#include "../../archive/include/fileIOImpl.h"
#include "archiveLanguageMap.h"

namespace archive
{
class Instance;
class FileMetadata;
class FileID;
}

namespace res
{

enum class ArchiveCategory : Uint32
{
	None,	// Placeholder / Invalid
	Content,// Archives in the base game (Note: Not the base directory)
	DLC,	// Archives for DLC
	Patch,	// Archives for the latest patch
	Local,	// Archives for local overrides
	Mod,	// Archives for mods archives
};

class ResourcePath;

class RED_RESOURCE_API ResourceGameDepot : public IResourceDepot
{
public:
	explicit ResourceGameDepot( const red::AbsolutePath& dataRootPath );
	virtual ~ResourceGameDepot();
	ResourceGameDepot( ResourceGameDepot&& ) = delete;
	ResourceGameDepot& operator=( ResourceGameDepot&& ) = delete;

	virtual red::SharedPtr<serialization::IAsyncSource> CreateResourceAsyncSource( const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params ) override;

	virtual bool ResourceExists( const res::ResourcePath resourcePath ) const override;
	virtual bool ResourceIsReadOnly( const res::ResourcePath resourcePath ) const override;
	virtual Uint64 GetResourceTimestamp( const res::ResourcePath resourcePath ) const override;

	virtual res::ResourcePath ResolveResourcePath( const res::ResourcePath resourcePath ) const override;

	virtual void ResolveResourcePathBulk( const red::ArraySpan<res::ResourcePath>& inOutResourcePaths ) const override;

	virtual const char* GetDebugName() const override;

	virtual Uint32 Debug_GetResourceTotalSizeOnDisk( const res::ResourcePath resourcePath ) const override;
	virtual Uint32 Debug_GetResourceObjectSizeOnDisk( const res::ResourcePath resourcePath ) const override;

	virtual Bool IsLanguageVoicePackAvailable( const CName languageCodeName ) const override;
	virtual Bool IsLanguageVoicePackInstalled( const CName languageCodeName ) const override;

	virtual const red::DynArray< red::String >& GetDLCRootPaths() const override;

#if defined(RED_PLATFORM_ORBIS) || defined(RED_PLATFORM_DURANGO)
	virtual Uint16 GetChunkForVoicePack( res::LanguageID languageID ) override;
	virtual LanguagePackStatus GetVoicePackStatus( res::LanguageID languageID ) override;
	virtual void ChangeChunksStatus( const red::DynArray< Uint16 > &chunks, LanguagePackStatus status ) override;
	virtual Bool HaveArchiveMap() const override;
	virtual Bool IsLanguageChunkID( Uint16 chunkID ) const override;
	virtual res::prv::ArchiveLanguageMap* GetLanguageMap() override;
	virtual LanguageID GetSystemLanguageID() const override;
#endif

private:
	struct ArchiveInfo
	{
		ArchiveInfo();
		~ArchiveInfo();
		ArchiveInfo( ArchiveInfo&& other );
		ArchiveInfo& operator=( ArchiveInfo&& other );

		red::UniquePtr< archive::Instance > instance;
		archive::SyncFileReader reader;
		io::TFileHandle asyncHandle;
		red::AbsolutePath archivePath;
		red::Blob optionalMemoryResidentArchive;
	};

	struct ArchiveSet
	{
		explicit ArchiveSet( ArchiveCategory archiveCategory, const red::AbsolutePath& baseSetPath );
		~ArchiveSet();
		ArchiveSet( ArchiveSet&& other );
		ArchiveSet& operator=( ArchiveSet&& other );

		red::DynArray< ArchiveInfo > archives;
		red::AbsolutePath basePath;
		ArchiveCategory category;
	};

	void OpenArchives( ArchiveSet& archiveSet, const red::DynArray< red::AbsolutePath >& archivePaths, red::DynArray< res::ResourcePath >& existingResources );
	void OpenContentArchives( ArchiveSet& archiveSet, const red::AbsolutePath& baseContentPath, red::DynArray< res::ResourcePath >& existingResources );
	void OpenPatchArchives( ArchiveSet& archiveSet, const red::AbsolutePath& basePatchPath, const red::AbsolutePath& baseContentPath, red::DynArray< res::ResourcePath >& existingResources );
	void OpenDLCArchives( ArchiveSet& archiveSet, const red::AbsolutePath& baseDLCPath, red::DynArray< res::ResourcePath >& existingResources );
	void OpenModArchives( ArchiveSet& archiveSet, const red::AbsolutePath& baseModPath, red::DynArray< res::ResourcePath >& existingResources );
	void OpenArchiveSets();

	bool FindFileMetadata( const res::ResourcePath resourcePath, archive::FileMetadata& outMetadata ) const;

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
	red::DynArray< red::AbsolutePath > FindLanguageArchives( const red::AbsolutePath& basePath ) const;
#endif

	archive::NullFileWriter m_nullWriter;

	red::DynArray< ArchiveSet > m_archives;
	red::DynArray< red::String > m_installedLanguageArchiveNames;

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
	prv::ArchiveLanguageMap m_archiveLanguageMap;
#endif

	const red::AbsolutePath m_dataRootPath;
	bool m_modded;
};

namespace prv
{

RED_RESOURCE_API red::String GetArchiveCategoryPathString( ArchiveCategory category );
RED_RESOURCE_API red::AbsolutePath MakeArchiveCategoryPath( const red::AbsolutePath& rootArchivePath, ArchiveCategory category );

RED_RESOURCE_API red::AbsolutePath MakeContentArchivesPath( const red::AbsolutePath& rootArchivePath );
RED_RESOURCE_API red::AbsolutePath MakePatchArchivesPath( const red::AbsolutePath& rootArchivePath );
RED_RESOURCE_API red::AbsolutePath MakeLocalArchivesPath( const red::AbsolutePath& rootArchivePath );
RED_RESOURCE_API red::AbsolutePath MakeDLCArchivesPath( const red::AbsolutePath& rootArchivePath );
RED_RESOURCE_API red::AbsolutePath MakeModArchivesPath( const red::AbsolutePath& rootArchivePath );

} // prv

} // res
