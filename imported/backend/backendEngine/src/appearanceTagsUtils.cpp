/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "appearanceTagsUtils.h"
#include "../../../common/resource/include/2darray.h"
#include "../../../common/generalEditorBackend/include/appearancesConfig.h"
#include "../../../common/resource/include/jsonResource.h"
#include "../../../common/redReflection/include/resourceAsyncReference.h"
#include "../../common/redJobs2/include/jobBuilder.h"
#include "resourceMetadata.h"

namespace tools
{
	static const String s_configurationFilePath( "base//appearances_mappings.json" );

	void AppearanceTags::Initialize( const CName& baseType, job::Builder* builder )
	{
		m_baseType = baseType;

		if ( builder )
			LoadConfigurationAsync( *builder );
		else
			LoadConfiguration();
	}

	void AppearanceTags::InitializeBaseTypesMaps( const THandle< C2dArray >& baseTypesResource )
	{
		if ( !baseTypesResource )
			return;

		red::DynArray< String > definedBaseTypes{ red::PoolBackend() };
		definedBaseTypes = baseTypesResource->GetColumn<String>( "engineBaseType" );

		red::DynArray< String > baseTypesPath{ red::PoolBackend() };
		baseTypesPath = baseTypesResource->GetColumn<String>( "path" );

		red::DynArray< String > baseTypesAbbr{ red::PoolBackend() };
		baseTypesAbbr = baseTypesResource->GetColumn<String>( "abbreviation" );

		for ( Uint32 i = 0; i < definedBaseTypes.Size(); ++i )
		{
			const CName baseType = RED_NAME( definedBaseTypes[i].AsChar() );
			m_baseTypesTemplatesMap.InsertUnsorted( baseType, res::ResourcePath::Build( baseTypesPath[i] ) );

			if ( !baseTypesAbbr[i].Empty() )
			{
				m_baseTypesAbbreviationsMap.Insert( baseTypesAbbr[i], baseType );
			}
		}
	}

	void AppearanceTags::InitializeCategories( const THandle< C2dArray >& categoriesResource )
	{
		if ( !categoriesResource )
			return;

		red::DynArray< String > definedCategories{ red::PoolBackend() };
		definedCategories = categoriesResource->GetColumn<String>( "categories" );

		for ( Uint32 i = 0; i < definedCategories.Size(); ++i )
		{
			const CName category = RED_NAME( definedCategories[i].AsChar() );
			m_categoriesList.PushBack( category );
		}
	}

	void AppearanceTags::InitializeParts( const THandle< C2dArray >& partsResource )
	{
		if ( !partsResource )
			return;

		red::DynArray< String > definedParts{ red::PoolBackend() };
		definedParts = partsResource->GetColumn<String>( "parts" );

		for ( Uint32 i = 0; i < definedParts.Size(); ++i )
		{
			const CName category = RED_NAME( definedParts[i].AsChar() );
			m_partsList.PushBack( category );
		}
	}

	void AppearanceTags::InitializePartsCategories( const THandle< JsonResource >& partsCategoriesResource )
	{
		if ( !partsCategoriesResource )
			return;

		THandle< PartsCategories > partsCategoriesRoot = partsCategoriesResource->GetRoot< PartsCategories >();
		if ( !partsCategoriesRoot )
			return;

		for ( const PartsCategory& category : partsCategoriesRoot->GetCategories() )
		{
			if ( !category.GetName().Empty() && !category.GetParts().Empty() )
			{
				red::Set< CName > parts;
				for ( const String& part : category.GetParts() )
				{
					const CName partName = RED_NAME( part.AsChar() );
					parts.Insert( partName );
					m_partNameToLogicName.Insert( partName, category.GetLogicName() );
				}
				m_partsCategoriesMap.InsertUnsorted( category.GetName(), parts );
			}
		}
	}

