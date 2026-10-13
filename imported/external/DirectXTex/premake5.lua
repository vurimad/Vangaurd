---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

usage "directXTex"
    filter { "platforms:x64",     "kind:not StaticLib" }
        links "lib/%{cfg.platform}.%{cfg.thirdpartycfg}/DirectXTex_MT"
    filter { "platforms:x64_DLL", "kind:not StaticLib" }
        links "lib/%{cfg.platform}.%{cfg.thirdpartycfg}/DirectXTex_MD"