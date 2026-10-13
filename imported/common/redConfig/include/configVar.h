/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redContainers/include/fundamentalStringConversion.h"
#include "../../redContainers/include/string/string.h"
#include "../../redContainers/include/string/stringLocale.h"
#include "../../redContainers/include/staticArray.h"
#include "../../redMath/include/numericalUtils.h"

namespace Config
{
	class IConfigVarGlobalNotifier;

	struct TGlobalNotifiers
	{
		red::DynArray< IConfigVarGlobalNotifier* > m_notifiers{ red::PoolEngine() };
		red::RWSpinLock m_spinLock;
	};

	/// Type of console variable
	enum EConfigVarType : Uint8
	{
		eConsoleVarType_Bool,			//!< On off
		eConsoleVarType_Int,			//!< Any integer (may be limited by variable)
		eConsoleVarType_Float,			//!< Any real value (may be limited by variable) 
		eConsoleVarType_String,			//!< General string (ascii)
		eConsoleVarType_Color			//!< RGBA
	};

	/// Console variable flags
	enum EConfigVarFlags : Uint8
	{
		eConsoleVarFlag_Save			= RED_FLAG( 0 ),	//!< Value is saved in the user config if modified
		eConsoleVarFlag_Developer		= RED_FLAG( 1 ),	//!< Value is only accessible by developer
		eConsoleVarFlag_ReadOnly		= RED_FLAG( 2 ),	//!< Variable cannot be changed in the console (may be changed using C++ code)
	};

	/// Configuration variable that is read/written to config files and is also changable in runtime
	/// Base abstract class - should not be created dynamically
	class RED_CONFIG_API IConfigVar
	{
	public:
		IConfigVar( const red::AnsiChar* group, const red::AnsiChar* name, const Uint32 flags = 0 );
		virtual ~IConfigVar();

		// Read current value
		virtual Bool GetText( red::String& outValue ) const = 0;

		// Get raw value
		virtual Bool GetRaw( void* outValue, const EConfigVarType expectedType ) const = 0;

		// Set current value
		virtual Bool SetText( const red::String& value ) = 0;

		// Set raw value
		virtual Bool SetRaw( const void* value, const EConfigVarType expectedType ) = 0;

		// Read the default value
		virtual Bool GetTextDefault( red::String& outValue ) const = 0;
		virtual Bool GetRawDefault( void* outValue, const EConfigVarType expectedType ) const = 0;

		virtual Bool GetRawMin( void* outValue, const EConfigVarType expectedType ) const = 0;
		virtual Bool GetRawMax( void* outValue, const EConfigVarType expectedType ) const = 0;
		virtual Bool IsRanged()														const = 0;

		// Returns true if value is different than default value in code
		virtual Bool IsDifferentThanCodeDefault() const
		{
			red::String defaultValue;
			Bool defaultGet = GetTextDefault( defaultValue );
			red::String currentValue;
			Bool currentGet = GetText( currentValue );
			if( defaultGet && currentGet )
				return currentValue != defaultValue;

			return true;
		}

		// Get name of the variable
		RED_FORCE_INLINE const red::AnsiChar* GetName() const { return m_name; }

		// Get group of the variable
		RED_FORCE_INLINE const red::AnsiChar* GetGroup() const { return m_group; }

		// Check flag description
		RED_FORCE_INLINE Bool HasFlag( const EConfigVarFlags flag ) const { return 0 != (m_flags & flag); }

		// Get type description
		virtual EConfigVarType GetType() const = 0;

		// Reset config to default value
		virtual Bool Reset() = 0;

		static void RegisterGlobalNotifier( IConfigVarGlobalNotifier* );
		static void UnRegisterGlobalNotifier( IConfigVarGlobalNotifier* );

		void SetHelpText( const red::AnsiChar* helpText ) { m_help = helpText; }
		const red::AnsiChar* GetHelpText() const { return m_help; }

		void SetOnValueChangedText( const red::AnsiChar* onChangedValue ) { m_onValueChanged = onChangedValue; }
		const red::AnsiChar* GetOnValueChangedText() const { return m_onValueChanged; }

	protected:
		const red::AnsiChar*	m_name;			//!< Configuration variable name
		const red::AnsiChar*	m_group;		//!< Configuration group
		const red::AnsiChar*	m_help;			//!< If specified, will be displayed when set to an invalid value
		const red::AnsiChar*	m_onValueChanged;//!< If specified, will be displayed when the value's modified using the in-game console.
		Uint8					m_flags;		//!< Flags

		static TGlobalNotifiers	st_globalNotifiers;

		void Register();
		void NotifyChanged();
	};

	class IConfigVarGlobalNotifier
	{
		friend class IConfigVar;

	public:
		virtual ~IConfigVarGlobalNotifier(){}

	protected:
		virtual void OnChanged( IConfigVar* changedVar ) = 0;

	private:
		void NotifyCvarChanged( IConfigVar* changedVar );

		red::SpinLock m_configVarGlobalNotifierLock;
	};

	/// Resolves compile time type to a console variable type
	namespace Helper
	{
		template< typename T >
		struct ResolveConsoleType {};

		template<>
		struct ResolveConsoleType<Bool> { static const EConfigVarType value = eConsoleVarType_Bool; };

		template<>
		struct ResolveConsoleType<Int32> { static const EConfigVarType value = eConsoleVarType_Int; };

		template<>
		struct ResolveConsoleType<Float> { static const EConfigVarType value = eConsoleVarType_Float; };

		template<>
		struct ResolveConsoleType<red::String> { static const EConfigVarType value = eConsoleVarType_String; };
	} // Helper

