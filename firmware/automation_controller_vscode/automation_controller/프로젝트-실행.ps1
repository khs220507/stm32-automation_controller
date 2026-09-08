param([switch]$BuildOnly)

$ErrorActionPreference = 'Stop'
$firmwarePath = $PSScriptRoot
$repoPath = [IO.Path]::GetFullPath((Join-Path $firmwarePath '../../..'))
$appProject = Join-Path $repoPath 'software/UltrasonicMonitor/UltrasonicMonitor.csproj'
$appExe = Join-Path $repoPath 'software/build/UltrasonicMonitor/bin/Debug/net10.0-windows/UltrasonicMonitor.exe'
$elfPath = Join-Path $firmwarePath 'build/Debug/automation_controller.elf'
$bundlePath = Join-Path $env:LOCALAPPDATA 'stm32cube/bundles'

function Find-BundleTool([string]$Package, [string]$Executable) {
    $versions = Get-ChildItem -LiteralPath (Join-Path $bundlePath $Package) -Directory |
        Sort-Object { [version](($_.Name -split '\+')[0]) } -Descending
    foreach ($version in $versions) {
        $candidate = Join-Path $version.FullName "bin/$Executable"
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "도구를 찾을 수 없습니다: $Package/$Executable"
}

Push-Location $firmwarePath
try {
    $cmake = Find-BundleTool 'cmake' 'cmake.exe'
    $dotnet = (Get-Command dotnet -ErrorAction Stop).Source
    if (!$BuildOnly) { $programmer = Find-BundleTool 'programmer' 'STM32_Programmer_CLI.exe' }

    # 실행 파일 교체를 위해 이 프로젝트의 WPF만 정상 종료한다. 강제 종료하지 않는다.
    foreach ($app in @(Get-Process -Name UltrasonicMonitor -ErrorAction SilentlyContinue)) {
        if ($app.Path -ieq $appExe) {
            Write-Host '기존 WPF를 종료합니다.'
            if (!$app.CloseMainWindow() -or !$app.WaitForExit(5000)) {
                throw 'WPF를 직접 닫은 뒤 다시 실행하세요.'
            }
        }
    }

    # 기존 프로젝트의 Debug 구성을 사용한다. 설정이 없으면 먼저 생성한다.
    Write-Host '[1/4] STM32 Debug 빌드'
    if (!(Test-Path -LiteralPath 'build/Debug/CMakeCache.txt')) {
        & $cmake --preset Debug
        if ($LASTEXITCODE -ne 0) { throw 'CMake 설정 실패' }
    }
    & $cmake --build --preset Debug
    if ($LASTEXITCODE -ne 0) { throw 'STM32 빌드 실패' }
    if (!(Test-Path -LiteralPath $elfPath)) { throw '빌드된 ELF 파일이 없습니다.' }

    # 두 빌드가 모두 성공한 뒤에만 보드에 기록한다.
    Write-Host '[2/4] WPF Debug 빌드'
    & $dotnet build $appProject --configuration Debug --nologo
    if ($LASTEXITCODE -ne 0) { throw 'WPF 빌드 실패' }
    if (!(Test-Path -LiteralPath $appExe)) { throw 'WPF 실행 파일이 없습니다.' }
    if ($BuildOnly) {
        Write-Host '빌드 확인 완료. 보드 다운로드와 WPF 실행은 생략했습니다.'
        exit 0
    }

    Write-Host '[3/4] ST-LINK 다운로드 및 검증'
    Write-Host 'NUCLEO의 ST-LINK USB 연결과 배선 점검을 마친 상태에서 사용하세요. 디버그 세션은 종료해야 합니다.'
    & $programmer -c port=SWD mode=UR -w $elfPath -v
    if ($LASTEXITCODE -ne 0) { throw '다운로드 또는 검증 실패. ST-LINK 연결과 디버그 세션을 확인하세요.' }

    # 검증 성공 후 리셋하여 새 펌웨어를 실행한다.
    & $programmer -c port=SWD -rst
    if ($LASTEXITCODE -ne 0) { throw '보드 리셋 실패' }

    Write-Host '[4/4] WPF 실행'
    Start-Process -FilePath $appExe -WorkingDirectory (Split-Path $appExe -Parent)
    Write-Host '완료. WPF에서 COM 연결 후 SLEEP 해제·확인을 누르세요.'
}
catch {
    Write-Host "실패: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally { Pop-Location }
