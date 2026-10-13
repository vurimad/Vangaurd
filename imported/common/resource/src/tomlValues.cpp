/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "tomlValues.h"
#include "../../redContainers/include/string/stringUtils.h"
#include "../../redContainers/include/fundamentalStringParser.h"

namespace red
{
	namespace prv
	{
		Bool TryParseTransform( StringView input, Transform& output )
		{
			input = Trim( input );
			if ( input.StartsWith( '{' ) && input.EndsWith( '}' ) )
			{
				input.TrimFront( 1u );
				input.TrimBack( 1u );
				input = Trim( input );

				auto finder = [&] ( const char* o1 ) -> Uint32 {
					const Uint32 found = input.Find( o1 );
					if ( found != StringView::npos )
						return found + (Uint32)Strlen( o1 );
					return StringView::npos;
				};

				auto findFloatValue = [&] ( StringView inputView ) -> Float {
					inputView = Trim( inputView );
					const char* validFloatChars = "-.0123456789";
					StringView validChars( validFloatChars );

					//Find first char that is not a part of validChars set.
					Uint32 numberLength = 0;
					while ( numberLength < inputView.Length() )
					{
						if ( validChars.Find( inputView[numberLength] ) == StringView::npos )
							break;
						++numberLength;
					}

					inputView = inputView.SubView( 0u, numberLength );
					String inputViewStr = inputView.ToString();
					const AnsiChar* inputViewCStr = inputViewStr.AsChar();

					Float value;
					if ( GParseFloat( inputViewCStr, value ) ) // TODO add StringView variants to avoid temp String
						return value;

					//Will be detected later with Transform::IsOk()
					return RED_FLT_INF;
				};

				const Uint32 XPos = Min( finder( "X =" ), finder( "X=" ) );
				const Uint32 YPos = Min( finder( "Y =" ), finder( "Y=" ) );
				const Uint32 ZPos = Min( finder( "Z =" ), finder( "Z=" ) );

				if ( XPos == StringView::npos || YPos == StringView::npos || ZPos == StringView::npos )
					return false;

				StringView xPosSubView = input.SubView( XPos );
				StringView yPosSubView = input.SubView( YPos );
				StringView zPosSubView = input.SubView( ZPos );

				Vector3 position;
				Quaternion rotation = Quaternion::IDENTITY();

				position.X = findFloatValue( xPosSubView );
				position.Y = findFloatValue( yPosSubView );
				position.Z = findFloatValue( zPosSubView );

				const Uint32 PitchPos = Min( finder( "Pitch=" ), finder( "Pitch =" ) );
				const Uint32 YawPos = Min( finder( "Yaw=" ), finder( "Yaw =" ) );
				const Uint32 RollPos = Min( finder( "Roll=" ), finder( "Roll =" ) );

				//Rotation is optional
				if ( PitchPos != StringView::npos && YawPos != StringView::npos && RollPos != StringView::npos )
				{
					StringView pitchSubView = input.SubView( PitchPos );
					StringView yawSubView = input.SubView( YawPos );
					StringView rollSubView = input.SubView( RollPos );

					const Float pitch = findFloatValue( pitchSubView );
					const Float yaw = findFloatValue( yawSubView );
					const Float roll = findFloatValue( rollSubView );

					EulerAngles eua( roll, pitch, yaw );

					rotation = eua.ToQuat();
				}

				output = Transform( position, rotation );

				return true;
			}
			else
			{
				return false;
			}
		}

		//Unfortunately GParseXYZ are too forgiving :(
		Bool TryParseBool( StringView input, Bool& output )
		{
			if ( input == "true" )
			{
				output = true;
				return true;
			}
			else if ( input == "false" )
			{
				output = false;
				return true;
			}

			return false;
		}

		Bool TryParseInt( StringView input, Int64& output )
		{
			const char* __okChars = "0123456789-";
			StringView okChars( __okChars );

			for ( const char character : input )
			{
				if ( okChars.Find( character ) == StringView::npos )
					return false;
			}

			String stringInput = input.ToString();
			const char* cstrInput = stringInput.AsChar();

			return GParseInt64( cstrInput, output );
		}

