---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

usage "fbxsdk"
    filter { "system:Windows", "kind:not StaticLib" }
        links "lib/x64.%{cfg.thirdpartycfg}/libfbxsdk"
    filter { "system:Windows", "kind:ConsoleApp or WindowedApp" }
        postbuildcommands '{COPY} "%{wks.location}/../../external/fbxsdk/lib/x64.%{cfg.thirdpartycfg}/libfbxsdk.dll" "%{cfg.targetdir}"'