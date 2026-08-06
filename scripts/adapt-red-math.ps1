param(
    [string] $RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$sourceRoot = Join-Path $RepositoryRoot "source\imported\common\redMath"
$targetRoot = Join-Path $RepositoryRoot "source\math\adapted"

if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot "include\redMathPublic.h")))
{
    throw "The quarantined RED Math image was not found at '$sourceRoot'."
}

$resolvedRepository = [System.IO.Path]::GetFullPath($RepositoryRoot)
$resolvedTarget = [System.IO.Path]::GetFullPath($targetRoot)
if (-not $resolvedTarget.StartsWith(
    $resolvedRepository + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase))
{
    throw "Refusing to write outside the repository: '$resolvedTarget'."
}

if (Test-Path -LiteralPath $targetRoot)
{
    Remove-Item -LiteralPath $targetRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $targetRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot "include") `
    -Destination $targetRoot -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot "src") `
    -Destination $targetRoot -Recurse

$encoding = [System.Text.Encoding]::GetEncoding(1252)
$extensions = @(".h", ".hpp", ".inl", ".cpp", ".natvis", ".natstepfilter")

Get-ChildItem -LiteralPath $targetRoot -Recurse -File |
    Where-Object { $extensions -contains $_.Extension } |
    ForEach-Object {
        $content = [System.IO.File]::ReadAllText($_.FullName, $encoding)
        $content = $content.Replace("math::", "vanguard::math::")
        $content = $content.Replace(
            "using namespace math",
            "using namespace vanguard::math")
        $content = $content.Replace(
            "namespace math",
            "namespace vanguard::math")
        $content = $content.Replace("simd::", "vanguard::math::simd::")
        $content = $content.Replace(
            "using namespace simd",
            "using namespace vanguard::math::simd")
        $content = $content.Replace(
            "namespace simd",
            "namespace vanguard::math::simd")
        $content = $content.Replace(
            "../../redSystem/include/",
            "../../../imported/common/redSystem/include/")
        [System.IO.File]::WriteAllText($_.FullName, $content, $encoding)
    }

Write-Host "Adapted RED Math into '$targetRoot'."
