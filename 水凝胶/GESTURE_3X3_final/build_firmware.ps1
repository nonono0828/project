param(
    [string]$Toolchain = 'C:\Keil_v5\ARM\ARMCC\bin'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$OutputDir = Join-Path $ProjectRoot 'Build'
$ArmCc = Join-Path $Toolchain 'armcc.exe'
$ArmAsm = Join-Path $Toolchain 'armasm.exe'
$ArmLink = Join-Path $Toolchain 'armlink.exe'
$FromElf = Join-Path $Toolchain 'fromelf.exe'

foreach ($tool in @($ArmCc, $ArmAsm, $ArmLink, $FromElf)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "ARMCC tool not found: $tool"
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$Includes = @(
    '-IUSER',
    '-ICORE',
    '-IHARDWARE\LED',
    '-IHARDWARE\OLED',
    '-IHARDWARE\BUTTON4_4',
    '-ISTM32F10x_FWLib\inc',
    '-ISYSTEM\delay',
    '-ISYSTEM\sys',
    '-ISYSTEM\usart'
)

$Common = @(
    '--cpu', 'Cortex-M3',
    '--c99',
    '--apcs=interwork',
    '--library_type=microlib',
    '--split_sections',
    '-O1',
    '--debug',
    '-DSTM32F10X_MD',
    '-DUSE_STDPERIPH_DRIVER'
) + $Includes

$Sources = [ordered]@{
    'main' = 'USER\main.c'
    'stm32f10x_it' = 'USER\stm32f10x_it.c'
    'system_stm32f10x' = 'USER\system_stm32f10x.c'
    'led' = 'HARDWARE\LED\led.c'
    'oled' = 'HARDWARE\OLED\oled.c'
    'button4_4' = 'HARDWARE\BUTTON4_4\button4_4.c'
    'delay' = 'SYSTEM\delay\delay.c'
    'usart' = 'SYSTEM\usart\usart.c'
    'sys' = 'SYSTEM\sys\sys.c'
    'core_cm3' = 'CORE\core_cm3.c'
    'misc' = 'STM32F10x_FWLib\src\misc.c'
    'stm32f10x_usart' = 'STM32F10x_FWLib\src\stm32f10x_usart.c'
    'stm32f10x_gpio' = 'STM32F10x_FWLib\src\stm32f10x_gpio.c'
    'stm32f10x_rcc' = 'STM32F10x_FWLib\src\stm32f10x_rcc.c'
    'stm32f10x_adc' = 'STM32F10x_FWLib\src\stm32f10x_adc.c'
}

$Objects = @()
Push-Location $ProjectRoot
try {
    foreach ($entry in $Sources.GetEnumerator()) {
        $object = Join-Path $OutputDir ($entry.Key + '.o')
        & $ArmCc @Common -c $entry.Value -o $object
        if ($LASTEXITCODE -ne 0) {
            throw "Compile failed: $($entry.Value)"
        }
        $Objects += $object
    }

    $StartupObject = Join-Path $OutputDir 'startup_stm32f10x_md.o'
    & $ArmAsm --cpu Cortex-M3 --apcs=interwork --pd '__MICROLIB SETA 1' --debug `
        'CORE\startup_stm32f10x_md.s' -o $StartupObject
    if ($LASTEXITCODE -ne 0) {
        throw 'Startup assembly failed.'
    }
    $Objects = @($StartupObject) + $Objects

    $Axf = Join-Path $OutputDir 'GESTURE_3X3.axf'
    $Map = Join-Path $OutputDir 'GESTURE_3X3.map'
    & $ArmLink --cpu Cortex-M3 --library_type=microlib --strict --map --xref --symbols `
        --info sizes,totals,unused --list $Map --entry Reset_Handler `
        --keep 'startup_stm32f10x_md.o(RESET)' --first 'startup_stm32f10x_md.o(RESET)' `
        --ro-base 0x08000000 --rw-base 0x20000000 `
        -o $Axf @Objects
    if ($LASTEXITCODE -ne 0) {
        throw 'Link failed.'
    }

    $Hex = Join-Path $OutputDir 'GESTURE_3X3.hex'
    $Bin = Join-Path $OutputDir 'GESTURE_3X3.bin'
    & $FromElf --i32combined --output $Hex $Axf
    if ($LASTEXITCODE -ne 0) {
        throw 'HEX generation failed.'
    }
    & $FromElf --bin --output $Bin $Axf
    if ($LASTEXITCODE -ne 0) {
        throw 'BIN generation failed.'
    }

    & $FromElf --text -z $Axf
    if ($LASTEXITCODE -ne 0) {
        throw 'Size report failed.'
    }
}
finally {
    Pop-Location
}
