/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "inGameConfigReader.h"
#include "inGameConfigRegistry.h"
#include "inGameConfigGroup.h"
#include "inGameConfigVar.h"
#include "inGameConfigUtils.h"
#include "inGameConfigFileUtils.h"
#include "../../redCore/include/absolutePath.h"
#include "../../redFileSystem/include/fileSys.h"
#include "inGameConfigSystem.h"

namespace
{
	constexpr auto c_optionsFilename = "options.json";

	constexpr Int32 c_defaultOrder = -1;

	static const CName s_rootGroup = RED_NAME_CONSTEXPR( "/" );

	using InGameConfig::UserSettingsLoadStatus;
	using InGameConfig::RapidJsonValue;

	CName ProcessLocalizedName( const char* localizedNameStr )
	{
		red::StringView localizedNameView( localizedNameStr );
		static constexpr red::StringView prefix = { "LocKey#" };
		if ( localizedNameView.StartsWith( prefix ) )
		{
			localizedNameView.RemovePrefix( prefix.Length() );
			return CName( FromStringDirect<Uint64>( localizedNameView.ToString() ) );
		}
		else
		{
			return RED_NAME( localizedNameView );
		}
	}

	Bool ParseBoolOption( CName groupPath, CName name, const InGameConfig::RapidJsonValue& object, Bool& value, Bool& defaultValue )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "value" ) && object[ "value" ].IsBool(),  "Expected boolean property 'value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "default_value" ) && object[ "default_value" ].IsBool(), "Expected boolean property 'default_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		value = object[ "value" ].GetBool();
		defaultValue = object[ "default_value" ].GetBool();

		return true;
	}

	Bool ParseNameOption( CName groupPath, CName name, const InGameConfig::RapidJsonValue& object, CName& value, CName& defaultValue )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "value" ) && object[ "value" ].IsString(),  "Expected string property 'value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "default_value" ) && object[ "default_value" ].IsString(), "Expected string property 'default_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		value = RED_NAME( object[ "value" ].GetString() );
		defaultValue = RED_NAME( object[ "default_value" ].GetString() );

		return true;
	}

	Bool ParseIntOption( CName groupPath, CName name, const InGameConfig::RapidJsonValue& object, Int32& value, Int32& defaultValue, Int32& minValue, Int32& maxValue, Int32& stepValue )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "value" ) && object[ "value" ].IsInt(), "Expected int property 'value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "default_value" ) && object[ "default_value" ].IsInt(), "Expected int property 'default_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "min_value" ) && object[ "min_value" ].IsInt(), "Expected int property 'min_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "max_value" ) && object[ "max_value" ].IsInt(), "Expected int property 'max_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "step_value" ) && object[ "step_value" ].IsInt(), "Expected int property 'step_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		value = object[ "value" ].GetInt();
		defaultValue = object[ "default_value" ].GetInt();
		minValue = object[ "min_value" ].GetInt();
		maxValue = object[ "max_value" ].GetInt();
		stepValue = object[ "step_value" ].GetInt();

		return true;
	}

	Bool ParseFloatOption( CName groupPath, CName name, const InGameConfig::RapidJsonValue& object, float& value, float& defaultValue, float& minValue, float& maxValue, float& stepValue )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "value" ) && object[ "value" ].IsFloat(), "Expected float property 'value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "default_value" ) && object[ "default_value" ].IsFloat(), "Expected float property 'default_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "min_value" ) && object[ "min_value" ].IsFloat(), "Expected float property 'min_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "max_value" ) && object[ "max_value" ].IsFloat(), "Expected float property 'max_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "step_value" ) && object[ "step_value" ].IsFloat(), "Expected float property 'step_value' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		value = object[ "value" ].GetFloat();
		defaultValue = object[ "default_value" ].GetFloat();
		minValue = object[ "min_value" ].GetFloat();
		maxValue = object[ "max_value" ].GetFloat();
		stepValue = object[ "step_value" ].GetFloat();

		return true;
	}

	Bool ParseIntListOption( CName groupPath, CName name, const InGameConfig::RapidJsonValue& object, Int32& index, Int32& defaultIndex, red::DynArray< Int32 >&values, red::DynArray< CName > &displayValues )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "index" ) && object[ "index" ].IsInt(), "Expected int property 'index' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "default_index" ) && object[ "default_index" ].IsInt(), "Expected int property 'default_index' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		index = object[ "index" ].GetInt();
		defaultIndex = object[ "default_index" ].GetInt();

		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "values" ) && object[ "values" ].IsArray(), "Expected array property 'values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		for ( const auto& x : object[ "values" ].GetArray() )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( x.IsInt(), "Expected property 'values' to be an array of integers. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

			const Int32 v = x.GetInt();

			values.PushBack( v );
		}

		const Bool foundIndex = index >= 0 && index < static_cast< Int32 >( values.Size() );
		const Bool foundDefaultIndex = defaultIndex >= 0 && defaultIndex < static_cast< Int32 >( values.Size() );

		const Uint32 initSize = values.Size();
		red::alg::RemoveDuplicates( values );

		if ( object.HasMember( "display_values" ) )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( object[ "display_values" ].IsArray(), "Expected array property 'display_values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

			for ( const auto &x : object[ "display_values" ].GetArray() )
			{
				ALWAYSENABLED_RED_FATAL_ASSERT( x.IsString(), "Expected property 'display_values' to be an array of strings. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

				displayValues.PushBack( ProcessLocalizedName( x.GetString() ) );
			}

			const Uint32 valuesSize = values.Size();
			const Uint32 displayValuesSize = displayValues.Size();

			ALWAYSENABLED_RED_FATAL_ASSERT( valuesSize == displayValuesSize, "Expected property 'display_values' to have size equal to 'values'. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		}

		ALWAYSENABLED_RED_FATAL_ASSERT( foundIndex, "Wrong value (%d) for property 'index' for in-game config var '%s' defined in group '%s'", index, name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( foundDefaultIndex, "Wrong value (%d) for property 'default_index' for in-game config var '%s' defined in group '%s'", defaultIndex, name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( initSize == values.Size(), "Duplicated values in property 'values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		return true;
	}

	Bool ParseFloatListOption( CName groupPath, CName name, const InGameConfig::RapidJsonValue& object, Int32& index, Int32& defaultIndex, red::DynArray< Float >&values, red::DynArray< CName > &displayValues )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "index" ) && object[ "index" ].IsInt(), "Expected int property 'index' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "default_index" ) && object[ "default_index" ].IsInt(), "Expected int property 'default_index' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		index = object[ "index" ].GetInt();
		defaultIndex = object[ "default_index" ].GetInt();

		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "values" ) && object[ "values" ].IsArray(), "Expected array property 'values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		for ( const auto& x : object[ "values" ].GetArray() )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( x.IsFloat(), "Expected property 'values' to be an array of floats. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

			const Float v = x.GetFloat();

			values.PushBack( v );
		}

		const Bool foundIndex = index >= 0 && index < static_cast< Int32 >( values.Size() );
		const Bool foundDefaultIndex = defaultIndex >= 0 && defaultIndex < static_cast< Int32 >( values.Size() );

		const Uint32 initSize = values.Size();
		red::alg::RemoveDuplicates( values );

		if ( object.HasMember( "display_values" ) )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( object[ "display_values" ].IsArray(), "Expected array property 'display_values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

			for ( const auto &x : object[ "display_values" ].GetArray() )
			{
				ALWAYSENABLED_RED_FATAL_ASSERT( x.IsString(), "Expected property 'display_values' to be an array of strings. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

				displayValues.PushBack( ProcessLocalizedName( x.GetString() ) );
			}

			const Uint32 valuesSize = values.Size();
			const Uint32 displayValuesSize = displayValues.Size();

			ALWAYSENABLED_RED_FATAL_ASSERT( valuesSize == displayValuesSize, "Expected property 'display_values' to have size equal to 'values'. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		}

		ALWAYSENABLED_RED_FATAL_ASSERT( foundIndex, "Wrong value (%d) for property 'index' for in-game config var '%s' defined in group '%s'", index, name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( foundDefaultIndex, "Wrong value (%d) for property 'default_index' for in-game config var '%s' defined in group '%s'", defaultIndex, name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( initSize == values.Size(), "Duplicated values in property 'values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		return true;
	}

	Bool ParseStringListOption( CName groupPath, CName name, const InGameConfig::RapidJsonValue& object, Int32& index, Int32& defaultIndex, red::DynArray< red::String >&values, red::DynArray< CName > &displayValues )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "index" ) && object[ "index" ].IsInt(), "Expected int property 'index' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "default_index" ) && object[ "default_index" ].IsInt(), "Expected int property 'default_index' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		index = object[ "index" ].GetInt();
		defaultIndex = object[ "default_index" ].GetInt();

		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "values" ) && object[ "values" ].IsArray(), "Expected array property 'values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		for ( const auto& x : object[ "values" ].GetArray() )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( x.IsString(), "Expected property 'values' to be an array of strings. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

			const red::String v = x.GetString();

			values.PushBack( std::move( v ) );
		}

		const Bool foundIndex = index >= 0 && index < static_cast< Int32 >( values.Size() );
		const Bool foundDefaultIndex = defaultIndex >= 0 && defaultIndex < static_cast< Int32 >( values.Size() );

		const Uint32 initSize = values.Size();

		if ( object.HasMember( "display_values" ) )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( object[ "display_values" ].IsArray(), "Expected array property 'display_values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

			for ( const auto &x : object[ "display_values" ].GetArray() )
			{
				ALWAYSENABLED_RED_FATAL_ASSERT( x.IsString(), "Expected property 'display_values' to be an array of strings. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

				displayValues.PushBack( ProcessLocalizedName( x.GetString() ) );
			}

			const Uint32 valuesSize = values.Size();
			const Uint32 displayValuesSize = displayValues.Size();

			ALWAYSENABLED_RED_FATAL_ASSERT( valuesSize == displayValuesSize, "Expected property 'display_values' to have size equal to 'values'. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		}

		ALWAYSENABLED_RED_FATAL_ASSERT( foundIndex, "Wrong value (%d) for property 'index' for in-game config var '%s' defined in group '%s'", index, name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( foundDefaultIndex, "Wrong value (%d) for property 'default_index' for in-game config var '%s' defined in group '%s'", defaultIndex, name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( initSize == values.Size(), "Duplicated values in property 'values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		return true;
	}

	Bool ParseNameListOption( CName groupPath, CName name, const InGameConfig::RapidJsonValue& object, Int32& index, Int32& defaultIndex, red::DynArray< CName >&values, red::DynArray< CName > &displayValues )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "index" ) && object[ "index" ].IsInt(), "Expected int property 'index' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "default_index" ) && object[ "default_index" ].IsInt(), "Expected int property 'default_index' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		index = object[ "index" ].GetInt();
		defaultIndex = object[ "default_index" ].GetInt();

		ALWAYSENABLED_RED_FATAL_ASSERT( object.HasMember( "values" ) && object[ "values" ].IsArray(), "Expected array property 'values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		for ( const auto& x : object[ "values" ].GetArray() )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( x.IsString(), "Expected property 'values' to be an array of strings. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

			values.PushBack( RED_NAME( x.GetString() ) );
		}

		const Bool foundIndex = index >= 0 && index < static_cast< Int32 >( values.Size() );
		const Bool foundDefaultIndex = defaultIndex >= 0 && defaultIndex < static_cast< Int32 >( values.Size() );

		const Uint32 initSize = values.Size();

		if ( object.HasMember( "display_values" ) )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( object[ "display_values" ].IsArray(), "Expected array property 'display_values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

			for ( const auto &x : object[ "display_values" ].GetArray() )
			{
				ALWAYSENABLED_RED_FATAL_ASSERT( x.IsString(), "Expected property 'display_values' to be an array of strings. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

				displayValues.PushBack( ProcessLocalizedName( x.GetString() ) );
			}

			const Uint32 valuesSize = values.Size();
			const Uint32 displayValuesSize = displayValues.Size();

			ALWAYSENABLED_RED_FATAL_ASSERT( valuesSize == displayValuesSize, "Expected property 'display_values' to have size equal to 'values'. In-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );
		}

		ALWAYSENABLED_RED_FATAL_ASSERT( foundIndex, "Wrong value (%d) for property 'index' for in-game config var '%s' defined in group '%s'", index, name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( foundDefaultIndex, "Wrong value (%d) for property 'default_index' for in-game config var '%s' defined in group '%s'", defaultIndex, name.AsChar(), groupPath.AsChar() );
		ALWAYSENABLED_RED_FATAL_ASSERT( initSize == values.Size(), "Duplicated values in property 'values' for in-game config var '%s' defined in group '%s'", name.AsChar(), groupPath.AsChar() );

		return true;
	}

	red::UniquePtr< InGameConfig::Var > CreateOption(
		const InGameConfig::RapidJsonValue& object,
		CName groupPath,
		CName name,
		CName displayName,
		CName description,
		InGameConfig::VarUpdatePolicy updatePolicy,
		InGameConfig::VarImportPolicy importPolicy,
		InGameConfig::VarType type,
		Int32 flags,
		Int32 order )
	{
		const Bool isDynamic = flags & InGameConfig::VF_Dynamic;

		red::UniquePtr< InGameConfig::Var > configVar;

		switch ( type )
		{
		case InGameConfig::VarType::Bool:
			{
				Bool value{}, defaultValue{};
				const Bool wasParsed = ParseBoolOption( groupPath, name, object, value, defaultValue );
				ALWAYSENABLED_RED_FATAL_ASSERT( wasParsed, "Can't parse 'bool' in-game config var" );

				configVar = red::CreateUniquePtr< InGameConfig::VarBool >( groupPath, name, displayName, description, updatePolicy, importPolicy, flags, order, value, defaultValue );
			}
			break;

		case InGameConfig::VarType::Int:
			{
				Int32 value{}, defaultValue{}, minValue{}, maxValue{}, stepValue{};
				const Bool wasParsed = ParseIntOption( groupPath, name, object, value, defaultValue, minValue, maxValue, stepValue );
				ALWAYSENABLED_RED_FATAL_ASSERT( wasParsed, "Can't parse 'int' in-game config var" );

				configVar = red::CreateUniquePtr< InGameConfig::VarInt >( groupPath, name, displayName, description, updatePolicy, importPolicy, flags, order, value, defaultValue, minValue, maxValue, stepValue );
			}
			break;

		case InGameConfig::VarType::Float:
			{
				Float value{}, defaultValue{}, minValue{}, maxValue{}, stepValue{};
				const Bool wasParsed = ParseFloatOption( groupPath, name, object, value, defaultValue, minValue, maxValue, stepValue );
				ALWAYSENABLED_RED_FATAL_ASSERT( wasParsed, "Can't parse 'float' in-game config var" );

				configVar = red::CreateUniquePtr< InGameConfig::VarFloat >( groupPath, name, displayName, description, updatePolicy, importPolicy, flags, order, value, defaultValue, minValue, maxValue, stepValue );
			}
			break;

		case InGameConfig::VarType::Name:
			{
				CName value{}, defaultValue{};
				const Bool wasParsed = ParseNameOption( groupPath, name, object, value, defaultValue );
				ALWAYSENABLED_RED_FATAL_ASSERT( wasParsed, "Can't parse 'name' in-game config var" );

				configVar = red::CreateUniquePtr< InGameConfig::VarName >( groupPath, name, displayName, description, updatePolicy, importPolicy, flags, order, value, defaultValue );
			}
			break;

		case InGameConfig::VarType::IntList:
			{
				Int32 index{ -1 }, defaultIndex{ -1 };
				red::DynArray< Int32 > values{ red::PoolEngine() };
				red::DynArray< CName > displayValues{ red::PoolEngine() };

				if ( !isDynamic )
				{
					const Bool wasParsed = ParseIntListOption( groupPath, name, object, index, defaultIndex, values, displayValues );
					ALWAYSENABLED_RED_FATAL_ASSERT( wasParsed, "Can't parse 'int_list' in-game config var" );
				}

				configVar = red::CreateUniquePtr< InGameConfig::VarListInt >( groupPath, name, displayName, description, updatePolicy, importPolicy, flags, order, index, defaultIndex, values, displayValues );
			}
			break;

		case InGameConfig::VarType::FloatList:
			{
				Int32 index{ -1 }, defaultIndex{ -1 };
				red::DynArray< Float > values{ red::PoolEngine() };
				red::DynArray< CName > displayValues{ red::PoolEngine() };

				if ( !isDynamic )
				{
					const Bool wasParsed = ParseFloatListOption( groupPath, name, object, index, defaultIndex, values, displayValues );
					ALWAYSENABLED_RED_FATAL_ASSERT( wasParsed, "Can't parse 'float_list' in-game config var" );
				}

				configVar = red::CreateUniquePtr< InGameConfig::VarListFloat >( groupPath, name, displayName, description, updatePolicy, importPolicy, flags, order, index, defaultIndex, values, displayValues );
			}
			break;

		case InGameConfig::VarType::StringList:
			{
				Int32 index{ -1 }, defaultIndex{ -1 };
				red::DynArray< red::String > values{ red::PoolEngine() };
				red::DynArray< CName > displayValues{ red::PoolEngine() };

				if ( !isDynamic )
				{
					const Bool wasParsed = ParseStringListOption( groupPath, name, object, index, defaultIndex, values, displayValues );
					ALWAYSENABLED_RED_FATAL_ASSERT( wasParsed, "Can't parse 'string_list' in-game config var" );
				}

				configVar = red::CreateUniquePtr< InGameConfig::VarListString >( groupPath, name, displayName, description, updatePolicy, importPolicy, flags, order, index, defaultIndex, values, displayValues );
			}
			break;

		case InGameConfig::VarType::NameList:
			{
				Int32 index{ -1 }, defaultIndex{ -1 };
				red::DynArray< CName > values{ red::PoolEngine() };
				red::DynArray< CName > displayValues{ red::PoolEngine() };

				if ( !isDynamic )
				{
					const Bool wasParsed = ParseNameListOption( groupPath, name, object, index, defaultIndex, values, displayValues );
					ALWAYSENABLED_RED_FATAL_ASSERT( wasParsed, "Can't parse 'name_list' in-game config var" );
				}

				configVar = red::CreateUniquePtr< InGameConfig::VarListName >( groupPath, name, displayName, description, updatePolicy, importPolicy, flags, order, index, defaultIndex, values, displayValues );
			}
			break;

		default:
			ALWAYSENABLED_RED_FATAL( "Unknown in-game config var type (%d)", static_cast< Int32 >( type ) );
		}

		ALWAYSENABLED_RED_FATAL_ASSERT( configVar, "Couldn't create in-game config var" );

		return configVar;
	}
}

namespace InGameConfig
{
	Reader::Reader( Registry& registry )
		: m_registry( registry )
	{
	}

	Reader::~Reader() = default;

	Bool Reader::Init( const red::AbsolutePath& commonSettingsPath, const red::AbsolutePath& platformSettingsPath )
	{
		const red::AbsolutePath commonPath = commonSettingsPath.AddFilePath( c_optionsFilename );
		const red::AbsolutePath platformPath = platformSettingsPath.AddFilePath( c_optionsFilename );

		m_registry.RegisterGroup( CName::NONE(), s_rootGroup, s_rootGroup, CName::NONE(), 0 );

		const Bool optionsLoaded = LoadTemplates( commonPath, platformPath );
		ALWAYSENABLED_RED_FATAL_ASSERT( optionsLoaded, "Can't load in-game config file" );

		return true;
	}

	Bool Reader::LoadTemplates( const red::AbsolutePath& commonPath, const red::AbsolutePath& platformPath )
	{
		red::String commonString, platformString;

		const Bool commonFileLoaded = red::LoadFileToString( commonPath, commonString );
		ALWAYSENABLED_RED_FATAL_ASSERT( commonFileLoaded, "Failed to load in-game config file from '%s'", commonPath.ToDebugString() );

		const Bool platformFileLoaded = red::LoadFileToString( platformPath, platformString );
		ALWAYSENABLED_RED_FATAL_ASSERT( platformFileLoaded, "Failed to load in-game config file from '%s'", platformPath.ToDebugString() );

		RapidJsonDocument commonDoc, platformDoc;

		rapidjson::ParseResult commonResult = commonDoc.Parse( commonString.AsChar(), commonString.Length() );
		ALWAYSENABLED_RED_FATAL_ASSERT( commonResult, "Failed to load in-game config file from '%s'. Invalid JSON format: %s (%u)", commonPath.ToDebugString(), rapidjson::GetParseError_En( commonResult.Code() ), commonResult.Offset() );

		rapidjson::ParseResult platformResult = platformDoc.Parse( platformString.AsChar(), platformString.Length() );
		ALWAYSENABLED_RED_FATAL_ASSERT( platformResult, "Failed to load in-game config file from '%s'. Invalid JSON format: %s (%u)", platformPath.ToDebugString(), rapidjson::GetParseError_En( platformResult.Code() ), platformResult.Offset() );

		const RapidJsonValue& commonValue( commonDoc );
		ALWAYSENABLED_RED_FATAL_ASSERT( commonValue.IsObject(), "Expected JSON object as a root node of in-game config file '%s'", commonPath.ToDebugString() );
		ALWAYSENABLED_RED_FATAL_ASSERT( commonValue.HasMember( "version" ) && commonValue[ "version" ].IsInt(), "Expected JSON value 'version' in root node of in-game config file '%s'", commonPath.ToDebugString() );

		const RapidJsonValue& platformValue( platformDoc );
		ALWAYSENABLED_RED_FATAL_ASSERT( platformValue.IsObject(), "Expected JSON object as a root node of in-game config file '%s'", platformPath.ToDebugString() );
		ALWAYSENABLED_RED_FATAL_ASSERT( platformValue.HasMember( "version" ) && platformValue[ "version" ].IsInt(), "Expected JSON value 'version' in root node of in-game config file '%s'", platformPath.ToDebugString() );

		const Int32 commonVersion = commonValue[ "version" ].GetInt();
		const Int32 platformVersion = platformValue[ "version" ].GetInt();

		auto &system = InGameConfig::System::GetInstance();

		ALWAYSENABLED_RED_FATAL_ASSERT( commonVersion == platformVersion,
			"File version mismatch, got '%d' in in-game config file '%s' and '%d' in in-game config file '%s'",
			commonVersion, commonPath.ToDebugString(),
			platformVersion, platformPath.ToDebugString() );

		system.SetFileVersion( commonVersion );

		const Bool commonGroupsLoaded = LoadGroups( commonPath, commonDoc );

		if ( !commonGroupsLoaded )
		{
			return false;
		}

		const Bool platformGroupsLoaded = LoadGroups( platformPath, platformDoc );

		if ( !platformGroupsLoaded )
		{
			return false;
		}

		const Bool commonVarsLoaded = LoadConfigVars( commonPath, commonDoc, IsPlatformSpecific::No );

		if ( !commonVarsLoaded )
		{
			return false;
		}

		const Bool platformVarsLoaded = LoadConfigVars( platformPath, platformDoc, IsPlatformSpecific::Yes );

		if ( !platformVarsLoaded )
		{
			return false;
		}

		return true;
	}

	Bool Reader::LoadGroups( const red::AbsolutePath& path, const RapidJsonDocument& doc )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( doc.HasMember( "groups" ) && doc[ "groups" ].IsArray(), "Expected JSON array 'groups' in root node of in-game config file '%s'", path.ToDebugString() );

		const RapidJsonValue& groups = doc[ "groups" ];

		const Uint32 groupsSize = groups.Size();

		for ( Uint32 i = 0; i < groupsSize; ++i )
		{
			const RapidJsonValue& group = groups[ i ];

			ALWAYSENABLED_RED_FATAL_ASSERT( group.IsObject(), "Group must be of type 'object' in in-game config file from '%s'", path.ToDebugString() );

			ALWAYSENABLED_RED_FATAL_ASSERT( group.HasMember( "group_name" ), "Group JSON object needs have 'group_name' member in in-game config file from '%s'", path.ToDebugString() );
			ALWAYSENABLED_RED_FATAL_ASSERT( group[ "group_name" ].IsString(), "Member 'group_name' must be of type 'string' in in-game config file from '%s'", path.ToDebugString() );

			String groupPathString = group[ "group_name" ].GetString();
			const CName groupPath = RED_NAME( groupPathString );

			red::DynArray< CName > parentGroups{ red::PoolEngine() };

			Uint32 index{};
			Uint32 startIndex{};

			while ( groupPathString.IndexOf( '/', index, startIndex ) )
			{
				if ( index != 0 )
				{
					CName name = RED_NAME( groupPathString.MidString( 0, index ) );
					parentGroups.PushBack( name );
				}
				else
				{
					parentGroups.PushBack( s_rootGroup );
				}
				startIndex = index + 1;
			}

			for ( const auto &parentGroup : parentGroups )
			{
				ALWAYSENABLED_RED_FATAL_ASSERT( m_registry.HasGroup( parentGroup ), "Parent group '%hs' needs to exist in in-game config file from '%s'", parentGroup.AsChar(), path.ToDebugString() );
			}

			CName parentGroupPath = parentGroups.Back();

			const CName groupName = RED_NAME( groupPathString.MidString( index + 1 ) );

			Int32 groupOrder = c_defaultOrder;

			if ( group.HasMember( "order" ) )
			{
				ALWAYSENABLED_RED_FATAL_ASSERT( group[ "order" ].IsInt(), "Member 'order' must be of type 'int' in in-game config file from '%s'", path.ToDebugString() );

				groupOrder = group[ "order" ].GetInt();
			}

			CName displayName;
			if ( group.HasMember( "display_name" ) )
			{
				ALWAYSENABLED_RED_FATAL_ASSERT( group[ "display_name" ].IsString(), "Member 'display_name' must be of type 'string' in in-game config file from '%s'", path.ToDebugString() );
				displayName = ProcessLocalizedName( group[ "display_name" ].GetString() );
			}

			m_registry.RegisterGroup( parentGroupPath, groupPath, groupName, displayName, groupOrder );
		}


		m_registry.SortGroups();

		return true;
	}

	Bool Reader::LoadConfigVars( const red::AbsolutePath& path, const RapidJsonDocument& doc, const IsPlatformSpecific isPlatformSpecific )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT( doc.HasMember( "options" ) && doc[ "options" ].IsArray(), "Expected JSON array 'options' in root node of in-game config file '%s'", path.ToDebugString() );

		const RapidJsonValue& value = doc[ "options" ];

		const auto& groups = value.GetArray();
		for ( const auto& groupDef : groups )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( groupDef.IsObject(), "Expected JSON array of objects as a root node of in-game config file '%s'", path.ToDebugString() );
			ALWAYSENABLED_RED_FATAL_ASSERT( groupDef.HasMember( "group_name" ), "Expected property 'group_name' in group definition in in-game config file '%s'", path.ToDebugString() );
			ALWAYSENABLED_RED_FATAL_ASSERT( groupDef.HasMember( "options" ), "Expected property 'options' in group definition in in-game config file '%s'", path.ToDebugString() );

			const char* groupPath = groupDef["group_name"].GetString();

			const CName groupPathName = RED_NAME( groupPath );

			ALWAYSENABLED_RED_FATAL_ASSERT( m_registry.HasGroup( groupPathName ), "Group '%s' not found in in-game config file '%s'", groupPath, path.ToDebugString() );

			auto& group = m_registry.GetGroup( groupPathName );

			const auto& options = groupDef["options"].GetArray();
			for ( const auto& optionDef : options )
			{
				ALWAYSENABLED_RED_FATAL_ASSERT( optionDef.HasMember( "name" ) && optionDef[ "name" ].IsString(), "Expected string property 'name' for option definition in group '%s' in in-game config file '%s'", groupPath, path.ToDebugString() );
				const char* optionName = optionDef[ "name" ].GetString();

				ALWAYSENABLED_RED_FATAL_ASSERT( optionDef.HasMember( "display_name" ) && optionDef[ "display_name" ].IsString(), "Expected string property 'display_name' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );
				const char* optionDisplayName = optionDef[ "display_name" ].GetString();

				Int32 optionOrder = c_defaultOrder;

				if ( optionDef.HasMember( "order" ) )
				{
					ALWAYSENABLED_RED_FATAL_ASSERT( optionDef[ "order" ].IsInt(), "Expected int property 'order' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );

					optionOrder = optionDef[ "order" ].GetInt();
				}

				if ( optionDef.HasMember( "is_input" ) )
				{
					ALWAYSENABLED_RED_FATAL_ASSERT( optionDef[ "is_input" ].IsBool(), "Expected boolean property 'is_input' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );

					const Bool optionIsInput = optionDef[ "is_input" ].GetBool();
					if ( optionIsInput )
					{
						group.AddVar( red::CreateUniquePtr< VarName >(
							groupPathName,
							RED_NAME( optionName ),
							ProcessLocalizedName( optionDisplayName ),
							RED_NAME_CONSTEXPR( "UI-Settings-Bind" ),
							VarUpdatePolicy::ConfirmationRequired,
							VarImportPolicy::ReadValue,
							VF_Visible | VF_InPreGame | VF_InGame | VF_IsInput | VF_CanBeRestoredToDefault | ( isPlatformSpecific == IsPlatformSpecific::Yes ? VF_PlatformSpecific : 0 ),
							optionOrder,
							CName::NONE(),
							CName::NONE()
						) );

						continue;
					}
				}

				ALWAYSENABLED_RED_FATAL_ASSERT( optionDef.HasMember( "description" ) && optionDef[ "description" ].IsString(), "Expected string property 'description' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );
				ALWAYSENABLED_RED_FATAL_ASSERT( optionDef.HasMember( "is_visible" ) && optionDef[ "is_visible" ].IsBool(), "Expected boolean property 'is_visible' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );

				const char* optionDescription =  optionDef["description"].GetString();
				Int32 optionFlags = isPlatformSpecific == IsPlatformSpecific::Yes ? VF_PlatformSpecific : 0;

				const Bool optionIsVisible = optionDef["is_visible"].GetBool();
				if ( optionIsVisible )
				{
					optionFlags |= VF_Visible;
				}

				if ( optionDef.HasMember( "is_dynamic" ) )
				{
					ALWAYSENABLED_RED_FATAL_ASSERT( optionDef[ "is_dynamic" ].IsBool(), "Expected boolean property 'is_dynamic' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );

					const Bool optionIsDynamic = optionDef[ "is_dynamic" ].GetBool();
					if ( optionIsDynamic )
					{
						optionFlags |= VF_Dynamic;
					}
				}

				ALWAYSENABLED_RED_FATAL_ASSERT( optionDef.HasMember( "update_policy" ) && optionDef[ "update_policy" ].IsString(), "Expected string property 'update_policy' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );

				const char* optionUpdatePolicyStr = optionDef["update_policy"].GetString();
				const auto& updatePolicyMapping = m_registry.GetUpdatePolicyMapping();

				const auto configVarUpdatePolicyMappingIt = updatePolicyMapping.Find( optionUpdatePolicyStr );
				ALWAYSENABLED_RED_FATAL_ASSERT( configVarUpdatePolicyMappingIt != updatePolicyMapping.End(), "Invalid value '%s' for string property 'update_policy' for option '%s' in group '%s' in in-game config file '%s'", optionUpdatePolicyStr, optionName, groupPath, path.ToDebugString() );

				const VarUpdatePolicy optionUpdatePolicy = configVarUpdatePolicyMappingIt.Value();

				VarImportPolicy optionImportPolicy = VarImportPolicy::ReadValue;

				if ( optionDef.HasMember( "import_policy" ) )
				{
					ALWAYSENABLED_RED_FATAL_ASSERT( optionDef[ "import_policy" ].IsString(), "Expected string property 'import_policy' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );

					const char* optionImportPolicyStr = optionDef[ "import_policy" ].GetString();
					const auto& importPolicyMapping = m_registry.GetImportPolicyMapping();

					const auto configVarImportPolicyMappingIt = importPolicyMapping.Find( optionImportPolicyStr );
					ALWAYSENABLED_RED_FATAL_ASSERT( configVarImportPolicyMappingIt != importPolicyMapping.End(), "Invalid value '%s' for string property 'import_policy' for option '%s' in group '%s' in in-game config file '%s'", optionImportPolicyStr, optionName, groupPath, path.ToDebugString() );

					optionImportPolicy = configVarImportPolicyMappingIt.Value();
				}

				ALWAYSENABLED_RED_FATAL_ASSERT( optionDef.HasMember( "in_pre_game" ) && optionDef[ "in_pre_game" ].IsBool(), "Expected boolean property 'in_pre_game' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );
				ALWAYSENABLED_RED_FATAL_ASSERT( optionDef.HasMember( "in_game" ) && optionDef[ "in_game" ].IsBool(), "Expected boolean property 'in_game' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );

				const Bool optionInPreGame = optionDef["in_pre_game"].GetBool();
				const Bool optionInGame = optionDef[ "in_game" ].GetBool();

				if ( optionInPreGame )
				{
					optionFlags |= VF_InPreGame;
				}

				if ( optionInGame )
				{
					optionFlags |= VF_InGame;
				}

				Bool optionCanBeRestoredToDefault = true;

				if ( optionDef.HasMember( "can_be_restored_to_default" ) && optionDef[ "can_be_restored_to_default" ].IsBool() )
				{
					optionCanBeRestoredToDefault = optionDef[ "can_be_restored_to_default" ].GetBool();
				}

				if ( optionCanBeRestoredToDefault )
				{
					optionFlags |= VF_CanBeRestoredToDefault;
				}

				ALWAYSENABLED_RED_FATAL_ASSERT( optionDef.HasMember( "type" ) && optionDef[ "type" ].IsString(), "Expected string property 'type' for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path.ToDebugString() );

				const char* optionTypeStr = optionDef["type"].GetString();

				const auto& typeMapping = m_registry.GetTypeMapping();
				const auto configVarTypeMappingIt = typeMapping.Find( optionTypeStr );
				ALWAYSENABLED_RED_FATAL_ASSERT( configVarTypeMappingIt != typeMapping.End(), "Invalid value '%s' for string property 'type' for option '%s' in group '%s' in in-game config file '%s'", optionTypeStr, optionName, groupPath, path.ToDebugString() );

				const VarType optionType = configVarTypeMappingIt.Value();

				red::UniquePtr< Var > configVar = CreateOption(
					optionDef,
					groupPathName,
					RED_NAME( optionName ),
					ProcessLocalizedName( optionDisplayName ),
					ProcessLocalizedName( optionDescription ),
					optionUpdatePolicy,
					optionImportPolicy,
					optionType,
					optionFlags,
					optionOrder );

				ALWAYSENABLED_RED_FATAL_ASSERT( configVar, "Can't create in-game config var!" );
				ALWAYSENABLED_RED_FATAL_ASSERT( configVar->HasDefaultValue(), "Option '%s' in group '%s' in in-game config file '%s' have value different then default", optionName, groupPath, path.ToDebugString() );

				group.AddVar( std::move( configVar ) );
			}
		}

		return true;
	}

	UserSettingsLoadStatus Reader::LoadUserSettings( IFile* file )
	{
		if ( !file )
		{
			return UserSettingsLoadStatus::FileIsMissing;
		}

		red::String string;
		if ( !red::LoadFileToString( *file, string ) )
		{
			return UserSettingsLoadStatus::FileIsMissing;
		}

		if ( string.Empty() )
		{
			return UserSettingsLoadStatus::FileIsCorrupted;
		}

		RapidJsonDocument doc;
		rapidjson::ParseResult result = doc.Parse( string.AsChar(), string.Length() );
		if ( !result )
		{
			return UserSettingsLoadStatus::FileIsCorrupted;
		}

		const RapidJsonValue& json( doc );
		if ( !json.IsObject() )
		{
			return UserSettingsLoadStatus::FileIsCorrupted;
		}

		if ( !json.HasMember( "version" ) || !json[ "version" ].IsInt() )
		{
			return UserSettingsLoadStatus::FileIsCorrupted;
		}

		if ( !json.HasMember( "data" ) || !json[ "data" ].IsArray() )
		{
			return UserSettingsLoadStatus::FileIsCorrupted;
		}

		const Int32 thisVersion = json[ "version" ].GetInt();
		const Int32 fileVersion = System::GetInstance().GetFileVersion();

		const Bool importingFromOldVersion = thisVersion != fileVersion;

		const auto& typeMapping = m_registry.GetTypeMapping();

		auto path = file->GetFileNameForDebug();

		m_registry.MarkAsNeedRestoreToDefault();

		const auto& groups = json[ "data" ].GetArray();
		for ( const auto& groupDef : groups )
		{
			if ( !groupDef.IsObject() )
			{
				RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Expected JSON array of objects as a root node of in-game config file '%s'", path );
				return UserSettingsLoadStatus::FileIsCorrupted;
			}

			if ( !groupDef.HasMember( "group_name" ) || !groupDef[ "group_name" ].IsString() )
			{
				RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Expected string property 'group_name' in group definition in in-game config file '%s'", path );
				return UserSettingsLoadStatus::FileIsCorrupted;
			}

			if ( !groupDef.HasMember( "options" ) || !groupDef[ "options" ].IsArray() )
			{
				RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Expected array property 'options' in group definition in in-game config file '%s'", path );
				return UserSettingsLoadStatus::FileIsCorrupted;
			}

			const char* groupPath = groupDef[ "group_name" ].GetString();

			CName groupPathName = RED_NAME( groupPath );

			if ( !m_registry.HasGroup( groupPathName ) )
			{
				RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Group '%s' not found in in-game config file '%s'", groupPath, path );
				if ( !importingFromOldVersion )
				{
					return UserSettingsLoadStatus::FileIsCorrupted;
				}
				else
				{
					continue;
				}
			}

			auto& group = m_registry.GetGroup( groupPathName );

			const auto& options = groupDef[ "options" ].GetArray();
			for ( const auto& optionDef : options )
			{
				if ( !optionDef.HasMember( "name" ) || !optionDef[ "name" ].IsString() )
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Expected string property 'name' for option definition in group '%s' in in-game config file '%s'", groupPath, path );
					return UserSettingsLoadStatus::FileIsCorrupted;
				}

				if ( !optionDef.HasMember( "type" ) || !optionDef[ "type" ].IsString() )
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Expected string property 'type' for option definition in group '%s' in in-game config file '%s'", groupPath, path );
					return UserSettingsLoadStatus::FileIsCorrupted;
				}

				const char* optionName = optionDef[ "name" ].GetString();
				CName name = RED_NAME( optionName );

				if ( !group.HasVar( name ) )
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Variable '%s' doesn't exist in group '%s' in in-game config file '%s'", optionName, groupPath, path );
					if ( !importingFromOldVersion )
					{
						return UserSettingsLoadStatus::FileIsCorrupted;
					}
					else
					{
						continue;
					}
				}

				auto &commonVar = group.GetVar( name );

				commonVar.InternalRejectValue();
				commonVar.InternalMarkAsSaved();
				commonVar.InternalResetNeedRestoreToDefault();
				commonVar.ResetSourceOfChange();

				const char* typeName = optionDef[ "type" ].GetString();

				const auto configVarTypeMappingIt = typeMapping.Find( typeName );
				if ( configVarTypeMappingIt == typeMapping.End() )
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Invalid value '%s' for string property 'type' for option '%s' in group '%s' in in-game config file '%s'", typeName, optionName, groupPath, path );
					if ( !importingFromOldVersion )
					{
						return UserSettingsLoadStatus::FileIsCorrupted;
					}
					else
					{
						continue;
					}
				}

				const VarType optionType = configVarTypeMappingIt.Value();

				if ( optionType != commonVar.GetType() )
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Type mismatch for option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
					if ( !importingFromOldVersion )
					{
						return UserSettingsLoadStatus::FileIsCorrupted;
					}
					else
					{
						continue;
					}
				}

				const VarImportPolicy varImportPolicy = commonVar.GetImportPolicy();

				if ( varImportPolicy == VarImportPolicy::Ignore )
				{
					continue;
				}

				switch ( optionType )
				{
				case VarType::Bool:
					{
						if ( !optionDef.HasMember( "value" ) )
						{
							RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'value' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
							return UserSettingsLoadStatus::FileIsCorrupted;
						}

						if ( !optionDef[ "value" ].IsBool() )
						{
							RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'value' should be a bool in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
							return UserSettingsLoadStatus::FileIsCorrupted;
						}

						auto &var = group.GetVarBool( name );
						const Bool value = optionDef[ "value" ].GetBool();

						if ( var.GetValue() == value )
						{
							continue;
						}

						commonVar.InternalSetValue( value, Source::LoadSettings );

						break;
					}
				case VarType::Name:
					{
						if ( !optionDef.HasMember( "value" ) )
						{
							RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'value' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
							return UserSettingsLoadStatus::FileIsCorrupted;
						}

						if ( !optionDef[ "value" ].IsString() )
						{
							RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'value' should be a string in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
							return UserSettingsLoadStatus::FileIsCorrupted;
						}

						const char* item = optionDef[ "value" ].GetString();
						const CName value = RED_NAME( item );

						auto& var = group.GetVarName( name );
						if ( var.GetValue() == value )
						{
							continue;
						}

						commonVar.InternalSetValue( value, Source::LoadSettings );
					}
					break;
				case VarType::Int:
					{
						if ( !optionDef.HasMember( "value" ) )
						{
							RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'value' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
							return UserSettingsLoadStatus::FileIsCorrupted;
						}

						if ( !optionDef[ "value" ].IsInt() )
						{
							RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'value' should be an int in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
							return UserSettingsLoadStatus::FileIsCorrupted;
						}

						auto &var = group.GetVarInt( name );
						const Int32 value = optionDef[ "value" ].GetInt();

						if ( var.GetValue() == value )
						{
							continue;
						}

						if ( value >= var.GetMinValue() && value <= var.GetMaxValue() )
						{
							commonVar.InternalSetValue( value, Source::LoadSettings );
						}
						break;
					}
				case VarType::Float:
					{
						if ( !optionDef.HasMember( "value" ) )
						{
							RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'value' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
							return UserSettingsLoadStatus::FileIsCorrupted;
						}

						if ( !optionDef[ "value" ].IsDouble() )
						{
							RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'value' should be a double in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
							return UserSettingsLoadStatus::FileIsCorrupted;
						}

						auto &var = group.GetVarFloat( name );
						const auto value = static_cast< Float >( optionDef[ "value" ].GetDouble() );

						if ( var.GetValue() == value )
						{
							continue;
						}

						if ( value >= var.GetMinValue() && value <= var.GetMaxValue() )
						{
							commonVar.InternalSetValue( value, Source::LoadSettings );
						}
						break;
					}
				case VarType::IntList:
					{
						auto &var = group.GetVarListInt( name );

						if ( var.IsDynamic() )
						{
							if ( !optionDef.HasMember( "value" ) )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'value' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							if ( !optionDef[ "value" ].IsInt() )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'value' should be an int in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							const Int32 value = optionDef[ "value" ].GetInt();
							const auto &values = var.GetValues();

							if ( !values.Empty() )
							{
								const Int32 foundIndex = values.GetIndex( value );
								const Int32 index = foundIndex != -1 ? foundIndex : var.GetDefaultIndex();

								if ( var.GetIndex() == index )
								{
									continue;
								}

								commonVar.InternalSetValue( index, Source::LoadSettings );
							}
							else
							{
								commonVar.InternalLoadValue( &value );
							}
						}
						else
						{
							if ( !optionDef.HasMember( "index" ) )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'index' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							if ( !optionDef[ "index" ].IsInt() )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'index' should be an int in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							const Int32 index = optionDef[ "index" ].GetInt();

							if ( var.GetIndex() == index )
							{
								continue;
							}

							const auto size = static_cast< Int32 >( var.GetValues().Size() );
							if ( index >= 0 && index < size )
							{
								commonVar.InternalSetValue( index, Source::LoadSettings );
							}
						}

						break;
					}
				case VarType::FloatList:
					{
						auto &var = group.GetVarListFloat( name );

						if ( var.IsDynamic() )
						{
							if ( !optionDef.HasMember( "value" ) )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'value' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							if ( !optionDef[ "value" ].IsDouble() )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'value' should be a double in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							const Float value = static_cast< Float >( optionDef[ "value" ].GetDouble() );
							const auto &values = var.GetValues();

							if ( !values.Empty() )
							{
								const Int32 foundIndex = values.GetIndex( value );
								const Int32 index = foundIndex != -1 ? foundIndex : var.GetDefaultIndex();

								if ( var.GetIndex() == index )
								{
									continue;
								}

								commonVar.InternalSetValue( index, Source::LoadSettings );
							}
							else
							{
								commonVar.InternalLoadValue( &value );
							}
						}
						else
						{
							if ( !optionDef.HasMember( "index" ) )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'index' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							if ( !optionDef[ "index" ].IsInt() )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'index' should be an int in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							const Int32 index = optionDef[ "index" ].GetInt();

							if ( var.GetIndex() == index )
							{
								continue;
							}

							const auto size = static_cast< Int32 >( var.GetValues().Size() );
							if ( index >= 0 && index < size )
							{
								commonVar.InternalSetValue( index, Source::LoadSettings );
							}
						}

						break;
					}
				case VarType::StringList:
					{
						auto &var = group.GetVarListString( name );

						if ( var.IsDynamic() )
						{
							if ( !optionDef.HasMember( "value" ) )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'value' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							if ( !optionDef[ "value" ].IsString() )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'value' should be an string in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							const String value = optionDef[ "value" ].GetString();
							const auto &values = var.GetValues();

							if ( !values.Empty() )
							{
								const Int32 foundIndex = values.GetIndex( value );
								const Int32 index = foundIndex != -1 ? foundIndex : var.GetDefaultIndex();

								if ( var.GetIndex() == index )
								{
									continue;
								}

								commonVar.InternalSetValue( index, Source::LoadSettings );
							}
							else
							{
								commonVar.InternalLoadValue( &value );
							}
						}
						else
						{
							if ( !optionDef.HasMember( "index" ) )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'index' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							if ( !optionDef[ "index" ].IsInt() )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'index' should be an int in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							const Int32 index = optionDef[ "index" ].GetInt();

							if ( var.GetIndex() == index )
							{
								continue;
							}

							const auto size = static_cast< Int32 >( var.GetValues().Size() );
							if ( index >= 0 && index < size )
							{
								commonVar.InternalSetValue( index, Source::LoadSettings );
							}
						}

						break;
					}
				case VarType::NameList:
					{
						auto &var = group.GetVarListName( name );

						if ( var.IsDynamic() )
						{
							if ( !optionDef.HasMember( "value" ) )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'value' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							if ( !optionDef[ "value" ].IsString() )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'value' should be a string in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							const CName value = RED_NAME( optionDef[ "value" ].GetString() );
							const auto &values = var.GetValues();

							if ( !values.Empty() )
							{
								const Int32 foundIndex = values.GetIndex( value );
								const Int32 index = foundIndex != -1 ? foundIndex : var.GetDefaultIndex();

								if ( var.GetIndex() == index )
								{
									continue;
								}

								commonVar.InternalSetValue( index, Source::LoadSettings );
							}
							else
							{
								commonVar.InternalLoadValue( &value );
							}
						}
						else
						{
							if ( !optionDef.HasMember( "index" ) )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Missing 'index' in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							if ( !optionDef[ "index" ].IsInt() )
							{
								RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Property 'index' should be an int in option '%s' in group '%s' in in-game config file '%s'", optionName, groupPath, path );
								return UserSettingsLoadStatus::FileIsCorrupted;
							}

							const Int32 index = optionDef[ "index" ].GetInt();

							if ( var.GetIndex() == index )
							{
								continue;
							}

							const auto size = static_cast< Int32 >( var.GetValues().Size() );
							if ( index >= 0 && index < size )
							{
								commonVar.InternalSetValue( index, Source::LoadSettings );
							}
						}

						break;
					}
				}
			}
		}

		m_registry.RestoreToDefaults( Source::LoadSettings, false, false, true );

		return !importingFromOldVersion ? UserSettingsLoadStatus::Loaded : UserSettingsLoadStatus::ImportedFromOldVersion;
	}
}
