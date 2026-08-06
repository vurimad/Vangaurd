/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace rtti
{
	class Property;
	class ClassType;

	/// Builder class for rtti property override, used with macro RTTI_PROPERTY_OVERRIDE.
	/// It is used to override some attributes of parent (base) class properties, for example we can
	/// make some inherited property from base class into a "readOnly" property, even though base class had it writable.
	/// Only subset of property attributes can be overridden, for obvious reasons "serializable" etc are not overridable.
	class RED_REFLECTION_API PropertyOverrideBuilder
	{
		RED_USE_MEMORY_POOL( red::PoolRTTIProperty );

	public:
		/// What attributes of the property override are actually modified (set)?
		enum OverrideAttribute : Uint16
		{
			OA_ReadOnly  =     RED_FLAG( 0 ),
			OA_Editable  =     RED_FLAG( 1 ),
			OA_Browsable =     RED_FLAG( 2 ),
			OA_InstanceEditable = RED_FLAG( 3 )
		};

		PropertyOverrideBuilder( const rtti::ClassType* parentClass, CName name );

		PropertyOverrideBuilder& readOnly();
		PropertyOverrideBuilder& notEditable();
		PropertyOverrideBuilder& instanceEditable();
		PropertyOverrideBuilder& browsable( Bool isBrowsable );
		RED_INLINE PropertyOverrideBuilder& tooltip( const char * tooltip );
		
		void AddPropertyOverrideToClass();

	private:
		const ClassType* m_parentClass;			//!< class that the property is in
		CName m_name;							//!< name of the property
		String m_tooltip;						//!< tooltip for the editor

		Uint64 m_flags;							//!< property flags

		Uint32 m_attributeModifiedFlags;		//< What attributes of PropertyOverride have been actually modified (set)?
	};

	RED_INLINE PropertyOverrideBuilder& PropertyOverrideBuilder::tooltip( const char* tooltip )
	{
#ifndef NO_EDITOR
		m_tooltip = tooltip;
#endif
		return *this;
	}

} // rtti
