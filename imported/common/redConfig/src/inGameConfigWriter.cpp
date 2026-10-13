/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "inGameConfigWriter.h"
#include "inGameConfigRegistry.h"
#include "inGameConfigGroup.h"
#include "inGameConfigVar.h"
#include "inGameConfigFileUtils.h"
#include "inGameConfigSystem.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redSystem/include/hex.h"
// #include "../../redFileSystem/include/filePaths.h"

namespace
{
	Bool WriteBoolConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::VarBool& var )
	{
		writer.Key( "type" );
		writer.String( "bool" );

		writer.Key( "value" );
		writer.Bool( var.GetValue() );

		writer.Key( "default_value" );
		writer.Bool( var.GetDefaultValue() );

		return true;
	}

	Bool WriteNameConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::VarName& var )
	{
		writer.Key( "type" );
		writer.String( "name" );

		writer.Key( "value" );
		const auto valueView = var.GetValue().AsStringView();
		writer.String( valueView.Data(), valueView.Length() );

		writer.Key( "default_value" );
		const auto defaultView = var.GetDefaultValue().AsStringView();
		writer.String( defaultView.Data(), defaultView.Length() );

		return true;
	}

	Bool WriteIntConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::VarInt& var )
	{
		writer.Key( "type" );
		writer.String( "int" );

		writer.Key( "value" );
		writer.Int( var.GetValue() );

		writer.Key( "default_value" );
		writer.Int( var.GetDefaultValue() );

		writer.Key( "min_value" );
		writer.Int( var.GetMinValue() );

		writer.Key( "max_value" );
		writer.Int( var.GetMaxValue() );

		writer.Key( "step_value" );
		writer.Int( var.GetStepValue() );

		return true;
	}

	Bool WriteFloatConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::VarFloat& var )
	{
		writer.Key( "type" );
		writer.String( "float" );

		writer.Key( "value" );
		writer.Double( var.GetValue() );

		writer.Key( "default_value" );
		writer.Double( var.GetDefaultValue() );

		writer.Key( "min_value" );
		writer.Double( var.GetMinValue() );

		writer.Key( "max_value" );
		writer.Double( var.GetMaxValue() );

		writer.Key( "step_value" );
		writer.Double( var.GetStepValue() );

		return true;
	}

	Bool WriteListIntConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::VarListInt& var )
	{
		writer.Key( "type" );
		writer.String( "int_list" );

		writer.Key( "is_dynamic" );
		writer.Bool( var.IsDynamic() );

		writer.Key( "value" );
		writer.Int( var.GetValue() );

		writer.Key( "index" );
		writer.Int( var.GetIndex() );

		if ( !var.IsDynamic() )
		{
			writer.Key( "default_index" );
			writer.Int( var.GetDefaultIndex() );

			writer.Key( "values" );
			writer.StartArray();

			for ( const auto& value : var.GetValues() )
			{
				writer.Int( value );
			}

			writer.EndArray();
		}

		return true;
	}

	Bool WriteListFloatConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::VarListFloat& var )
	{
		writer.Key( "type" );
		writer.String( "float_list" );

		writer.Key( "is_dynamic" );
		writer.Bool( var.IsDynamic() );

		writer.Key( "value" );
		writer.Double( var.GetValue() );

		writer.Key( "index" );
		writer.Int( var.GetIndex() );

		if ( !var.IsDynamic() )
		{
			writer.Key( "default_index" );
			writer.Int( var.GetDefaultIndex() );

			writer.Key( "values" );
			writer.StartArray();

			for ( const auto& value : var.GetValues() )
			{
				writer.Double( value );
			}

			writer.EndArray();
		}

		return true;
	}

	Bool WriteListStringConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::VarListString& var )
	{
		writer.Key( "type" );
		writer.String( "string_list" );

		writer.Key( "is_dynamic" );
		writer.Bool( var.IsDynamic() );

		writer.Key( "value" );
		writer.String( var.GetValue().AsChar() );

		writer.Key( "index" );
		writer.Int( var.GetIndex() );

		if ( !var.IsDynamic() )
		{
			writer.Key( "default_index" );
			writer.Int( var.GetDefaultIndex() );

			writer.Key( "values" );
			writer.StartArray();

			for ( const auto& value : var.GetValues() )
			{
				writer.String( value.AsChar() );
			}

			writer.EndArray();
		}

		return true;
	}

	Bool WriteListNameConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::VarListName& var )
	{
		writer.Key( "type" );
		writer.String( "name_list" );

		writer.Key( "is_dynamic" );
		writer.Bool( var.IsDynamic() );

		writer.Key( "value" );
		writer.String( var.GetValue().AsChar() );

		writer.Key( "index" );
		writer.Int( var.GetIndex() );

		if ( !var.IsDynamic() )
		{
			writer.Key( "default_index" );
			writer.Int( var.GetDefaultIndex() );

			writer.Key( "values" );
			writer.StartArray();

			for ( const auto& value : var.GetValues() )
			{
				writer.String( value.AsChar() );
			}

			writer.EndArray();
		}

		return true;
	}

	Bool WriteTypeSpecificData( InGameConfig::RapidJsonWriter& writer, const InGameConfig::Var& var )
	{
		switch ( var.GetType() )
		{
		case InGameConfig::VarType::Bool:
			return WriteBoolConfigVar( writer, static_cast< const InGameConfig::VarBool& >( var ) );

		case InGameConfig::VarType::Int:
			return WriteIntConfigVar( writer, static_cast< const InGameConfig::VarInt& >( var ) );

		case InGameConfig::VarType::Float:
			return WriteFloatConfigVar( writer, static_cast< const InGameConfig::VarFloat& >( var ) );

		case InGameConfig::VarType::Name:
			return WriteNameConfigVar( writer, static_cast< const InGameConfig::VarName& >( var ) );

		case InGameConfig::VarType::IntList:
			return WriteListIntConfigVar( writer, static_cast< const InGameConfig::VarListInt& >( var ) );

		case InGameConfig::VarType::FloatList:
			return WriteListFloatConfigVar( writer, static_cast< const InGameConfig::VarListFloat& >( var ) );

		case InGameConfig::VarType::StringList:
			return WriteListStringConfigVar( writer, static_cast< const InGameConfig::VarListString& >( var ) );

		case InGameConfig::VarType::NameList:
			return WriteListNameConfigVar( writer, static_cast<const InGameConfig::VarListName&>( var ) );

		default:
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Unknown type '%d' for in-game config var '%s:%s'", static_cast< int >( var.GetType() ), var.GetGroup().AsChar(), var.GetName().AsChar() );
			return false;
		}

		return false;
	}

	Bool WriteConfigVar( InGameConfig::RapidJsonWriter& writer, const InGameConfig::Var& var )
	{
		writer.StartObject();

		writer.Key( "name" );
		writer.String( var.GetName().AsChar() );

		if ( !WriteTypeSpecificData( writer, var ) )
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Failed to write type specific data for in-game config var '%s:%s'", var.GetGroup().AsChar(), var.GetName().AsChar() );
			return false;
		}

		writer.EndObject();
		return true;
	}


}

