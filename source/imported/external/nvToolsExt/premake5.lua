---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

usage "nvToolsExt"
    filter { "system:Windows", "kind:not StaticLib" }
        links "lib/x64.Release/nvToolsExt64_1"
    filter { "system:Windows", "kind:ConsoleApp or WindowedApp" }
        postbuildcommands '{COPY} "%{wks.location}/../../external/nvToolsExt/lib/x64.Release/nvToolsExt64_1.dll" "%{cfg.targetdir}"'