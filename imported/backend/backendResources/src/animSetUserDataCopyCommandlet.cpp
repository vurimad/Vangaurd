/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/timeLineBase.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/rapidjson.h"

class AnimSetUserDataCopyCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( AnimSetUserDataCopyCommandlet );
	RED_BASE_CLASS( ResaveCommandlet );

public:
	AnimSetUserDataCopyCommandlet();
	virtual ~AnimSetUserDataCopyCommandlet();

	virtual const AnsiChar* GetOneLiner() const override final;
	virtual Bool Execute( const red::CommandLine& options ) override final;
	virtual void PrintHelp() const override final;

private:
	virtual Bool OnResourcePreSave( CResource& resource ) override final;
	Bool CreateMappingFromFile( const red::AbsolutePath& mappingFileAbsolutePath );
	Bool CopyUserData( const anim::AnimSet& sourceAnimSet, anim::AnimSet& destinationAnimSet ) const;
	Bool CopyUserData( const anim::AnimSetEntry& sourceEntry, anim::AnimSetEntry& destinationEntry ) const;
	Bool CopyUserData( const anim::Animation& sourceAnim, anim::Animation& destinationAnim ) const;

	red::HashMap<res::ResourcePath, res::ResourcePath> m_copySourcePathByDestinationPath;
};

