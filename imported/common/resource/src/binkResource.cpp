/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "binkResource.h"

RTTI_BEGIN_TYPE( Bink );
	RTTI_PARENT_TYPE( CResource );
RTTI_END_TYPE();

Bink::Bink()
{
}

void Bink::OnSerialize( IFile &file )
{
}