namespace InGameConfig
{
	Writer::~Writer() = default;

	Writer::Writer( const Registry& registry )
		: m_registry{ registry }
	{}

	Uint32 Writer::SaveUserSettings( IFile* file )
	{
		if ( !file )
		{
			return 0;
		}

		String output;
		JsonTextWriter textWriter{ output };
		RapidJsonWriter writer{ textWriter };

		writer.SetMaxDecimalPlaces(10);

		const auto& groups = m_registry.GetGroups();
		if ( groups.Empty() )
		{
			return 0;
		}

		writer.StartObject();

		writer.Key( "version" );
		writer.Int( InGameConfig::System::GetInstance().GetFileVersion() );

		writer.Key( "data" );
		writer.StartArray();

		for ( const auto& group : groups )
		{
			const auto& vars = group.Value()->GetVars( QF_NoFilter );

			if ( vars.Empty() )
			{
				continue;
			}

			writer.StartObject();

			writer.Key( "group_name" );
			writer.String( group.Key().AsChar() );

			writer.Key( "options" );
			writer.StartArray();

			for ( const auto& var : vars )
			{
				if ( !WriteConfigVar( writer, *var ) )
				{
					return 0;
				}
			}

			writer.EndArray();

			writer.EndObject();
		}

		writer.EndArray();

		writer.EndObject();

		file->Serialize( output.AsChar(), output.Length() );

		if ( file->HasErrors() )
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Failed to save %hs", file->GetFileNameForDebug() );
			return 0;
		}

		return output.Length();
	}
}