	void AppearanceTags::InitializeBaseTypesMapsAsync( const res::ResourcePath& baseTypesMapPath, job::Builder& builder )
	{
		TResAsyncRef< C2dArray > baseTypesFileRef( baseTypesMapPath );
		res::ResourceTokenHandle token = baseTypesFileRef.IssueLoadingRequest();
		builder.DispatchWait( token->GetWaitCounter() );

		builder.DispatchJob( "AppearanceParts/InitializeBaseTypesMapsAsync", [this, token] ( const job::RunContext& )
		{
			const THandle< C2dArray > baseTypesFile = Cast< C2dArray >( token->GetResource() );
			InitializeBaseTypesMaps( baseTypesFile );
		} );
	}

	void AppearanceTags::InitializeCategoriesAsync( const res::ResourcePath& categoriesPath, job::Builder& builder )
	{
		TResAsyncRef< C2dArray > categoriesFileRef( categoriesPath );
		res::ResourceTokenHandle token = categoriesFileRef.IssueLoadingRequest();

		builder.DispatchWait( token->GetWaitCounter() );

		builder.DispatchJob( "AppearanceParts/InitializeCategoriesAsync", [this, token] ( const job::RunContext& )
		{
			const THandle< C2dArray > categoriesFile = Cast< C2dArray >( token->GetResource() );
			InitializeCategories( categoriesFile );
		} );
	}


	void AppearanceTags::InitializePartsAsync( const res::ResourcePath& partsPath, const res::ResourcePath& partsCategoriesPath, job::Builder& builder )
	{
		TResAsyncRef< C2dArray > partsFileRef( partsPath );
		res::ResourceTokenHandle partsFileToken = partsFileRef.IssueLoadingRequest();

		builder.DispatchWait( partsFileToken->GetWaitCounter() );

		builder.DispatchJob( "AppearanceParts/InitializePartsAsync", [this, partsFileToken] ( const job::RunContext& )
		{
			const THandle< C2dArray > partsFile = Cast< C2dArray >( partsFileToken->GetResource() );
			InitializeParts( partsFile );
		} );

		TResAsyncRef< JsonResource > partsCategoriesFileRef( partsCategoriesPath );
		res::ResourceTokenHandle partsCategoriesFileToken = partsCategoriesFileRef.IssueLoadingRequest();
		builder.DispatchWait( partsCategoriesFileToken->GetWaitCounter() );

		builder.DispatchJob( "AppearanceParts/InitializePartsAsync", [this, partsCategoriesFileToken] ( const job::RunContext& )
		{
			const THandle< JsonResource > partsCategoriesFile = Cast< JsonResource >( partsCategoriesFileToken->GetResource() );
			InitializePartsCategories( partsCategoriesFile );
		} );
	}

	void AppearanceTags::LoadConfiguration()
	{
		TResRef< JsonResource > configurationFile( res::ResourcePath::Build( s_configurationFilePath ) );
		RED_FATAL_ASSERT( configurationFile.Get(), "Missing appearances editor configuration file %hs", s_configurationFilePath.AsChar() );

		THandle< AppearancesConfig > configRoot = configurationFile->GetRoot< AppearancesConfig >();
		if ( !configRoot )
		{
			RED_LOG_ERROR( "[Appearance Editor configuration] Missing json root node" );
			return;
		}

		for ( const AppearanceConfigMapping& mapping : configRoot->GetMappings() )
		{
			if ( mapping.m_name == m_baseType )
			{
				m_baseTypePrefix = mapping.m_baseTypePrefix;

				res::ResourcePath baseTypesFilePath = res::ResourcePath::Build( mapping.m_baseTypesFile );
				if ( baseTypesFilePath.IsValid() )
				{
					TResRef< C2dArray > baseTypesFileRef( baseTypesFilePath );
					InitializeBaseTypesMaps( baseTypesFileRef.Get() );
				}

				res::ResourcePath categoriesFilePath = res::ResourcePath::Build( mapping.m_categoriesFile );
				if ( categoriesFilePath.IsValid() )
				{
					TResRef< C2dArray > categoriesFileRef( categoriesFilePath );
					InitializeCategories( categoriesFileRef.Get() );
				}

				res::ResourcePath partsFilePath = res::ResourcePath::Build( mapping.m_partsFile );
				if ( partsFilePath.IsValid() )
				{
					TResRef< C2dArray > partsFileRef( partsFilePath );
					InitializeParts( partsFileRef.Get() );
				}

				res::ResourcePath partsCategoriesFilePath = res::ResourcePath::Build( mapping.m_partsCategoriesFile );
				if ( partsCategoriesFilePath.IsValid() )
				{
					TResRef< JsonResource > partsCategoriesFileRef( partsCategoriesFilePath );
					InitializePartsCategories( partsCategoriesFileRef.Get() );
				}

				return;
			}
		}
	}

