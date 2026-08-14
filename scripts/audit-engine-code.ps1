param(
    [string] $RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$repository = [System.IO.Path]::GetFullPath($RepositoryRoot)
$sourceRoot = Join-Path $repository "source"
$allowlistPath = Join-Path $repository `
    "configs\engine\engine-code-audit-allowlist.txt"

if (-not (Test-Path -LiteralPath $sourceRoot))
{
    throw "Vanguard source root was not found at '$sourceRoot'."
}

$allowed = @{}
if (Test-Path -LiteralPath $allowlistPath)
{
    foreach ($line in Get-Content -LiteralPath $allowlistPath)
    {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#"))
        {
            continue
        }

        $parts = $trimmed.Split("|", 3)
        if ($parts.Count -ne 3 -or
            [string]::IsNullOrWhiteSpace($parts[0]) -or
            [string]::IsNullOrWhiteSpace($parts[1]) -or
            [string]::IsNullOrWhiteSpace($parts[2]))
        {
            throw "Invalid audit allowlist entry: '$line'. Expected rule|path|reason."
        }

        $key = $parts[0].Trim() + "|" +
            $parts[1].Trim().Replace("\", "/").ToLowerInvariant()
        $allowed[$key] = $parts[2].Trim()
    }
}

$rules = @(
    @{
        Id = "crt-allocation"
        Pattern = "(?<![A-Za-z0-9_:])(?:(?:std|::)\s*::\s*)?(?:malloc|calloc|realloc|free)\s*\("
        Message = "Use Vanguard Memory or inline/arena storage."
    },
    @{
        Id = "raw-heap"
        Pattern = "(?<![A-Za-z0-9_])(?:::)?new\s+(?!\()|\bdelete\s*(?:\[\s*\])?\s+(?!=)"
        Message = "Use Vanguard Memory. Placement construction is permitted."
    },
    @{
        Id = "console-output"
        Pattern = "\bstd::(?:cout|cerr|clog|wcout|wcerr|wclog)\b|(?<![A-Za-z0-9_])(?:printf|fprintf|puts|fputs)\s*\("
        Message = "Use Vanguard Diagnostics; tests and benchmarks are excluded."
    },
    @{
        Id = "direct-red-logging"
        Pattern = "\bRED_(?:LOG|LOG_[A-Z_]+|ERROR|WARNING)\b"
        Message = "Use Vanguard Diagnostics so sinks, profiling, and editor ingestion remain unified."
    },
    @{
        Id = "owning-std"
        Pattern = "\bstd::(?:vector|deque|list|forward_list|map|multimap|set|multiset|unordered_[A-Za-z_]+|basic_string|string|wstring|u8string|u16string|u32string|unique_ptr|shared_ptr|weak_ptr|make_unique|make_shared|function|filesystem|thread|jthread|mutex|recursive_mutex|shared_mutex|condition_variable|future|promise|packaged_task|async|regex|pmr::[A-Za-z_]+)\b"
        Message = "Use the Vanguard equivalent, or add a reviewed exception proving that none exists and the facility is appropriate."
    },
    @{
        Id = "std-view"
        Pattern = "\bstd::(?:span|string_view)\b"
        Message = "Use Vanguard Containers ArraySpan/StringView unless this layer is below Containers."
    }
)

$extensions = @(".h", ".hpp", ".inl", ".c", ".cc", ".cpp")
$violations = [System.Collections.Generic.List[object]]::new()

# Third-party checkouts are import sources, never build inputs. Keeping this gate in project
# generation prevents a locally available checkout from hiding a non-reproducible dependency.
$premakeFiles = Get-ChildItem -LiteralPath $repository -Recurse -File -Filter "premake5.lua" |
    Where-Object {
        $_.FullName -notmatch "[\\/]build[\\/]" -and
        $_.FullName -notmatch "[\\/]external[\\/].*[\\/]upstream[\\/]"
    }

foreach ($file in $premakeFiles)
{
    $relative = $file.FullName.Substring($repository.TrimEnd("\").Length + 1).Replace("\", "/")
    $content = [System.IO.File]::ReadAllText($file.FullName)
    $matches = [regex]::Matches(
        $content,
        "(?i)D:[\\/]vendors\b|(?:(?:\.\.)[\\/]){2,}vendors\b")

    foreach ($match in $matches)
    {
        $line = 1 + [regex]::Matches($content.Substring(0, $match.Index), "`n").Count
        $violations.Add([pscustomobject]@{
            Rule = "external-build-input"
            File = $relative
            Line = $line
            Message = "Copy pinned third-party sources under external/ and build the repository-contained copy."
        })
    }
}

$files = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
    Where-Object {
        $extensions -contains $_.Extension -and
        $_.FullName -notmatch "[\\/](?:imported|adapted|tests|benchmarks)[\\/]"
    }

