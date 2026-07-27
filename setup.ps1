$ErrorActionPreference = "Stop"

$Repository = "https://github.com/UE4SS-RE/RE-UE4SS.git"
$PinnedCommit = "c838a8acaade1a0f860bdf249f039e58f4e10088"
$PrivateSubmoduleUrl = "https://github.com/Re-UE4SS/UEPseudo.git"
$Destination = Join-Path $PSScriptRoot "RE-UE4SS"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git was not found in PATH. Install Git for Windows first."
}

if (-not (Test-Path (Join-Path $Destination ".git"))) {
    git clone --filter=blob:none --no-checkout $Repository $Destination
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone RE-UE4SS." }
}

git -C $Destination fetch --depth 1 origin $PinnedCommit
if ($LASTEXITCODE -ne 0) { throw "Failed to fetch the pinned RE-UE4SS commit." }

git -C $Destination checkout --detach $PinnedCommit
if ($LASTEXITCODE -ne 0) { throw "Failed to check out the pinned RE-UE4SS commit." }

$SubmoduleArguments = @(
    "-C", $Destination,
    "-c", "url.https://github.com/.insteadOf=git@github.com:"
)

$UePseudoToken = [Environment]::GetEnvironmentVariable("UEPSEUDO_TOKEN")
if (-not [string]::IsNullOrWhiteSpace($UePseudoToken)) {
    $Credential = [Convert]::ToBase64String(
        [Text.Encoding]::ASCII.GetBytes("x-access-token:$UePseudoToken")
    )
    $SubmoduleArguments += @(
        "-c", "http.$PrivateSubmoduleUrl.extraheader=AUTHORIZATION: basic $Credential"
    )
}
elseif ($env:GITHUB_ACTIONS -eq "true") {
    throw "UEPSEUDO_TOKEN is not configured. Add a GitHub Actions secret containing a PAT from an account that can read Re-UE4SS/UEPseudo."
}

$SubmoduleArguments += @("submodule", "update", "--init", "--recursive", "--depth", "1")
git @SubmoduleArguments
if ($LASTEXITCODE -ne 0) { throw "Failed to initialize RE-UE4SS submodules." }

Write-Host "UE4SS source is ready at $PinnedCommit"
