# calctest.ps1 -- bench client for the usfmcalc USB stream (stage 1).
#
#   .\calctest.ps1 [-Port COM7] [-Seconds 3] [-Rate 100] [-Source sim|i2c]
#
# Speaks the command channel of stream.h ([len16][addr][func][payload][crc16]),
# reads the record stream, verifies magic/CRC/seq, prints frame rate and the
# calculator's counters.  Pure PowerShell 5.1, no modules.
param(
    [string]$Port = "COM7",
    [int]$Seconds = 3,
    [int]$Rate = 100,
    [ValidateSet("sim","i2c")][string]$Source = "sim",
    [int]$PollMs = 0,
    [int]$I2cKhz = 100,
    [int]$MspRate = -1      # >= 0: set MSP430 meas_per_sec for the run, restore after
)

function Crc16([byte[]]$b, [int]$off, [int]$n) {
    $crc = 0xFFFF
    for ($i = 0; $i -lt $n; $i++) {
        $crc = $crc -bxor $b[$off + $i]
        for ($k = 0; $k -lt 8; $k++) {
            if ($crc -band 1) { $crc = ($crc -shr 1) -bxor 0xA001 } else { $crc = $crc -shr 1 }
        }
    }
    return $crc
}

# NB: PowerShell keeps [byte] -shl 8 a byte (= 0); always widen first
function U16([byte[]]$b, [int]$i) { return ([int]$b[$i]) -bor (([int]$b[$i+1]) -shl 8) }

function Frame([byte]$addr, [byte]$func, [byte[]]$payload) {
    $f = New-Object byte[] (2 + $payload.Length + 2)
    $f[0] = $addr; $f[1] = $func
    if ($payload.Length) { [Array]::Copy($payload, 0, $f, 2, $payload.Length) }
    $c = Crc16 $f 0 (2 + $payload.Length)
    $f[2 + $payload.Length] = $c -band 0xFF; $f[3 + $payload.Length] = $c -shr 8
    $out = New-Object byte[] (2 + $f.Length)
    $out[0] = $f.Length -band 0xFF; $out[1] = $f.Length -shr 8
    [Array]::Copy($f, 0, $out, 2, $f.Length)
    return $out
}

function F100Write([int]$reg, [byte[]]$data) {
    $r = -$reg
    $pl = New-Object byte[] (3 + $data.Length)
    $pl[0] = $r -band 0xFF; $pl[1] = ($r -shr 8) -band 0xFF; $pl[2] = $data.Length
    [Array]::Copy($data, 0, $pl, 3, $data.Length)
    return Frame 2 100 $pl
}
function F100Read([int]$reg, [int]$len) {
    return Frame 2 100 @([byte]($reg -band 0xFF), [byte](($reg -shr 8) -band 0xFF), [byte]$len)
}

# ---- stream parser state ----
$script:buf = New-Object byte[] 0
$script:records = 0; $script:crcBad = 0; $script:seqGaps = 0; $script:lastSeq = -1
$script:replies = New-Object System.Collections.ArrayList
$script:synth = 0; $script:dropFlag = 0; $script:firstTick = -1; $script:lastTick = -1
$script:seqA = 0; $script:seqB = 0

function Parse() {
    $b = $script:buf
    $pos = 0
    while ($true) {
        $n = $b.Length - $pos
        if ($n -lt 6) { break }
        # find 'U','S','F'
        if (-not ($b[$pos] -eq 0x55 -and $b[$pos+1] -eq 0x53 -and $b[$pos+2] -eq 0x46)) { $pos++; continue }
        if ($b[$pos+3] -eq 0x52) {           # "USFR" reply
            $len = U16 $b ($pos+4)
            if ($n -lt 6 + $len) { break }
            $fr = New-Object byte[] $len; [Array]::Copy($b, $pos+6, $fr, 0, $len)
            [void]$script:replies.Add($fr)
            $pos += 6 + $len; continue
        }
        if ($b[$pos+3] -ne 0x4D) { $pos++; continue }   # not "USFM"
        if ($n -lt 24) { break }
        $hdrLen = U16 $b ($pos+6)
        $seq = [BitConverter]::ToUInt32([byte[]]$b, $pos+8)
        $tick = [BitConverter]::ToUInt32([byte[]]$b, $pos+12)
        $frameLen = U16 $b ($pos+16)
        $flags = U16 $b ($pos+20)
        $total = $hdrLen + $frameLen + 2
        if ($n -lt $total) { break }
        $want = U16 $b ($pos+$total-2)
        if ((Crc16 $b $pos ($total-2)) -ne $want) { $script:crcBad++; $pos++; continue }
        $script:records++
        if ($script:lastSeq -ge 0 -and $seq -ne $script:lastSeq + 1) { $script:seqGaps++ }
        $script:lastSeq = $seq
        if ($flags -band 1) { $script:synth++ }
        if ($flags -band 2) { $script:dropFlag++ }
        if ($script:firstTick -lt 0) { $script:firstTick = $tick }
        $script:lastTick = $tick
        # frame form from usfmMeasureReply.frame_flags (offset 14 in the 16-byte reply)
        $ff = $b[$pos+$hdrLen+14]
        if (($ff -band 3) -eq 3) { $script:seqB++ } else { $script:seqA++ }
        $pos += $total
    }
    if ($pos -gt 0) { $script:buf = $b[$pos..($b.Length-1)]; if ($pos -eq $b.Length) { $script:buf = New-Object byte[] 0 } }
}

function Pump($p, [int]$ms) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $ms) {
        $avail = $p.BytesToRead
        if ($avail -gt 0) {
            $chunk = New-Object byte[] $avail
            $got = $p.Read($chunk, 0, $avail)
            $script:buf = $script:buf + $chunk[0..($got-1)]
            Parse
        } else { Start-Sleep -Milliseconds 2 }
    }
}

