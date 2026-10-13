/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/pathResolver.h"

namespace commandlet
{
class FileLinkMap;

class BACKEND_DATA_API FileLinkResolver : public serialization::PathResolver
{
public:
	explicit FileLinkResolver( const red::HashMap< res::ResourcePath, res::ResourcePath >& linkMapping );
	explicit FileLinkResolver( const commandlet::FileLinkMap& linkMap );

	virtual res::ResourcePath ResolvePath( const res::ResourcePath& path ) override;

private:
	const red::HashMap< res::ResourcePath, res::ResourcePath >& m_linkMapping;
};

} // commandlet
