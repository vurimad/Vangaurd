/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/inkCore/include/widgetResource.h"
#include "resaveCommandlet.h"

class WidgetResourceResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( WidgetResourceResaveCommandlet );

public:

	WidgetResourceResaveCommandlet();
	virtual ~WidgetResourceResaveCommandlet() override;

	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;

private:

	virtual bool OnResourcePreSave( CResource & resource ) override final;
};

RTTI_BEGIN_TYPE( WidgetResourceResaveCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

WidgetResourceResaveCommandlet::WidgetResourceResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "widgetResourceResave" ) )
{}

WidgetResourceResaveCommandlet::~WidgetResourceResaveCommandlet()
{}

const char* WidgetResourceResaveCommandlet::GetOneLiner() const
{
	return nullptr;	
}

void WidgetResourceResaveCommandlet::PrintHelp() const
{
	
}

bool WidgetResourceResaveCommandlet::OnResourcePreSave( CResource & resource )
{
	if( const THandle<ink::WidgetLibraryResource> widgetLibrary = Cast< ink::WidgetLibraryResource >( resource.HandleFromThis() ) )
	{
		// Add re-save logic here
		// return true;
	}
	
	RED_LOG_ERROR( "WidgetResourceResaveCommandlet: Resource %hs cannot be processed by widgetResourceResave commandlet", resource.GetPath().ToDebugString() );
	return false;
}