foreach ($file in $files)
{
    $relative = $file.FullName.Substring(
        $repository.TrimEnd("\").Length + 1).Replace("\", "/")
    $normalized = $relative.ToLowerInvariant()
    $content = [System.IO.File]::ReadAllText($file.FullName)

    # Preserve line count while removing comments before line-oriented checks.
    $content = [regex]::Replace(
        $content,
        "(?s)/\*.*?\*/",
        {
            param($match)
            return "`n" * ([regex]::Matches($match.Value, "`n").Count)
        })

    $lines = $content -split "`r?`n"
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; ++$lineIndex)
    {
        $code = [regex]::Replace($lines[$lineIndex], "//.*$", "")
        foreach ($rule in $rules)
        {
            if (-not [regex]::IsMatch($code, $rule.Pattern))
            {
                continue
            }

            $key = $rule.Id + "|" + $normalized
            if ($allowed.ContainsKey($key))
            {
                continue
            }

            $violations.Add([pscustomobject]@{
                Rule = $rule.Id
                File = $relative
                Line = $lineIndex + 1
                Message = $rule.Message
            })
        }
    }
}

$commentRoots = @("source", "editor", "runtime", "tools") |
    ForEach-Object { Join-Path $repository $_ } |
    Where-Object { Test-Path -LiteralPath $_ }
$commentFiles = $commentRoots |
    ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File } |
    Where-Object {
        $extensions -contains $_.Extension -and
        $_.FullName -notmatch "[\\/](?:imported|adapted|compat)[\\/]"
    }

foreach ($file in $commentFiles)
{
    $relative = $file.FullName.Substring($repository.TrimEnd("\").Length + 1).Replace("\", "/")
    $content = [System.IO.File]::ReadAllText($file.FullName)
    foreach ($comment in [regex]::Matches($content, "(?s)/\*.*?\*/|//[^`r`n]*"))
    {
        if (-not [regex]::IsMatch($comment.Value, "(?i)\bRED(?:engine)?\b|RED-style|RED-derived|RED-backed"))
        {
            continue
        }

        $line = 1 + [regex]::Matches($content.Substring(0, $comment.Index), "`n").Count
        $violations.Add([pscustomobject]@{
            Rule = "source-lineage-comment"
            File = $relative
            Line = $line
            Message = "Describe Vanguard behavior only; record implementation lineage in UPSTREAM.md or docs/migration."
        })
    }
}

# Vanguard-owned implementation directories contain translation units only. Headers that
# form a public contract live under include/vanguard/<module>; private contracts live under
# private/vanguard/<module>. Imported and mechanically adapted source trees preserve their
# upstream layout and are intentionally excluded.
$layoutRoots = @("source", "editor", "runtime", "tools") |
    ForEach-Object { Join-Path $repository $_ } |
    Where-Object { Test-Path -LiteralPath $_ }
$misplacedHeaders = $layoutRoots |
    ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File } |
    Where-Object {
        $_.Extension -in @(".h", ".hpp", ".inl") -and
        $_.FullName -match "[\\/]src[\\/]" -and
        $_.FullName -notmatch "[\\/](?:imported|adapted|external)[\\/]"
    }

foreach ($file in $misplacedHeaders)
{
    $relative = $file.FullName.Substring($repository.TrimEnd("\").Length + 1).Replace("\", "/")
    $violations.Add([pscustomobject]@{
        Rule = "header-in-source"
        File = $relative
        Line = 1
        Message = "Move public headers to include/vanguard/<module> or private headers to private/vanguard/<module>."
    })
}

$nestedIncludeDirectories = $layoutRoots |
    ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -Directory -ErrorAction SilentlyContinue } |
    Where-Object {
        $_.FullName -match "[\\/]include[\\/].*[\\/]include(?:[\\/]|$)" -and
        $_.FullName -notmatch "[\\/](?:imported|external)[\\/]"
    }

foreach ($directory in $nestedIncludeDirectories)
{
    $relative = $directory.FullName.Substring($repository.TrimEnd("\").Length + 1).Replace("\", "/")
    $violations.Add([pscustomobject]@{
        Rule = "nested-include-root"
        File = $relative
        Line = 1
        Message = "A module has one include root; namespace folders belong below it without another include directory."
    })
}

if ($violations.Count -ne 0)
{
    Write-Error "Vanguard engine-service audit failed with $($violations.Count) violation(s)."
    foreach ($violation in $violations)
    {
        Write-Host (
            "{0}({1}): [{2}] {3}" -f
            $violation.File,
            $violation.Line,
            $violation.Rule,
            $violation.Message)
    }
    exit 1
}

Write-Host "Vanguard engine-service audit passed."
