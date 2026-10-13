/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "commentNode.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(CommentNode, tools);
RTTI_PARENT_TYPE(ISerializable);
RTTI_DESCRIPTIVE_NAME( Comment );
RTTI_PROPERTY(m_title).editable();
RTTI_PROPERTY(m_comment).editable();
RTTI_END_TYPE();
