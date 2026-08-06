/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "package.h"

namespace red
{
	class PackageStream;
	struct PackageTable;

	class RED_REFLECTION_API PackageLayoutSaver
	{
	public:
		PackageLayoutSaver();
		virtual ~PackageLayoutSaver();

		void WriteLayout( const Package& package, PackageStream & stream ) const;

		Uint32 GetLayoutSize( const PackageTable & table ) const;

	private:

		virtual void OnWriteUserLayout( PackageStream & stream ) const = 0;
		virtual Uint32 OnGetUserLayoutSize() const = 0;
	};

}
