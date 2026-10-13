/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class C2dArray;
class JsonResource;

namespace tools
{
	// utility class for appearance resource tags. It reads in the tags setup for chosen baseType (ie. Characters, Vehicles, Weapons etc)
	// from the csv and json configuration files.
	// It allows to check given entity resource's tags setup in context of given appearance baseType.
	class BACKEND_ENGINE_API AppearanceTags
	{
	public:
		void Initialize( const CName& baseType, job::Builder* builder = nullptr );

		Bool IsPartType( const CName& tag ) const;
		CName GetLogicPartName( const CName tag ) const;
		CName GetCategoryNameForLogicPartName( const CName logicPartName ) const;

		red::DynArray< CName > GetResourceMask( const res::ResourcePath& path ) const;
		String GetBaseTypeAbbr( const res::ResourcePath& baseEntity ) const;
		CName GetPartType( const THandle< CResource >& baseEntity ) const;
		CName GetPartType( const res::ResourcePath& baseEntity ) const;
		CName GetBaseType( const res::ResourcePath& baseEntity ) const;
		CName GetBaseType( const red::DynArray< CName >& tags ) const;
		Bool GetBaseTypeByAbbr( const String& abbr, CName& outBaseType ) const;

		const red::Map< CName, red::Set< CName > >& GetPartCategories() const;

	private:
		void LoadConfiguration();
		void LoadConfigurationAsync( job::Builder& builder );

		void InitializeCategoriesAsync( const res::ResourcePath& categoriesPath, job::Builder& builder );
		void InitializePartsAsync( const res::ResourcePath& partsPath, const res::ResourcePath& partsCategoriesPath, job::Builder& builder );
		void InitializeBaseTypesMapsAsync( const res::ResourcePath& baseTypesMapPath, job::Builder& builder );

		void InitializeBaseTypesMaps( const THandle< C2dArray >& baseTypesResource );
		void InitializeCategories( const THandle< C2dArray >& categoriesResource );
		void InitializeParts( const THandle< C2dArray >& partsResource );
		void InitializePartsCategories( const THandle< JsonResource >& partsCategoriesResource );

		CName m_baseType;

		red::DynArray< CName > m_categoriesList;
		red::DynArray< CName > m_partsList;
		red::Map< CName, red::Set< CName > > m_partsCategoriesMap;
		red::HashMap< CName, CName > m_partNameToLogicName;

		String m_baseTypePrefix;
		red::Map< CName, res::ResourcePath > m_baseTypesTemplatesMap;
		red::HashMap< String, CName > m_baseTypesAbbreviationsMap;
	};
}