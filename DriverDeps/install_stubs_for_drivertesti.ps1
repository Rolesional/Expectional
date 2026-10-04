# drivertesti.hpp icindeki mutlak include yollarina stub kopyalar (bir kez calistirin).
$ErrorActionPreference = "Continue"
$root = Join-Path $PSScriptRoot "stubs"
$targets = @(
    @{ Src = "skStr.h"; Dest = "C:\Users\asdas\Desktop\USERMODE\UM\skStr.h" },
    @{ Src = "crypt.h"; Dest = "C:\Users\asdas\Desktop\USERMODE\UM\framework\encryption\crypt.h" }
)
foreach ($t in $targets) {
    $src = Join-Path $root $t.Src
    if (-not (Test-Path $src)) { Write-Warning "Missing $src"; continue }
    $dest = $t.Dest
    $dir = Split-Path -Parent $dest
    try {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Copy-Item -LiteralPath $src -Destination $dest -Force
        Write-Host "Copied to $dest"
    }
    catch {
        Write-Warning $_.Exception.Message
    }
}
