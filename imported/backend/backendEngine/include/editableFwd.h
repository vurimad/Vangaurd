/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace tools
{

	class IEditableProxy;
	using EditableProxyPtr = red::SharedPtr< IEditableProxy >;
	class IEditableView;
	using EditableViewPtr = red::SharedPtr< IEditableView >;
	class IEditableCustomProperty;
	class EditableCollection;
	typedef CName ViewClass;

} // tools