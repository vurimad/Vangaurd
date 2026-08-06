/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiType.h"

namespace rtti
{
	using EnumValuePair = std::pair< CName, Int64 >;

	/// Enumeration - named options that have integer (int32) values
	class RED_REFLECTION_API EnumType : public rtti::IType
	{
	public:
		EnumType( const CName name, Uint32 size, Bool scripted );

		/// get list of enumeration options
		typedef red::DynArray< CName > TNames;
		RED_FORCE_INLINE const TNames& GetOptions() const { return m_options; }

		/// get list of enumeration values
		typedef red::DynArray< Int64 > TValues;
		RED_FORCE_INLINE const TValues& GetValues() const { return m_values; }

		/// get list of legacy enumeration options
		RED_FORCE_INLINE const TNames& GetLegacyOptions() const { return m_legacyOptions; }

		/// get list of legacy enumeration values
		RED_FORCE_INLINE const TValues& GetLegacyValues() const { return m_legacyValues; }

		/// is this typed declared in scripts ?
		RED_FORCE_INLINE const Bool IsScripted() const { return m_isScripted; }

		/// reset object - used to optimize script reloading
		void Reset( const Uint32 size );

		/// add an option to the enum
		void Add( const CName name, const Int64 value );

		/// add a legacy name -- can be used when renaming an enum value (while keeping the same value), so that the old
		/// name will still load, but will not appear in editor or be used for saving.
		void AddLegacy( const CName name, const Int64 value );

		/// find a numerical value for an option with given name
		/// returns false if matching option was not found
		Bool FindValue( const CName name, Int64& outValue ) const;
		RED_INLINE Bool FindValue( const red::StringView name, Int64& outValue ) const
		{
			return FindValue( RED_NAME_NOREG( name ), outValue );
		}

		/// find name for option with given value
		/// returns false if matching option was not found
		Bool FindName( const Int64 value, CName& outName ) const;

		CName GetName( const void * data ) const;
		void SetValue( void * data, CName enumName ) const;  

	public:
		// const rtti::IType interface
		virtual const CName GetName() const override final { return m_name; }
		virtual Uint32 GetSize() const override final { return m_size; }
		virtual Uint32 GetAlignment() const override final { return 1; }
		virtual ERTTITypeType GetType() const override final { return RT_Enum; }
		virtual CName GetRefName() const override final { return m_refName; }

		virtual void Construct( void *mem ) const override final {};				 
		virtual void Destruct( void *mem ) const override final {};				 
					 
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;					 
		virtual void Copy( void* dest, const void* src ) const override final;					 
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
					 
		virtual Bool ToString( const void* object, String& valueString ) const override final;					 
		virtual Bool FromString( void* object, const String& valueString ) const override final;
					 
		virtual Bool NeedsCleaning() const override final { return false; }
		virtual red::memory::Pool & GetInnerTypeMemoryPool() const override final { return red::PoolRTTI::GetInstance(); }

	private:
		CName						m_name;
		CName						m_refName;
		Uint8						m_size;
		Uint8						m_isScripted:1;

		red::DynArray< CName >		m_options;
		red::DynArray< Int64 >		m_values;

		red::DynArray< CName >		m_legacyOptions;
		red::DynArray< Int64 >		m_legacyValues;

		void ReadInt64( const void* data, Int64& outValue ) const;
		void WriteInt64( void* data, const Int64 value ) const;

		// removed the map for now - most enums are very small
	};

} // rtti