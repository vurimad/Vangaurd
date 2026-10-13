/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "validator.h"

RTTI_BEGIN_ABSTRACT_TYPE( IValidator );
RTTI_END_TYPE();

IValidator::IValidator( const rtti::ClassType* supportedResourceClass )
	: m_supportedClass( supportedResourceClass )
{
}

IValidator::~IValidator() = default;

bool IValidator::SupportsResource( const rtti::ClassType* resourceClass ) const
{
	return resourceClass->IsA( m_supportedClass );
}

red::DynArray< red::UniquePtr< IValidator > > IValidator::FindValidators( const rtti::ClassType* resourceClass )
{
	red::DynArray< const rtti::ClassType* > validatorClasses;
	GetRttiSystem().EnumClasses( ClassID<IValidator>(), validatorClasses );

	red::DynArray< red::UniquePtr< IValidator > > validators;
	validators.Reserve( validatorClasses.Size() ); // worst case reservation

	for ( const rtti::ClassType* validatorClass : validatorClasses )
	{
		IValidator* defaultValidator = validatorClass->GetDefaultObject< IValidator >();
		if ( defaultValidator->SupportsResource( resourceClass ) )
		{
			validators.PushBack( red::UniquePtr< IValidator >( validatorClass->CreateObject< IValidator >() ) );
		}
	}

	return validators;
}
