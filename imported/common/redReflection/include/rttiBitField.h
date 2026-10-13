/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "rttiType.h"

namespace rtti
{
	using BitFieldValuePair = std::pair< CName, Uint64 >;

	/// set of boolean on/off options
	class RED_REFLECTION_API BitFieldType : public rtti::IType
	{
	public:
		BitFieldType( const CName name, Uint32 size, const Bool isScripted );

		/// get list of enumeration options
		typedef red::DynArray< CName > TNames;
		void GetOptions( TNames& outNames ) const;

		/// is this a scripted bitfield ?
		RED_INLINE const Bool IsScripted() const { return m_isScripted; }

		/// add option to bit field
		void AddBit( const CName name, const Uint64 bitMask );

		/// get name for the given flag bit, will return empty name if bit is not defined
		const CName GetBitName( const Uint64 bitIndex ) const;

		/// get bit INDEX for given option name
		const Int32 GetBitValue( const CName name ) const;

		/// reset for reuse in scripts
		void Reset( const Uint32 size );

	public:
		/// const rtti::IType interface
		virtual const CName GetName() const override final { return m_name; }
		virtual Uint32 GetSize() const override final { return m_size; }
		virtual Uint32 GetAlignment() const { return 1; } // align to the size of the enum filed
		virtual ERTTITypeType GetType() const override final { return RT_BitField; }
		virtual CName GetRefName() const override final { return m_refName; }

		virtual void Construct( void *mem ) const override final {};
		virtual void Destruct( void *mem ) const override final {};

		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;

		virtual Bool ToString( const void* object, String& valueString ) const override final;
		virtual Bool FromString( void* object, const String& valueString ) const override final;

		virtual Bool NeedsCleaning() const override final { return false; }

		void ReadUint64( const void* data, Uint64& outValue ) const;
		void WriteUint64( void* data, const Uint64 value ) const;

	private:
		CName		m_name;
		CName		m_refName;
		Uint8		m_size:8;
		Uint8		m_isScripted:1;
		Uint64		m_usedBitMask;
		CName 		m_options[ 64 ];

	};

} // rtti