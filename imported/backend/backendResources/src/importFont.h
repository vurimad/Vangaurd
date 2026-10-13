/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../backend/backendEngine/include/importer.h"


class FontImporter : public IImporter
{
	RTTI_DECLARE_TYPE( FontImporter );

public:
	FontImporter();
	
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;
};
