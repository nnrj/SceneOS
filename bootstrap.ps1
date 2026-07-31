$ErrorActionPreference = "Stop"
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command python3 -ErrorAction SilentlyContinue
}
if (-not $python) {
    throw "Python 3.10 or newer is required to run the project-local bootstrap."
}
& $python.Source "$PSScriptRoot\tools\scenectl.py" bootstrap @args
exit $LASTEXITCODE

