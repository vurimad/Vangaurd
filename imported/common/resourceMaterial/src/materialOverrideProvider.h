/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class IMaterial;

/// Material override context
class SMaterialOverrideContext
{
public:
	IMaterial*	m_originalMaterial;
	Uint32		m_chunkIndex;

public:
	SMaterialOverrideContext ();
};


/// Material override provider
class IMaterialOverrideProvider
{
public:
	virtual ~IMaterialOverrideProvider () 
	{
		// empty
	}

	/// Overrides given material (returns provided material in case of no override).
	virtual IMaterial* OverrideMaterial( const SMaterialOverrideContext &context )=0;
};
