/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "generator.h"
#include "../../../common/redJobs2/include/jobBuilder.h"

RTTI_BEGIN_ABSTRACT_TYPE( IGenerator );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE( GeneratorAreaDescriptorInterface );
RTTI_END_TYPE();

IGenerator::~IGenerator() = default;
GeneratorAreaDescriptorInterface::~GeneratorAreaDescriptorInterface() = default;

bool IGenerator::InitialiseEnvironment( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const
{
	return true;
}

CName IGenerator::GetName() const
{
	return this->GetClass()->GetName();
}

data::CacheStrategy IGenerator::GetCacheStrategy() const
{
	return data::CacheStrategy::GET_NEVER;
}

Uint32 IGenerator::GetBuildSettings() const
{
	return data::BuildSettings::CLIENT | data::BuildSettings::SERVER;
}

data::DepotLocation IGenerator::GetOutputDepot() const
{
	return data::DepotLocation::Source;
}

namespace data
{
namespace prv
{

BACKEND_DATA_API red::DynArray< IGenerator* > EnumerateGenerators()
{
	red::DynArray< const rtti::ClassType* > generatorClasses;
	GetRttiSystem().EnumClasses( ClassID<IGenerator>(), generatorClasses );

	red::DynArray< IGenerator* > generators;
	generators.Reserve( generatorClasses.Size() );

	for ( const rtti::ClassType* generatorClass : generatorClasses )
	{
		IGenerator* generator = generatorClass->GetDefaultObject< IGenerator >();
		if ( generator )
		{
			generators.PushBack( generator );
		}
	}

	return generators;
}

} // prv
} // data

red::UniquePtr< IGenerator > IGenerator::Create( const res::ResourcePath& path )
{
	static red::DynArray< IGenerator* > generators{ data::prv::EnumerateGenerators() };

	for ( IGenerator* generator : generators )
	{
		if ( generator->CanGenerate( path ) )
		{
			// Create a new instance of the object here.
			return red::UniquePtr< IGenerator >( generator->GetClass()->CreateObject< IGenerator >() );
		}
	}

	return nullptr;
}