		Bool TryParseFloat( StringView input, Float& output )
		{
			const char* __okChars = "0123456789-.";
			StringView okChars( __okChars );

			for ( const char character : input )
			{
				if ( okChars.Find( character ) == StringView::npos )
					return false;
			}

			String stringInput = input.ToString();
			const char* cstrInput = stringInput.AsChar();

			return GParseFloat( cstrInput, output );
		}
	}

	TOMLBaseValue::TOMLBaseValue() = default;
	TOMLBaseValue::TOMLBaseValue( StringView k )
		: key( k.ToString() )
	{}
	TOMLBaseValue::~TOMLBaseValue() = default;

	//////////////////////////////////////////////////////////////////////////

	TOMLKeyValue::TOMLKeyValue() = default;

	TOMLKeyValue::TOMLKeyValue( StringView k, StringView v )
		: TOMLBaseValue( k )
	{
		auto view = Trim( v );
		String temp = view.ToString();
		const char* cstring = temp.AsChar();

		Int64 iUnused;
		Float fUnused;
		Bool bUnused;
		Transform tUnused;

		if ( view.StartsWith( '\"' ) && view.EndsWith( '\"' ) )
		{
			StringView valueView = view.SubView( 1u, view.Length() - 2u );
			type = TOMLValueType::String;
			::new(&value.s) String( valueView.ToString() );
		}
		else if ( prv::TryParseTransform( view, tUnused ) )
		{
			type = TOMLValueType::Transform;
			::new(&value.t) Transform( tUnused );
		}
		else if ( prv::TryParseBool( cstring, bUnused ) )
		{
			type = TOMLValueType::Bool;
			::new(&value.b) Bool( bUnused );
		}
		else if ( prv::TryParseInt( cstring, iUnused ) )
		{
			type = TOMLValueType::Int64;
			::new(&value.i) Int64( iUnused );
		}
		else if ( prv::TryParseFloat( cstring, fUnused ) )
		{
			type = TOMLValueType::Float;
			::new(&value.f) Float( fUnused );
		}
		else
		{
			type = TOMLValueType::INVALID;
		}
	}

	TOMLKeyValue::~TOMLKeyValue()
	{
		switch ( type )
		{
			case TOMLValueType::INVALID:
			case TOMLValueType::Float:
			case TOMLValueType::Bool:
			case TOMLValueType::Int64:
			case TOMLValueType::Transform:
			{
				return;
			}
			case TOMLValueType::String:
			{
				value.s.~String();
				return;
			}
			default:
			{
				break;
			}
		}
	}
	
	//////////////////////////////////////////////////////////////////////////

	TOMLTable::TOMLTable() = default;

	TOMLTable::TOMLTable( StringView n )
		: TOMLBaseValue( n )
	{}

	TOMLTable::TOMLTable( const TOMLTable& table )
		: entries( table.entries )
	{}

	TOMLTable::~TOMLTable() = default;

	ArraySpan< const THandle< const TOMLBaseValue > > TOMLTable::GetEntries() const
	{
		return { entries };
	}

	void TOMLTable::PushBack( THandle< TOMLBaseValue >&& newEntry )
	{
		entries.EmplaceBack( std::move( newEntry ) );
	}

	void TOMLTable::PushBack( const THandle< TOMLBaseValue >& newEntry )
	{
		entries.PushBack( newEntry );
	}

	//////////////////////////////////////////////////////////////////////////

	DEFINE_TOML_VALUE_TYPE( Float, f );
	DEFINE_TOML_VALUE_TYPE( Bool, b );
	DEFINE_TOML_VALUE_TYPE( Transform, t );
	DEFINE_TOML_VALUE_TYPE( String, s );
	DEFINE_TOML_VALUE_TYPE( Int64, i );

	//////////////////////////////////////////////////////////////////////////

	TOMLArrayTable::TOMLArrayTable() = default;

	TOMLArrayTable::TOMLArrayTable( StringView name )
		: TOMLArray< THandle< TOMLTable > >( name )
	{}

	TOMLArrayTable::~TOMLArrayTable() = default;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE_IN_NAMESPACE( TOMLBaseValue, red );
RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TOMLKeyValue, red );
RTTI_PARENT_TYPE( red::TOMLBaseValue );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TOMLTable, red );
RTTI_PARENT_TYPE( red::TOMLBaseValue );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TOMLArrayTable, red );
RTTI_PARENT_TYPE( red::TOMLBaseValue );
RTTI_END_TYPE();