function Ask($p, [byte[]]$cmd, [int]$ms = 500) {
    $script:replies.Clear()
    $p.Write($cmd, 0, $cmd.Length)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($script:replies.Count -eq 0 -and $sw.ElapsedMilliseconds -lt $ms) { Pump $p 20 }
    if ($script:replies.Count -eq 0) { return $null }
    return $script:replies[0]
}

function Hex([byte[]]$b) { return (($b | ForEach-Object { $_.ToString("X2") }) -join " ") }

$p = New-Object System.IO.Ports.SerialPort $Port,115200,None,8,One
$p.ReadBufferSize = 1MB; $p.DtrEnable = $true; $p.Open()
Start-Sleep -Milliseconds 200; [void]$p.ReadExisting()

# stop any running stream first, then passport
$cfgOff = New-Object byte[] 2   # stream_on = 0
[void](Ask $p (F100Write 4000 $cfgOff))
$script:buf = New-Object byte[] 0

$r = Ask $p (Frame 2 17 @())
if ($r) {
    $name = [Text.Encoding]::ASCII.GetString($r, 3, 16).TrimEnd([char]0)
    $fw = [BitConverter]::ToUInt16($r, 19); $pv = [BitConverter]::ToUInt16($r, 21)
    "F17 addr2: name=$name fw=0x$($fw.ToString('X4')) proto=$pv"
} else { "F17 addr2: NO REPLY"; $p.Close(); exit 1 }

# bridge probe: F17 to the MSP430 (addr 1)
$r = Ask $p (Frame 1 17 @()) 1500
if ($r) { "F17 addr1 (via I2C): " + (Hex $r) } else { "F17 addr1: no reply" }

# optional: put the MSP430 into background measuring via the bridge
# (block 100: meas_per_sec at offset 18 -> reg 118; 0 = stop, 1..32 timer, >32 free run)
$mspOld = -1
if ($MspRate -ge 0) {
    $r = Ask $p (Frame 1 100 @([byte]100, [byte]0, [byte]40)) 1500
    if ($r -and $r[1] -eq 100) {
        $mspOld = [BitConverter]::ToUInt16($r, 2 + 18)
        "MSP block100: f1=$([BitConverter]::ToUInt32($r,2)) burst=$([BitConverter]::ToUInt16($r,2+8)) npulses=$([BitConverter]::ToUInt16($r,2+10)) sample_size=$([BitConverter]::ToUInt16($r,2+14)) meas_per_sec=$mspOld"
        $w = -118
        $r2 = Ask $p (Frame 1 100 @([byte]($w -band 0xFF), [byte](($w -shr 8) -band 0xFF), [byte]2, [byte]($MspRate -band 0xFF), [byte]($MspRate -shr 8))) 1500
        "MSP meas_per_sec := $MspRate -> " + $(if ($r2) { Hex $r2 } else { "no reply" })
    } else { "MSP block100 read failed: " + $(if ($r) { Hex $r } else { "no reply" }) }
}

# configure: stream_on=1 source sim_rate i2c_khz poll_ms
$src = if ($Source -eq "sim") { 1 } else { 0 }
$cfg = New-Object byte[] 16
$cfg[0] = 1; $cfg[2] = $src; $cfg[4] = $Rate -band 0xFF; $cfg[5] = $Rate -shr 8
$cfg[6] = $I2cKhz -band 0xFF; $cfg[7] = $I2cKhz -shr 8; $cfg[8] = $PollMs -band 0xFF; $cfg[9] = $PollMs -shr 8
$r = Ask $p (F100Write 4000 $cfg)
"cfg write reply: " + $(if ($r) { Hex $r } else { "none" })

$script:records = 0; $script:crcBad = 0; $script:seqGaps = 0; $script:lastSeq = -1; $script:firstTick = -1
$sw = [Diagnostics.Stopwatch]::StartNew()
Pump $p ($Seconds * 1000)
$sw.Stop()

# stop stream, read state
[void](Ask $p (F100Write 4000 $cfgOff))
$st = Ask $p (F100Read 4100 56)
if ($mspOld -ge 0) {
    $w = -118
    $r2 = Ask $p (Frame 1 100 @([byte]($w -band 0xFF), [byte](($w -shr 8) -band 0xFF), [byte]2, [byte]($mspOld -band 0xFF), [byte]($mspOld -shr 8))) 1500
    "MSP meas_per_sec restored to $mspOld -> " + $(if ($r2) { Hex $r2 } else { "no reply" })
}

$rate = if ($script:lastTick -gt $script:firstTick) { [math]::Round(($script:records - 1) * 1000.0 / ($script:lastTick - $script:firstTick), 1) } else { 0 }
"STREAM ${Seconds}s: records=$($script:records) crcBad=$($script:crcBad) seqGaps=$($script:seqGaps) dropFlagged=$($script:dropFlag) synth=$($script:synth) formA=$($script:seqA) formB=$($script:seqB) rate=$rate fps (device ticks)"
if ($st) {
    $o = 2
    $names = "uptime_s","frames_in","records_out","dropped","i2c_requests","i2c_ok","i2c_nack","i2c_timeout","i2c_bus","crc_err","format_err","exceptions"
    $line = @()
    for ($i = 0; $i -lt $names.Count; $i++) { $line += "$($names[$i])=$([BitConverter]::ToUInt32($st, $o + 4*$i))" }
    $line += "drdy=$([BitConverter]::ToUInt16($st, $o+48))"; $line += "last_seq=$([BitConverter]::ToUInt16($st, $o+50))"
    "STATE: " + ($line -join " ")
} else { "STATE: no reply" }
$p.Close()
