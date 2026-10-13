/*
 * Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "backendMemory.h"
#include "../../../common/core/include/message.h"

// Base resource validator interface
// Used to perform custom resource specific validation steps before submitting to version control
class BACKEND_ENGINE_API IValidator
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( IValidator );
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	virtual ~IValidator() = 0;

	virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) const;
	virtual Bool Validate( const THandle< CResource >& resource, tools::IMessageReporter& errorHandler ) const = 0;

	static red::DynArray< red::UniquePtr< IValidator > > FindValidators( const rtti::ClassType* resourceClass );

protected:
	IValidator( const rtti::ClassType* supportedResourceClass );

private:
	// Resource class that the validator supports
	const rtti::ClassType* m_supportedClass;
};