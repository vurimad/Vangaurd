/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "inGameConfigUtils.h"
#include "redConfigPool.h"
#include "../../redCore/include/absolutePath.h"

namespace InGameConfig
{
	class RED_CONFIG_API Var
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( PoolInGameConfig );

		friend class System;
		friend class Group;
		friend class Reader;

	public:
		Var(CName groupPath,
			CName name,
			CName displayName,
			CName description,
			VarType type,
			VarUpdatePolicy updatePolicy,
			VarImportPolicy importPolicy,
			Int32 flags,
			Int32 order );

		virtual ~Var() = 0;

		virtual Bool WasModifiedSinceLastSave() const = 0;
		virtual Bool HasRequestedValue() const = 0;
		virtual Bool HasDefaultValue() const = 0;
		virtual Bool RestoreDefault( Source source ) = 0;

		CName GetName() const { return m_name; }
		CName GetGroup() const { return m_group; }
		CName GetDisplayName() const { return m_displayName; }
		CName GetDescription() const { return m_description; }
		VarType GetType() const { return m_type; }
		VarUpdatePolicy GetUpdatePolicy() const { return m_updatePolicy; }
		VarImportPolicy GetImportPolicy() const { return m_importPolicy; }
		Bool RequiresRestart() const { return m_updatePolicy == VarUpdatePolicy::RestartRequired; }
		Bool IsInPreGame() const { return HasFlag( VF_InPreGame ); }
		Bool IsInGame() const { return HasFlag( VF_InGame ); }
		Bool IsVisible() const { return HasFlag( VF_Visible ); }
		Bool IsPlatformSpecific() const { return HasFlag( VF_PlatformSpecific ); }
		Bool IsDynamic() const { return HasFlag( VF_Dynamic ); }
		Bool IsDynamicInitialized() const { return HasFlag( VF_Dynamic | VF_DynamicInitialized ); }
		Bool ListHasDisplayValues() const { return HasFlag( VF_ListHasDisplayValues ); }
		Bool IsInput() const { return HasFlag( VF_IsInput ); }
		Bool IsDisabled() const { return HasFlag( VF_Disabled ); }
		Int32 GetOrder() const { return m_order; }
		void SetVisible( Bool visible );
		void SetEnabled( Bool enabled );
		Bool CanBeRestoredToDefault() const { return HasFlag( VF_CanBeRestoredToDefault ); }

		Source SourceOfChange() const { return m_sourceOfChange; }
		void ResetSourceOfChange();

		void SetReasonOfChange( const ChangeReason reason ) { m_reasonOfChange = reason; }
		ChangeReason ReasonOfChange() const { return m_reasonOfChange; }
		void ResetReasonOfChange();

		Bool MarkedAsNeedRestoreToDefault() const { return HasFlag( VF_MarkedAsRestoreToDefault ); }

	protected:
		template< typename T >
		void InternalSetValue( T newValue, Source source );

		RED_INLINE void SetFlag( const Int32 flag ) { m_flags |= flag; }
		RED_INLINE void ResetFlag( const Int32 flag ) { m_flags &= ~flag; }
		RED_INLINE Bool HasFlag( const Int32 flag ) const { return ( m_flags & flag ) == flag; }

		virtual void InternalSetRequestedValue( const void* value ) = 0;
		virtual void InternalAcceptValue() = 0;
		virtual void InternalRejectValue() = 0;
		virtual void InternalMarkAsSaved() = 0;

		virtual void InternalLoadValue( const void* value ) = 0;

		void InternalMarkAsNeedRestoreToDefault();
		void InternalResetNeedRestoreToDefault();

		void ScheduleVarUpdateWithoutConfirmation();
		void ScheduleVarUpdateWithConfirmation();
		void ScheduleVarUpdateWithRestart();
		void ScheduleVarUpdateWithLoadLastCheckpoint();
		void ScheduleVarLoadFromSettings();

