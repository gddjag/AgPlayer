[CmdletBinding()]
param(
    [string]$OutputDirectory = "build/qa/frequency-color-fixture"
)

$ErrorActionPreference = "Stop"
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) {
    throw "Use a new output directory; this generator does not overwrite QA evidence: $output"
}
New-Item -ItemType Directory -Path $output | Out-Null
$audioPath = Join-Path $output 'frequency-color-synthetic.wav'

# Uses only the installed .NET runtime; no audio generator or model download.
if (-not ('AgPlayerFrequencyColorFixture' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;

public static class AgPlayerFrequencyColorFixture {
    public static double Write(string path) {
        const int rate = 48000;
        const int frames = rate * 24;
        double maximum = 0;
        using (var writer = new BinaryWriter(File.Create(path))) {
            writer.Write(Encoding.ASCII.GetBytes("RIFF"));
            writer.Write(36 + frames * 2);
            writer.Write(Encoding.ASCII.GetBytes("WAVEfmt "));
            writer.Write(16); writer.Write((short)1); writer.Write((short)1);
            writer.Write(rate); writer.Write(rate * 2);
            writer.Write((short)2); writer.Write((short)16);
            writer.Write(Encoding.ASCII.GetBytes("data"));
            writer.Write(frames * 2);
            for (int frame = 0; frame < frames; ++frame) {
                double t = (double)frame / rate;
                double low = Math.Sin(2 * Math.PI * 80 * t);
                double mid = Math.Sin(2 * Math.PI * 1000 * t);
                double high = Math.Sin(2 * Math.PI * 9000 * t);
                double envelope = 0.35 + 0.65 * Math.Pow(Math.Sin(Math.PI * t), 2);
                double value = 0;
                if (t >= 1 && t < 3) value = 0.70 * low * envelope;
                else if (t >= 3 && t < 5) value = 0.70 * mid * envelope;
                else if (t >= 5 && t < 7) value = 0.70 * high * envelope;
                else if (t >= 7 && t < 9) value = 0.34 * (low + mid) * envelope;
                else if (t >= 9 && t < 10) value = 0.34 * (low + high) * envelope;
                else if (t >= 10 && t < 11) value = 0.34 * (mid + high) * envelope;
                else if (t >= 11 && t < 12) value = 0.23 * (low + mid + high) * envelope;
                else if (t >= 12 && t < 14) {
                    // Isolated 30 ms decaying bursts alternate across bands.
                    int burst = (int)((t - 12) / 0.25);
                    double local = (t - 12) - burst * 0.25;
                    if (local < 0.030) {
                        double tone = burst % 3 == 0 ? low : burst % 3 == 1 ? mid : high;
                        value = 0.85 * tone * Math.Exp(-local * 100);
                    }
                    // One exact full-band, single-sample transient between bursts.
                    if (frame == rate * 12 + rate / 8) value = 0.95;
                }
                else if (t >= 16 && t < 18) value = 0.60 * low + 0.06 * mid;
                else if (t >= 18 && t < 20) value = 0.06 * low + 0.60 * mid;
                else if (t >= 20 && t < 22) value = 0.06 * mid + 0.60 * high;
                else if (t >= 22) value = 0.23 * (low + mid + high) * envelope;
                maximum = Math.Max(maximum, Math.Abs(value));
                if (Math.Abs(value) > 1) throw new InvalidOperationException("Fixture clipped");
                writer.Write((short)Math.Round(value * 32767));
            }
        }
        return maximum;
    }
}
'@
}
$maximum = [AgPlayerFrequencyColorFixture]::Write($audioPath)
$segments = @(
    @{ Start = 0; End = 1; Signal = 'silence' },
    @{ Start = 1; End = 3; Signal = '80 Hz only' },
    @{ Start = 3; End = 5; Signal = '1000 Hz only' },
    @{ Start = 5; End = 7; Signal = '9000 Hz only' },
    @{ Start = 7; End = 9; Signal = 'equal-amplitude 80 + 1000 Hz' },
    @{ Start = 9; End = 10; Signal = 'equal-amplitude 80 + 9000 Hz' },
    @{ Start = 10; End = 11; Signal = 'equal-amplitude 1000 + 9000 Hz' },
    @{ Start = 11; End = 12; Signal = 'equal-amplitude 80 + 1000 + 9000 Hz' },
    @{ Start = 12; End = 14; Signal = '30 ms decaying alternating-band bursts every 250 ms; one 0.95 single-sample impulse at 12.125 s' },
    @{ Start = 14; End = 16; Signal = 'silence' },
    @{ Start = 16; End = 18; Signal = '80 Hz dominant, 1000 Hz at 0.1 amplitude' },
    @{ Start = 18; End = 20; Signal = '1000 Hz dominant, 80 Hz at 0.1 amplitude' },
    @{ Start = 20; End = 22; Signal = '9000 Hz dominant, 1000 Hz at 0.1 amplitude' },
    @{ Start = 22; End = 24; Signal = 'equal-amplitude 80 + 1000 + 9000 Hz' }
)
$metadata = [ordered]@{
    Description = 'Deterministic synthetic audio for real decoder/provider/D3D11 waveform QA; not a music recording'
    Path = $audioPath
    Encoding = 'RIFF WAV PCM signed 16-bit little endian'
    SampleRate = 48000
    Channels = 1
    Frames = 1152000
    DurationMs = 24000
    Bytes = (Get-Item -LiteralPath $audioPath).Length
    MaximumUnquantizedAmplitude = $maximum
    Sha256 = (Get-FileHash -LiteralPath $audioPath).Hash
    SegmentsSeconds = $segments
    CaptureViewport = 'Existing capture helper: full 0-24 s and detail 10-14 s, cursor 12 s'
    Interpretation = 'Band filters have crossover leakage; equal source amplitudes are not a claim of exactly equal filtered band energy.'
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'metadata.json') -Encoding UTF8
[pscustomobject]$metadata | Select-Object Path, SampleRate, Channels, Frames, DurationMs, Bytes, Sha256