	/// Validation helpers
	namespace Validation
	{
		template < typename TYPE >
		struct RangeValidator
		{
			static void Validate( const TYPE& value, const TYPE& min, const TYPE& max, const char* varName )
			{
				RED_UNUSED( value );
				RED_UNUSED( min );
				RED_UNUSED( max );
				RED_FATAL( "Can not validate range for '%s' config variable - no implementation available.", varName );
			}
		};

		template <>
		struct RangeValidator< Int32 >
		{
			static void Validate( Int32& ret, const Int32 min, const Int32 max, const char* varName )
			{
				if ( ret < min || max < ret )
				{
					RED_LOG_WARNING( "Attempted to set integer config var '%s' to %d while allowed range is %d...%d", varName, ret, min, max );
				}
				ret = math::Clamp< Int32 >( ret, min, max );
			}
		};

		template <>
		struct RangeValidator< Float >
		{
			static void Validate( Float& ret, const Float min, const Float max, const char* varName )
			{
				if ( ret < min || max < ret )
				{
					RED_LOG_WARNING( "Attempted to set float config var '%s' to %f while allowed range is %f...%f", varName, ret, min, max );
				}
				ret = math::Clamp< Float >( ret, min, max );
			}
		};
	} // Validation

	/// In place console variable
	template< typename T >
	class TConfigVar : public IConfigVar
	{
	public:
		TConfigVar( const AnsiChar* group, const AnsiChar* name, const T& defaultValue, const Uint32 flags = eConsoleVarFlag_Save )
			: IConfigVar( group, name, flags )
			, m_value( defaultValue )
			, m_defaultValue( defaultValue )
		{
			Register();
		}

		TConfigVar( const AnsiChar* group, const AnsiChar* name, const T& defaultValue, const T& min, const T& max, const Uint32 flags = 0 )
			: IConfigVar( group, name, flags )
			, m_value( defaultValue )
			, m_min( min )
			, m_max( max )
			, m_defaultValue( defaultValue )
			, m_isRanged( true )
		{
			RED_ASSERT( min <= defaultValue && defaultValue <= max, "Default value of the config var '%s' is out of range." );
			Register();
		}

		const T& Get() const { return m_value; }

		void Set( const T& value )
		{
			m_value = value;
			if ( m_isRanged )
			{
				Validation::RangeValidator< T >::Validate( m_value, m_min, m_max, m_name );
			}

			NotifyChanged();
		}

		const T& GetDefault() const { return m_defaultValue; }

		const T& GetMin() const
		{
			RED_ASSERT( m_isRanged );
			return m_min;
		}

		const T& GetMax() const
		{
			RED_ASSERT( m_isRanged );
			return m_max;
		}

		Bool Reset() override
		{
			Set( m_defaultValue );
			return true;
		}

	private:
		Bool GetText( red::String& outValue ) const override
		{
			red::String txt;
			if ( !::ToString( txt, m_value ) )
				return false;

			outValue = std::move( txt );
			return true;
		}

		Bool GetTextDefault( red::String& outValue ) const override
		{
			red::String txt;
			if ( !::ToString( txt, m_defaultValue ) )
				return false;

			outValue = std::move( txt );
			return true;
		}

		Bool SetText( const red::String& value ) override
		{
			T ret;
			if ( !::FromString( value, ret ) )
				return false;

			Set( ret );
			return true;
		}

		Bool GetRaw( void* outValue, const EConfigVarType expectedType ) const override
		{
			if ( expectedType != GetType() )
			{
				RED_LOG_WARNING( "Core: Invalid access type for console variable '%hs'", GetName() );
				return false;
			}

			*(T*)outValue = m_value;
			return true;
		}

		Bool SetRaw( const void* value, const EConfigVarType expectedType ) override
		{
			if ( expectedType != GetType() )
			{
				RED_LOG_WARNING( "Core: Invalid access type for console variable '%hs'", GetName() );
				return false;
			}

			Set( *(T*)value );
			return true;
		}

		Bool GetRawDefault( void* outValue, const EConfigVarType expectedType ) const override
		{
			if ( expectedType != GetType() )
			{
				RED_LOG_WARNING( "Core: Invalid access type for console variable '%hs'", GetName() );
				return false;
			}

			*(T*)outValue = m_defaultValue;
			return true;
		}

		Bool GetRawMin( void* outValue, const EConfigVarType expectedType ) const override
		{
			if ( !m_isRanged )
			{
				RED_LOG_WARNING( "Core: Variable '%hs' is not ranged", GetName() );
				return false;
			}

			if ( expectedType != GetType() )
			{
				RED_LOG_WARNING( "Core: Invalid access type for console variable '%hs'", GetName() );
				return false;
			}

			*(T*)outValue = m_min;
			return true;
		}

		Bool GetRawMax( void* outValue, const EConfigVarType expectedType ) const override
		{
			if ( !m_isRanged )
			{
				RED_LOG_WARNING( "Core: Variable '%hs' is not ranged", GetName() );
				return false;
			}

			if ( expectedType != GetType() )
			{
				RED_LOG_WARNING( "Core: Invalid access type for console variable '%hs'", GetName() );
				return false;
			}

			*(T*)outValue = m_max;
			return true;
		}

		Bool IsRanged() const override
		{
			return m_isRanged;
		}

		EConfigVarType GetType() const override
		{
			return Helper::ResolveConsoleType< T >::value;
		}

		T m_value;
		T m_min;
		T m_max;
		T m_defaultValue;
		Bool m_isRanged = false;
	};

} // Console