	void AppearanceTags::LoadConfigurationAsync( job::Builder& builder )
	{
		TResAsyncRef< JsonResource > configurationFileRef( res::ResourcePath::Build( s_configurationFilePath ) );
		res::ResourceTokenHandle token = configurationFileRef.IssueLoadingRequest();

		builder.DispatchWait( token->GetWaitCounter() );

		builder.DispatchJob( "AppearanceParts/LoadConfiguration", [this, token] ( const job::RunContext& runContext )
		{
			const THandle< JsonResource > configurationFile = Cast< JsonResource >( token->GetResource() );
			THandle< AppearancesConfig > configRoot = configurationFile->GetRoot< AppearancesConfig >();
			if ( !configRoot )
			{
				RED_LOG_ERROR( "[Appearance Editor configuration] Missing json root node" );
				return;
			}

			job::Builder depBuilder{ runContext };

			const Uint32 mappingsCount = configRoot->GetMappings().Size();
			depBuilder.DispatchParallelForJob( "AppearanceParts/LoadConfiguration/InitMapping", { mappingsCount },
				[this, configRoot] ( Uint32 index, const job::RunContext& runContext )
			{
				const AppearanceConfigMapping& mapping = configRoot->GetMappings()[index];
				if ( mapping.m_name == m_baseType )
				{
					job::Builder depDepBuilder{ runContext };

					m_baseTypePrefix = mapping.m_baseTypePrefix;

					res::ResourcePath baseTypesFile = res::ResourcePath::Build( mapping.m_baseTypesFile );
					if ( baseTypesFile.IsValid() )
					{
						InitializeBaseTypesMapsAsync( baseTypesFile, depDepBuilder );
					}

					res::ResourcePath categoriesFile = res::ResourcePath::Build( mapping.m_categoriesFile );
					if ( categoriesFile.IsValid() )
					{
						InitializeCategoriesAsync( categoriesFile, depDepBuilder );
					}

					res::ResourcePath partsFile = res::ResourcePath::Build( mapping.m_partsFile );
					res::ResourcePath partsCategoriesFile = res::ResourcePath::Build( mapping.m_partsCategoriesFile );
					if ( partsFile.IsValid() )
					{
						InitializePartsAsync( partsFile, partsCategoriesFile, depDepBuilder );
					}

					return;
				}
			}
			);
		} );
	}

	Bool AppearanceTags::IsPartType( const CName& tag ) const
	{
		return m_partsList.Exist( tag );
	}

	CName AppearanceTags::GetLogicPartName( const CName tag ) const
	{
		auto it = m_partNameToLogicName.Find( tag );
		return it == m_partNameToLogicName.End() ? CName::NONE() : it.Value();
	}

	CName AppearanceTags::GetCategoryNameForLogicPartName( const CName logicPartName ) const
	{
		auto it = m_partNameToLogicName.FindValue( logicPartName );
		if ( it == m_partNameToLogicName.End() )
		{
			return CName::NONE();
		}

		const CName partName = it.Key();
		auto categoriesMapIt = std::find_if( m_partsCategoriesMap.Begin(), m_partsCategoriesMap.End(), [partName] ( const red::Map< CName, red::Set< CName > >::ElementType& element )
		{
			return element.Value().Exist( partName );
		} );

		return categoriesMapIt == m_partsCategoriesMap.End() ? CName::NONE() : categoriesMapIt.Key();
	}