		const CName m_name;
		const CName m_group;
		const CName m_displayName;
		const CName m_description;
		const VarType m_type;
		const VarUpdatePolicy m_updatePolicy;
		const VarImportPolicy m_importPolicy;
		Int32 m_flags;
		Int32 m_order;
		Source m_sourceOfChange;
		ChangeReason m_reasonOfChange;
	};

	class RED_CONFIG_API VarBool : public Var
	{
		friend class System;

	public:
		VarBool(
			CName groupPath,
			CName name,
			CName displayName,
			CName description,
			VarUpdatePolicy updatePolicy,
			VarImportPolicy importPolicy,
			Int32 flags,
			Int32 order,
			Bool value,
			Bool defaultValue );

		Bool WasModifiedSinceLastSave() const override
		{
			return m_importPolicy != VarImportPolicy::Ignore && m_value != m_lastSavedValue;
		}

		Bool HasRequestedValue() const override
		{
			return m_value != m_requestedValue;
		}

		Bool HasDefaultValue() const override
		{
			const Bool c_value = HasRequestedValue() ? GetRequestedValue() : GetValue();
			const Bool c_defaultValue = GetDefaultValue();

			return c_value == c_defaultValue;
		}

		Bool RestoreDefault( const Source source ) override
		{
			const Bool c_hasDefaultValue = HasDefaultValue();
			if ( !c_hasDefaultValue )
			{
				SetValue( GetDefaultValue(), source );
			}

			return !c_hasDefaultValue;
		}

		void SetValue( Bool value, Source source );

		Bool GetValue() const { return m_value; }
		Bool GetDefaultValue() const { return m_defaultValue; }

		Bool GetRequestedValue() const { return m_requestedValue; }

	private:
		void InternalSetRequestedValue( const void* value ) override
		{
			m_requestedValue = *reinterpret_cast<const Bool*>( value );
		}

		void InternalAcceptValue() override
		{
			m_value = m_requestedValue;
		}

		void InternalRejectValue() override
		{
			m_requestedValue = m_value;
		}

		void InternalMarkAsSaved() override
		{
			m_lastSavedValue = m_value;
		}

		void InternalLoadValue( const void* value ) override
		{
			m_value = m_requestedValue = m_lastSavedValue = *reinterpret_cast<const Bool*>( value );
		}

		Bool m_value;
		Bool m_defaultValue;
		Bool m_requestedValue;
		Bool m_lastSavedValue;
	};

	class RED_CONFIG_API VarName : public Var
	{
		friend class System;

	public:
		VarName(
			CName groupPath,
			CName name,
			CName displayName,
			CName description,
			VarUpdatePolicy updatePolicy,
			VarImportPolicy importPolicy,
			Int32 flags,
			Int32 order,
			CName value,
			CName defaultValue );

		Bool WasModifiedSinceLastSave() const override
		{
			return m_importPolicy != VarImportPolicy::Ignore && m_value != m_lastSavedValue;
		}

		Bool HasRequestedValue() const override
		{
			return m_value != m_requestedValue;
		}

		Bool HasDefaultValue() const override
		{
			const CName value = HasRequestedValue() ? GetRequestedValue() : GetValue();
			const CName defaultValue = GetDefaultValue();

			return value == defaultValue;
		}

		Bool RestoreDefault( const Source source ) override
		{
			const Bool c_hasDefaultValue = HasDefaultValue();
			if ( !c_hasDefaultValue )
			{
				SetValue( GetDefaultValue(), source );
			}

			return !c_hasDefaultValue;
		}

		void SetValue( CName value, const Source source );
		void SetDefaultValue( CName defaultValue ) { m_defaultValue = defaultValue; }

		CName GetValue() const { return m_value; }
		CName GetDefaultValue() const { return m_defaultValue; }

		CName GetRequestedValue() const { return m_requestedValue; }

	private:
		void InternalSetRequestedValue( const void* value ) override
		{
			m_requestedValue = *reinterpret_cast<const CName*>( value );
		}

		void InternalAcceptValue() override
		{
			m_value = m_requestedValue;
		}

		void InternalRejectValue() override
		{
			m_requestedValue = m_value;
		}

		void InternalMarkAsSaved() override
		{
			m_lastSavedValue = m_value;
		}

		void InternalLoadValue( const void* value ) override
		{
			m_value = m_requestedValue = m_lastSavedValue = *reinterpret_cast<const CName*>( value );
		}

		CName m_value;
		CName m_defaultValue;
		CName m_requestedValue;
		CName m_lastSavedValue;
	};

	template< typename T, InGameConfig::VarType Type >
	class RED_CONFIG_API VarRange : public Var
	{
	public:
		VarRange(
			CName groupPath,
			CName name,
			CName displayName,
			CName description,
			VarUpdatePolicy updatePolicy,
			VarImportPolicy importPolicy,
			Int32 flags,
			Int32 order,
			T value,
			T defaultValue,
			T minValue,
			T maxValue,
			T stepValue )
			: Var( groupPath, name, displayName, description, Type, updatePolicy, importPolicy, flags, order )
			, m_value( value )
			, m_defaultValue( defaultValue )
			, m_requestedValue( value )
			, m_lastSavedValue( value )
			, m_minValue( minValue )
			, m_maxValue( maxValue )
			, m_stepValue( stepValue )
		{}

		Bool WasModifiedSinceLastSave() const override
		{
			return m_importPolicy != VarImportPolicy::Ignore && m_value != m_lastSavedValue;
		}

		Bool HasRequestedValue() const override
		{
			return m_value != m_requestedValue;
		}

		Bool HasDefaultValue() const override
		{
			const T c_value = HasRequestedValue() ? GetRequestedValue() : GetValue();
			const T c_defaultValue = GetDefaultValue();

			return c_value == c_defaultValue;
		}

		T GetValue() const { return m_value; }

		T GetDefaultValue() const { return m_defaultValue; }
		T GetMinValue() const { return m_minValue; }
		T GetMaxValue() const { return m_maxValue; }
		T GetStepValue() const { return m_stepValue; }

		T GetRequestedValue() const { return m_requestedValue; }

	protected:
		void InternalSetRequestedValue( const void* value ) override
		{
			m_requestedValue = *reinterpret_cast<const T*>( value );
		}

		void InternalAcceptValue() override
		{
			m_value = m_requestedValue;
		}

		void InternalRejectValue() override
		{
			m_requestedValue = m_value;
		}

		void InternalMarkAsSaved() override
		{
			m_lastSavedValue = m_value;
		}

		void InternalLoadValue( const void* value ) override
		{
			m_value = m_requestedValue = m_lastSavedValue = *reinterpret_cast<const T*>( value );
		}

		T m_value;
		T m_defaultValue;
		T m_requestedValue;
		T m_lastSavedValue;
		T m_minValue;
		T m_maxValue;
		T m_stepValue;
	};

	class RED_CONFIG_API VarInt : public VarRange< Int32, InGameConfig::VarType::Int >
	{
		friend class System;

	public:
		using VarRange< Int32, InGameConfig::VarType::Int >::VarRange;

		void SetValue( Int32 value, Source source );

		Bool RestoreDefault( Source source ) override;
	};

	class RED_CONFIG_API VarFloat : public VarRange< Float, InGameConfig::VarType::Float >
	{
		friend class System;

	public:
		using VarRange< Float, InGameConfig::VarType::Float >::VarRange;

		void SetValue( Float value, Source source );

		Bool RestoreDefault( Source source ) override;
	};

	template< typename T, InGameConfig::VarType Type >
	class RED_CONFIG_API VarList : public Var
	{
	public:
		VarList(
			CName groupPath,
			CName name,
			CName displayName,
			CName description,
			VarUpdatePolicy updatePolicy,
			VarImportPolicy importPolicy,
			Int32 flags,
			Int32 order,
			Int32 index,
			Int32 defaultIndex,
			const red::DynArray< T >& values,
			const red::DynArray< CName >& displayValues )
			: Var( groupPath, name, displayName, description, Type, updatePolicy, importPolicy, flags, order )
			, m_loadedValue( T() )
			, m_index( FixIndex( index, values ) )
			, m_defaultIndex( FixIndex( defaultIndex, values ) )
			, m_requestedIndex( m_index )
			, m_lastSavedIndex( m_index )
			, m_values( values )
			, m_displayValues( PoolInGameConfig() )
		{
			SetDisplayValues( displayValues );
		}

		Bool WasModifiedSinceLastSave() const override
		{
			return m_importPolicy != VarImportPolicy::Ignore && m_index != m_lastSavedIndex;
		}

		Bool HasRequestedValue() const override
		{
			return m_index != m_requestedIndex;
		}

		Bool HasDefaultValue() const override
		{
			const Int32 c_index = HasRequestedValue() ? GetRequestedIndex() : GetIndex();
			const Int32 c_defaultIndex = GetDefaultIndex();

			return c_index == c_defaultIndex;
		}

		Bool RestoreDefault( const Source source ) override
		{
			const Bool c_hasDefaultValue = HasDefaultValue();

			if ( !c_hasDefaultValue )
			{
				SetIndex( GetDefaultIndex(), source );
			}

			return !c_hasDefaultValue;
		}

		T GetValueFor( const Int32 index ) const { return index != -1 ? m_values[ index ] : T(); }
		T GetValue() const { return GetValueFor( m_index ); }
		T GetDefaultValue() const { return GetValueFor( m_defaultIndex ); }
		T GetRequestedValue() const { return GetValueFor( m_requestedIndex ); }
		CName GetDisplayValue( const Int32 index ) const
		{
			return ( index >= 0 && index < static_cast< Int32 >( m_displayValues.Size() ) ) ? m_displayValues[ index ] : CName::NONE();
		}

		void SetValues( const red::DynArray< T >& values, const Bool callSetValue = true )
		{
			m_values = values;

			m_index = FixIndex( m_index, m_values );
			m_defaultIndex = FixIndex( m_defaultIndex, m_values );
			m_requestedIndex = FixIndex( m_requestedIndex, m_values );
			m_lastSavedIndex = FixIndex( m_lastSavedIndex, m_values );

			if ( callSetValue )
			{
				SetIndex( m_index, Source::Code );
			}
		}

		void InitializeDynamic( const red::DynArray< T >& values, const Int32 defaultIndex )
		{
			ALWAYSENABLED_RED_FATAL_ASSERT( IsDynamic() );

			m_defaultIndex = defaultIndex;
			m_index = m_defaultIndex;

			if ( HasFlag( VF_DynamicHasLoadedValue ) )
			{
				const Int32 loadedIndex = values.GetIndex( m_loadedValue );
				m_index = loadedIndex != -1 ? loadedIndex : m_index;

				m_loadedValue = T();
				ResetFlag( VF_DynamicHasLoadedValue );
			}

			m_requestedIndex = m_index;
			m_lastSavedIndex = m_index;

			SetFlag( VF_DynamicInitialized );

			SetValues( values, false );
		}

		const red::DynArray< T >& GetValues() const { return m_values; }

		Int32 GetIndexFor( const T value ) const { return m_values.GetIndex( value ); }
		Int32 GetIndex() const { return m_index; }
		Int32 GetDefaultIndex() const { return m_defaultIndex; }
		Int32 GetRequestedIndex() const { return m_requestedIndex; }

		void SetDefaultIndex( const Int32 index )
		{
			m_defaultIndex = FixIndex( index, m_values );
		}

		void SetIndex( const Int32 index, const Source source, const Bool callSetValue = true )
		{
			if ( callSetValue )
			{
				InternalSetValue( FixIndex( index, m_values ), source );
			}
			else
			{
				m_index = FixIndex( index, m_values );
				m_requestedIndex = m_index;
			}
		}

		void SetDisplayValues( const red::DynArray< CName > &displayValues )
		{
			if ( displayValues.Empty() )
			{
				return;
			}

			ALWAYSENABLED_RED_FATAL_ASSERT( displayValues.Size() == m_values.Size() );

			m_displayValues = displayValues;

			SetFlag( VF_ListHasDisplayValues );
		}

		void ResetMappings()
		{
			m_displayValues.Clear();
			ResetFlag( VF_ListHasDisplayValues );
		}

	protected:
		Int32 FixIndex( const Int32 index, const red::DynArray< T >& values )
		{
			const Int32 size = static_cast< Int32 >( values.Size() );

			if ( values.Empty() )
			{
				return -1;
			}
			else if ( index < 0 )
			{
				return 0;
			}
			else if ( index >= size )
			{
				return size - 1;
			}

			return index;
		}

		void InternalSetRequestedValue( const void* value ) override
		{
			m_requestedIndex = *reinterpret_cast< const Int32* >( value );
		}

		void InternalAcceptValue() override
		{
			m_index = m_requestedIndex;
		}

		void InternalRejectValue() override
		{
			m_requestedIndex = m_index;
		}

		void InternalMarkAsSaved() override
		{
			m_lastSavedIndex = m_index;
		}

		void InternalLoadValue( const void* value ) override
		{
			if ( IsDynamic() && m_values.Empty() )
			{
				m_loadedValue = *reinterpret_cast< const T* >( value );
				SetFlag( VF_DynamicHasLoadedValue );
			}
			else
			{
				m_index = m_requestedIndex = m_lastSavedIndex = *reinterpret_cast< const Int32* >( value );
			}
		}

		T m_loadedValue;
		Int32 m_index;
		Int32 m_defaultIndex;
		Int32 m_requestedIndex;
		Int32 m_lastSavedIndex;
		red::DynArray< T > m_values;
		red::DynArray< CName > m_displayValues;
	};

	class RED_CONFIG_API VarListInt : public VarList< Int32, InGameConfig::VarType::IntList >
	{
		friend class System;

	public:
		using VarList< Int32, InGameConfig::VarType::IntList >::VarList;
	};

	class RED_CONFIG_API VarListFloat : public VarList< Float, InGameConfig::VarType::FloatList >
	{
		friend class System;

	public:
		using VarList< Float, InGameConfig::VarType::FloatList >::VarList;
	};

	class RED_CONFIG_API VarListString : public VarList< red::String, InGameConfig::VarType::StringList >
	{
		friend class System;

	public:
		using VarList< red::String, InGameConfig::VarType::StringList >::VarList;
	};

	class RED_CONFIG_API VarListName : public VarList< CName, InGameConfig::VarType::NameList >
	{
		friend class System;

	public:
		using VarList< CName, InGameConfig::VarType::NameList >::VarList;
	};
}

#include "inGameConfigVar.hpp"
