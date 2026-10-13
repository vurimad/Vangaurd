/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../backend/backendEngine/include/importer.h"

class CurveSetImporter : public IImporter
{
	RTTI_DECLARE_TYPE( CurveSetImporter );

public:
	CurveSetImporter();

public:
	virtual ImportResult DoImport( THandle< CResource > resource, const THandle<ResourceImportOptions>& options ) override;
};