	red::DynArray< CName > AppearanceTags::GetResourceMask( const res::ResourcePath& path ) const
	{
		red::DynArray<CName> result;
		if ( THandle<res::ResourceMetadata> metadata = res::ResourceMetadata::Get( path, true ) )
		{
			for ( const CName& tag : metadata->GetTags() )
			{
				if ( IsPartType( tag ) )
				{
					Bool categoryFound = false;
					for ( const auto& category : m_partsCategoriesMap )
					{
						if ( category.Value().Exist( tag ) )
						{
							result.PushBack( red::DynArray< CName >( category.Value().Elements() ) );
							categoryFound = true;
						}
					}

					if ( !categoryFound )
					{
						result.PushBack( tag );
					}
				}
			}
		}
		return result;
	}

	CName AppearanceTags::GetPartType( const THandle< CResource >& baseEntity ) const
	{
		red::DynArray<CName> result;
		if ( THandle<res::ResourceMetadata> metadata = res::ResourceMetadata::Get( baseEntity ) )
		{
			for ( const CName& tag : metadata->GetTags() )
			{
				if ( m_partsList.Exist( tag ) )
				{
					return tag;
				}
			}
		}

		return CName::NONE();
	}

	CName AppearanceTags::GetPartType( const res::ResourcePath& baseEntity ) const
	{
		red::DynArray<CName> result;
		if ( THandle<res::ResourceMetadata> metadata = res::ResourceMetadata::Get( baseEntity, true ) )
		{
			for ( const CName& tag : metadata->GetTags() )
			{
				if ( m_partsList.Exist( tag ) )
				{
					return tag;
				}
			}
		}

		return CName::NONE();
	}

	CName AppearanceTags::GetBaseType( const res::ResourcePath& baseEntity ) const
	{
		red::DynArray<CName> result;
		if ( THandle<res::ResourceMetadata> metadata = res::ResourceMetadata::Get( baseEntity, true ) )
		{
			for ( const CName& tag : metadata->GetTags() )
			{
				String name = tag.AsChar();

				if ( name.BeginsWith( "bodyType" ) )
				{
					name = name.StringAfter( "bodyType" );

					CName baseName = RED_NAME( name.AsChar() );
					if ( m_baseTypesTemplatesMap.KeyExist( RED_NAME( name.AsChar() ) ) )
					{
						return baseName;
					}
				}
			}
		}

		return CName::NONE();
	}

	CName AppearanceTags::GetBaseType( const red::DynArray< CName >& tags ) const
	{
		for ( const CName& tag : tags )
		{
			if ( m_baseTypesTemplatesMap.KeyExist( tag ) )
			{
				return tag;
			}
		}

		return CName::NONE();
	}

	String AppearanceTags::GetBaseTypeAbbr( const res::ResourcePath& baseEntity ) const
	{
		for ( const auto& baseType : m_baseTypesTemplatesMap )
		{
			if ( baseType.Value() == baseEntity )
			{
				for ( const auto& baseAbbr : m_baseTypesAbbreviationsMap )
				{
					if ( baseAbbr.Value() == baseType.Key() )
					{
						return baseAbbr.Key();
					}
				}
				break;
			}
		}
		return String::EMPTY();
	}

	Bool AppearanceTags::GetBaseTypeByAbbr( const String& abbr, CName& outBaseType ) const
	{
		if ( m_baseTypesAbbreviationsMap.KeyExist( abbr ) )
		{
			outBaseType = m_baseTypesAbbreviationsMap[abbr];
			return true;
		}
		return false;
	}

	const red::Map< CName, red::Set< CName > >& AppearanceTags::GetPartCategories() const
	{
		return m_partsCategoriesMap;
	}
}