RTTI_BEGIN_TYPE( AnimSetUserDataCopyCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

AnimSetUserDataCopyCommandlet::AnimSetUserDataCopyCommandlet()
	: TBaseClass( RED_NAME_CONSTEXPR( "animSetUserDataCopy" ) )
{
}

AnimSetUserDataCopyCommandlet::~AnimSetUserDataCopyCommandlet() = default;

const AnsiChar* AnimSetUserDataCopyCommandlet::GetOneLiner() const
{
	return nullptr;
}

void AnimSetUserDataCopyCommandlet::PrintHelp() const 
{
}

Bool AnimSetUserDataCopyCommandlet::Execute( const red::CommandLine& options )
{
	RED_LOG_INFO( "\n\tANIM SET USER DATA COPY COMMANDLET STARTS...\n" );

	String mappingFileString;
	if ( !options.GetFirstOption( "mappingFile", mappingFileString ) )
	{
		RED_LOG_ERROR( "'mappingFile' argument was not specified." );
		return false;
	}

	const red::AbsolutePath mappingFileAbsolutePath = red::AbsolutePath::CreateFilePath( mappingFileString );
	if ( !CreateMappingFromFile( mappingFileAbsolutePath ) )
	{
		RED_LOG_ERROR( "Failed to create mapping from mapping file." );
		return false;
	}

	if ( m_copySourcePathByDestinationPath.Empty() )
	{
		RED_LOG_ERROR( "There are no entries in the mapping / mapping file." );
		return false;
	}

	red::StringBuilder< String > builder;
	builder.Append( "-force -file" );
	for ( const res::ResourcePath& animSetPath : m_copySourcePathByDestinationPath.GetKeys() )
	{
		builder.Appendf( " %hs", animSetPath.ToDebugString() );
	}
	const String newOptionsString = builder.ToString();
	const red::CommandLine newOptions = red::CommandLine::Parse( newOptionsString.AsChar() );
	const Bool baseExecuteResult = TBaseClass::Execute( newOptions );

	RED_LOG_INFO( "\n\tANIM SET USER DATA COPY COMMANDLET ENDS...\n" );
	return baseExecuteResult;
}

Bool AnimSetUserDataCopyCommandlet::OnResourcePreSave( CResource& resource )
{
	const res::ResourcePath& copyDestinationPath = resource.GetPath();
	const res::ResourcePath* copySourcePath = m_copySourcePathByDestinationPath.FindPtr( copyDestinationPath );
	if ( !copySourcePath )
	{
		RED_LOG_INFO( "Copy destination resource '%hs' is not present in mapping file. It is skipped.", copyDestinationPath.ToDebugString() );
		return false;
	}

	if ( !resource.IsA< anim::AnimSet >() )
	{
		RED_LOG_ERROR( "Copy destination resource '%hs' is not an anim set.", copyDestinationPath.ToDebugString() );
		return false;
	}

	anim::AnimSet& destinationAnimSet = static_cast< anim::AnimSet& >( resource );
	THandle< anim::AnimSet > sourceAnimSet = Cast< anim::AnimSet >( GResourceLoader->IssueLoadingRequest( *copySourcePath )->WaitUntilLoaded() );
	if ( !sourceAnimSet )
	{
		RED_LOG_ERROR( "Copy source anim set '%hs' cannot be loaded.", copySourcePath->ToDebugString() );
		return false;
	}

	if ( !CopyUserData( *sourceAnimSet, destinationAnimSet ) )
	{
		RED_LOG_ERROR( "Failed to copy data from '%hs' to '%hs'.", copySourcePath->ToDebugString(), copyDestinationPath.ToDebugString() );
		return false;
	}

	const res::ResourceEditorDepot* depot = commandlet::GetEditorDepot();
	const String p4CheckoutCommandStr = String( "p4 edit " ) + String( depot->TranslateResourcePath( copyDestinationPath ).ToDebugString() );
	const Int32 commandResult = std::system( p4CheckoutCommandStr.AsChar() );
	if ( commandResult != 0 )
	{
		RED_LOG_ERROR( "Failed to execute command '%hs' (error code = %u).", p4CheckoutCommandStr.AsChar(), commandResult );
		return false;
	}

	return true;
}

Bool AnimSetUserDataCopyCommandlet::CreateMappingFromFile( const red::AbsolutePath& mappingFileAbsolutePath )
{
	String fileData;
	if ( !red::LoadFileToString( mappingFileAbsolutePath, fileData ) )
	{
		RED_LOG_ERROR( "Mapping file '%hs' cannot be opened.", mappingFileAbsolutePath.ToDebugString() );
		return false;
	}

	rapidjson::JSONDocument jsonDocument;
	jsonDocument.Parse< 0 >( fileData.AsChar() );
	if ( jsonDocument.HasParseError() )
	{
		RED_LOG_ERROR( "Mapping file '%hs' cannot be parsed. It is not in JSON format.", mappingFileAbsolutePath.ToDebugString() );
		return false;
	}

	constexpr AnsiChar* entriesPropertyName = "entries";
	const auto iterator = jsonDocument.FindMember( entriesPropertyName );
	if ( iterator == jsonDocument.MemberEnd() )
	{
		RED_LOG_ERROR( "Mapping file does not contain '%hs' property.", entriesPropertyName );
		return false;
	}

	if ( !iterator->value.IsArray() )
	{
		RED_LOG_ERROR( "Property '%hs' in mapping file is not an array.", entriesPropertyName );
		return false;
	}

	const res::ResourceEditorDepot* depot = commandlet::GetEditorDepot();
	for ( auto entryIterator = iterator->value.Begin(); entryIterator != iterator->value.End(); ++entryIterator )
	{
		constexpr AnsiChar* sourceAnimSetProperty = "sourceAnimSet";
		auto sourceAnimSetIterator = entryIterator->FindMember( sourceAnimSetProperty );
		if ( sourceAnimSetIterator == entryIterator->MemberEnd() )
		{
			RED_LOG_ERROR( "Property '%hs' was not found for '%hs' in mapping file.", sourceAnimSetProperty, entryIterator->GetString() );
			return false;
		}

		if ( !sourceAnimSetIterator->value.IsString() )
		{
			RED_LOG_ERROR( "Property '%hs' in mapping file is not a string.", sourceAnimSetProperty );
			return false;
		}

		const res::ResourcePath sourceAnimSetResoucePath = res::ResourcePath::Build( sourceAnimSetIterator->value.GetString() );
		if ( !depot->ResourceExists( sourceAnimSetResoucePath ) )
		{
			RED_LOG_ERROR( "Source anim set '%hs' does not exist.", sourceAnimSetResoucePath.ToDebugString() );
			return false;
		}

		constexpr AnsiChar* destinationAnimSetsProperty = "destinationAnimSets";
		auto desinationAnimSetsIterator = entryIterator->FindMember( destinationAnimSetsProperty );
		if ( desinationAnimSetsIterator == entryIterator->MemberEnd() )
		{
			RED_LOG_ERROR( "Property '%hs' was not found for '%hs' in mapping file.", destinationAnimSetsProperty, entryIterator->GetString() );
			return false;
		}

		if ( !desinationAnimSetsIterator->value.IsArray() )
		{
			RED_LOG_ERROR( "Property '%hs' in mapping file is not an array.", destinationAnimSetsProperty );
			return false;
		}

		for ( auto destinationAnimSetIterator = desinationAnimSetsIterator->value.Begin(); destinationAnimSetIterator != desinationAnimSetsIterator->value.End(); ++destinationAnimSetIterator )
		{
			if ( !destinationAnimSetIterator->IsString() )
			{
				RED_LOG_ERROR( "Property '%hs' in mapping file does not contain only strings.", destinationAnimSetsProperty );
				return false;
			}

			const res::ResourcePath destinationAnimSetResoucePath = res::ResourcePath::Build( destinationAnimSetIterator->GetString() );
			if ( m_copySourcePathByDestinationPath.KeyExist( destinationAnimSetResoucePath ) )
			{
				RED_LOG_ERROR( "Destination anim set '%hs' appears more than once in mapping file.", destinationAnimSetResoucePath.ToDebugString() );
				return false;
			}

			if ( !depot->ResourceExists( destinationAnimSetResoucePath ) )
			{
				RED_LOG_ERROR( "Destination anim set '%hs' does not exist.", destinationAnimSetResoucePath.ToDebugString() );
				return false;
			}

			m_copySourcePathByDestinationPath[destinationAnimSetResoucePath] = sourceAnimSetResoucePath;
		}
	}

	RED_LOG_INFO( "Mapping is successfully created from mapping file '%hs'.", mappingFileAbsolutePath.ToDebugString() );
	return true;
}

Bool AnimSetUserDataCopyCommandlet::CopyUserData( const anim::AnimSet& sourceAnimSet, anim::AnimSet& destinationAnimSet ) const
{
	destinationAnimSet.EDITOR_GetTags() = sourceAnimSet.GetTags();
	destinationAnimSet.m_propSetups = sourceAnimSet.m_propSetups;

	const red::DynArray< THandle< anim::AnimSetEntry > >& sourceAnimations = sourceAnimSet.EDITOR_GetAnimations();
	const red::DynArray< THandle< anim::AnimSetEntry > >& destinationAnimations = destinationAnimSet.EDITOR_GetAnimations();
	if ( sourceAnimations.Size() != destinationAnimations.Size() )
	{
		RED_LOG_WARNING( "Source anim set '%hs' and destination anim set have different animations count.", sourceAnimSet.GetPath().ToDebugString(), destinationAnimSet.GetPath().ToDebugString() );
	}

	for ( const THandle< anim::AnimSetEntry >& sourceAnimSetEntry : sourceAnimations )
	{
		auto iterator = std::find_if( destinationAnimations.Begin(), destinationAnimations.End(), [&sourceAnimSetEntry]( const THandle< anim::AnimSetEntry >& destinationAnimSetEntry )
		{
			return sourceAnimSetEntry->GetName() == destinationAnimSetEntry->GetName();
		} );

		if ( iterator == destinationAnimations.End() )
		{
			RED_LOG_WARNING( "Source animation '%hs' in '%hs' anim set has no destination animation with the same name in '%hs'.", sourceAnimSetEntry->GetName().AsChar(), sourceAnimSet.GetPath().ToDebugString(), destinationAnimSet.GetPath().ToDebugString() );
			continue;
		}

		if ( !CopyUserData( *sourceAnimSetEntry, **iterator ) )
		{
			RED_LOG_ERROR( "Failed to copy data for animation set entry '%hs' from '%hs' to '%hs'.", sourceAnimSetEntry->GetName().AsChar(), sourceAnimSet.GetPath().ToDebugString(), destinationAnimSet.GetPath().ToDebugString() );
			return false;
		}
	}

	RED_LOG_INFO( "All data is successfully copied from '%hs' to '%hs'.", sourceAnimSet.GetPath().ToDebugString(), destinationAnimSet.GetPath().ToDebugString() );
	return true;
}

Bool AnimSetUserDataCopyCommandlet::CopyUserData( const anim::AnimSetEntry& sourceEntry, anim::AnimSetEntry& destinationEntry ) const
{
	destinationEntry.EDITOR_GetRegularEvents() = sourceEntry.EDITOR_GetRegularEvents();
	destinationEntry.SetBackendData( sourceEntry.GetBackendData() );

	if ( !CopyUserData( *sourceEntry.GetAnimation(), *destinationEntry.GetAnimation() ) )
	{
		RED_LOG_ERROR( "Failed to copy animation '%hs'.", sourceEntry.GetName().AsChar() );
		return false;
	}

	const TResRef< JsonResource > sourceAudioResource = sourceEntry.GetAudioResource();
	if ( sourceEntry.GetAudioResource() )
	{
		const res::ResourcePath& sourceJsonPath = sourceAudioResource.GetPath();
		const String sourceJsonPathStr = sourceJsonPath.ToString();
		String jsonFileNameStr;
		const Bool isJsonFileNameSplitSuccessful = sourceJsonPathStr.SplitFromRight( "\\", nullptr, &jsonFileNameStr );
		RED_ASSERT( isJsonFileNameSplitSuccessful );

		const res::ResourcePath& destinationAnimSetPath = destinationEntry.GetParent()->GetPath();
		String destinationJsonPathStr = destinationAnimSetPath.ToString();
		const Bool isReplaceSuccessful = destinationJsonPathStr.Replace( "base\\animations\\", "base\\sound\\animation\\" );
		RED_ASSERT( isReplaceSuccessful );
		const Bool isReplaceSuccessful2 = destinationJsonPathStr.Replace( ".anims", "\\" + jsonFileNameStr, true );
		RED_ASSERT( isReplaceSuccessful2 );
		const res::ResourcePath destinationJsonPath = res::ResourcePath::Build( destinationJsonPathStr.AsChar() );
		destinationEntry.GetAudioResource().SetPath( destinationJsonPath );

		const res::ResourceEditorDepot* depot = commandlet::GetEditorDepot();
		const red::AbsolutePath sourceJsonAbsolutePath = depot->TranslateResourcePath( sourceJsonPath );
		const red::AbsolutePath destinationJsonAbsolutePath = depot->TranslateResourcePath( destinationJsonPath );
		const Bool isPathCreatedSuccessfully = GFileManager->CreatePath( red::paths::ParentAbsolutePath( destinationJsonAbsolutePath ) );
		RED_ASSERT( isPathCreatedSuccessfully );

		const Bool didFileExist = GFileManager->FileExist( destinationJsonAbsolutePath );

		if ( didFileExist )
		{
			const String p4CommandStr = String( "p4 edit " ) + String( destinationJsonAbsolutePath.ToDebugString() );
			const Int32 commandResult = std::system( p4CommandStr.AsChar() );
			if ( commandResult != 0 )
			{
				RED_LOG_ERROR( "Failed to execute command '%hs' (error code = %u).", p4CommandStr.AsChar(), commandResult );
				return false;
			}
		}

		if ( !GFileManager->CopyFile( sourceJsonAbsolutePath, destinationJsonAbsolutePath, true ) )
		{
			RED_LOG_ERROR( "Failed to copy '%hs' to '%hs'.", sourceJsonPath.ToDebugString(), destinationJsonPath.ToDebugString() );
			return false;
		}

		if ( !GFileManager->SetFileReadOnly( destinationJsonAbsolutePath, false ) )
		{
			RED_LOG_ERROR( "Failed to mark '%hs' file as not read-only.", destinationJsonPath.ToDebugString() );
			return false;
		}

		if ( !didFileExist )
		{
			const String p4CommandStr = String( "p4 add " ) + String( destinationJsonAbsolutePath.ToDebugString() );
			const Int32 commandResult = std::system( p4CommandStr.AsChar() );
			if ( commandResult != 0 )
			{
				RED_LOG_ERROR( "Failed to execute command '%hs' (error code = %u).", p4CommandStr.AsChar(), commandResult );
				return false;
			}
		}
	}
	else
	{
		destinationEntry.GetAudioResource() = nullptr;
	}

	return true;
}

Bool AnimSetUserDataCopyCommandlet::CopyUserData( const anim::Animation& sourceAnim, anim::Animation& destinationAnim ) const
{
	destinationAnim.EDITOR_SetTags( sourceAnim.EDITOR_GetTags() );
	destinationAnim.EDITOR_SetAudioName( sourceAnim.GetAudioName() );
	destinationAnim.EDITOR_SetAdditionalTransforms( sourceAnim.EDITOR_GetAdditionalTransforms() );
	destinationAnim.EDITOR_SetAdditionalTracks( sourceAnim.EDITOR_GetAdditionalTracks() );
	destinationAnim.EDITOR_SetFrameClamping( sourceAnim.EDITOR_GetFrameClamping() );
	destinationAnim.EDITOR_SetFrameClampingStartFrame( sourceAnim.EDITOR_GetFrameClampingStartFrame() );
	destinationAnim.EDITOR_SetFrameClampingEndFrame( sourceAnim.EDITOR_GetFrameClampingEndFrame() );
	return true;
}
