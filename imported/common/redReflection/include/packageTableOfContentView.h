/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageTableOfContent.h"

namespace red
{
	struct Package;

	class RED_REFLECTION_API PackageTableOfContentView : public PackageTableOfContent
	{
	public:

		PackageTableOfContentView( const Package & package );
		virtual ~PackageTableOfContentView();

		bool IsObjectTypeValid( ObjectIndex index ) const; 

	private:

		virtual NameIndex OnMapName( CName name ) override final;
		virtual ObjectIndex OnMapObject( const ISerializable * object, const void * referenceObject ) override final;
		virtual ResourceIndex OnMapResource( const res::ResourcePath & path, PackageResourceImportType importType ) override final;

		virtual CName OnUnmapName( NameIndex index ) const override final;
		virtual void OnUnmapObject( ObjectIndex index, SerializableHandle& handle ) const override final;
		virtual res::ResourcePath OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const override final;

		virtual NameIndex OnRemapName( NameIndex ) override final;
		virtual ResourceIndex OnRemapResource( ResourceIndex, PackageResourceImportType importType ) override final;
		virtual ObjectIndex OnRemapObject( ObjectIndex index ) override final;

		const Package * m_package;
	};
}
