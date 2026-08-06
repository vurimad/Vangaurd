/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "package.h"

namespace red
{
	class PackageReadStream;

	class RED_REFLECTION_API PackageLayoutLoader
	{
	public:
		PackageLayoutLoader();
		virtual ~PackageLayoutLoader();

		void Initialize( red::BlobView data );

		Package Load();

	private:

		virtual void OnReadUserLayout( Uint32 version, PackageReadStream & stream ) = 0;

		red::BlobView m_data;
	};

}
