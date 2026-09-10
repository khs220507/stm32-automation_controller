param([string]$Compiler = 'C:\msys64\ucrt64\bin\gcc.exe')

$ErrorActionPreference = 'Stop'
$compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
$savedPath = $env:Path
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    # GCC의 cc1/런타임 DLL을 찾도록 이 프로세스에서만 검색 경로를 추가한다.
    $env:Path = (Split-Path $compilerPath -Parent) + ';' + $savedPath
    New-Item -ItemType Directory -Path 'build/host-tests' -Force | Out-Null
    & $compilerPath -std=c11 -Wall -Wextra -Werror -IInc `
        Tests/tcp_link_host_test.c Src/tcp_link.c -o build/host-tests/tcp_link_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'TCP 호스트 시험 빌드 실패' }
    & ./build/host-tests/tcp_link_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'TCP 호스트 시험 실패' }
    & $compilerPath -std=c11 -Wall -Wextra -Werror -ITests/spi2_host -IInc `
        Tests/w5500_host_test.c -o build/host-tests/w5500_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'W5500 호스트 시험 빌드 실패' }
    & ./build/host-tests/w5500_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'W5500 호스트 시험 실패' }
    & $compilerPath -std=c11 -Wall -Wextra -Werror -ITests/spi2_host -IInc `
        Tests/spi2_host_test.c -o build/host-tests/spi2_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'SPI2 호스트 시험 빌드 실패' }
    & ./build/host-tests/spi2_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'SPI2 호스트 시험 실패' }
    & $compilerPath -std=c11 -Wall -Wextra -Werror -ITests/host -IInc `
        Tests/i2c1_host_test.c Src/i2c1.c Src/protocol.c Src/app_state.c Src/mpu6050.c Tests/host/w5500_stub.c Tests/host/tcp_link_stub.c `
        -o build/host-tests/i2c1_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw '호스트 시험 빌드 실패' }
    & ./build/host-tests/i2c1_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw '호스트 시험 실패' }
    & $compilerPath -std=c11 -Wall -Wextra -Werror -ITests/host -IInc `
        Tests/mpu6050_host_test.c Src/mpu6050.c Src/protocol.c Src/app_state.c Tests/host/w5500_stub.c Tests/host/tcp_link_stub.c `
        -o build/host-tests/mpu6050_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw '가속도 호스트 시험 빌드 실패' }
    & ./build/host-tests/mpu6050_host_test.exe
    if ($LASTEXITCODE -ne 0) { throw '가속도 호스트 시험 실패' }
}
finally {
    $env:Path = $savedPath
    Pop-Location
}
