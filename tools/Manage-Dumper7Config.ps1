param(
    [ValidateSet('Prepare','Restore','Status')][string]$Action = 'Status',
    [string]$GameExe,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$package = Split-Path -Parent $PSScriptRoot
$stateDirectory = Join-Path $package 'State'
$stateFile = Join-Path $stateDirectory 'Dumper7-config.json'
function FileHash([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
if ($Action -eq 'Prepare') {
    if (-not $GameExe) { throw 'Specify -GameExe with the host executable path.' }
    $exe = (Resolve-Path -LiteralPath $GameExe).Path
    if (Test-Path -LiteralPath $stateFile) {
        $old = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
        if ($old.status -eq 'prepared') { throw 'A prepared configuration exists. Restore it before preparing another.' }
    }
    if (-not $OutputDirectory) { $OutputDirectory = 'C:\Dumper-7' }
    $output = [IO.Path]::GetFullPath($OutputDirectory)
    if ($output.ToCharArray() | Where-Object { [int]$_ -gt 127 }) { throw 'Use an ASCII OutputDirectory for this Dumper-7 configuration.' }
    [IO.Directory]::CreateDirectory($output) | Out-Null
    [IO.Directory]::CreateDirectory($stateDirectory) | Out-Null
    $config = Join-Path (Split-Path -Parent $exe) 'Dumper-7.ini'
    $state = [ordered]@{status='preparing'; game_exe=$exe; config=$config; output=$output; prior='absent'; backup=$null; prior_sha256=$null; installed_sha256=$null}
    if (Test-Path -LiteralPath $config) {
        $backup = Join-Path $stateDirectory ('Dumper-7.original-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff') + '.ini')
        Copy-Item -LiteralPath $config -Destination $backup
        $state.prior='present'; $state.backup=$backup; $state.prior_sha256=FileHash $backup
    }
    $outputIni = $output.Replace('\','/')
    $text = "[Settings]`r`nSDKNamespaceName=PBZ`r`nSDKGenerationPath=$outputIni`r`nLogFilePath=`r`nSleepTimeout=10`r`nShouldAutoUnload=1`r`nExecuteSDKTestScript=0`r`n"
    [IO.File]::WriteAllText($config, $text, [Text.Encoding]::ASCII)
    $state.installed_sha256=FileHash $config; $state.status='prepared'
    $state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding UTF8
    $state | ConvertTo-Json
} elseif ($Action -eq 'Restore') {
    if (-not (Test-Path -LiteralPath $stateFile)) { throw 'No recorded configuration to restore.' }
    $state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    if ($state.status -ne 'prepared') { throw 'The recorded configuration is not in prepared state.' }
    if (-not (Test-Path -LiteralPath $state.config)) { throw 'Prepared configuration is missing; no changes made.' }
    if ((FileHash $state.config) -ne $state.installed_sha256) { throw 'Prepared configuration changed; preserving the current file.' }
    if ($state.prior -eq 'present') {
        if ((FileHash $state.backup) -ne $state.prior_sha256) { throw 'Backup hash mismatch; no changes made.' }
        Copy-Item -LiteralPath $state.backup -Destination $state.config -Force
        if ((FileHash $state.config) -ne $state.prior_sha256) { throw 'Restored hash mismatch.' }
    } else { Remove-Item -LiteralPath $state.config }
    $state.status='restored'
    $state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding UTF8
    $state | ConvertTo-Json
} else {
    if (Test-Path -LiteralPath $stateFile) { Get-Content -LiteralPath $stateFile -Raw }
    else { Write-Output 'No package-managed configuration exists.' }
}
