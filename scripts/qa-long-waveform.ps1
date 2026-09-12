param([string]$OutputDirectory = 'build/qa/waveform-142s')
$ErrorActionPreference = 'Stop'
$qaRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../$OutputDirectory"))
New-Item -ItemType Directory -Path $qaRoot -Force | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class LongWaveformFixture {
    public static void Write(string path) {
        const int rate = 16000, frames = rate * 180;
        using (var writer = new BinaryWriter(File.Create(path))) {
            writer.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));
            writer.Write(36 + frames * 2);
            writer.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt "));
            writer.Write(16); writer.Write((short)1); writer.Write((short)1);
            writer.Write(rate); writer.Write(rate * 2);
            writer.Write((short)2); writer.Write((short)16);
            writer.Write(System.Text.Encoding.ASCII.GetBytes("data"));
            writer.Write(frames * 2);
            for (int i = 0; i < frames; i++) {
                double t = (double)i / rate;
                double beat = 0.25 + 0.6 * Math.Exp(-8 * (t % 0.5));
                double section = 0.45 + 0.55 * Math.Abs(Math.Sin(t * 0.07));
                double sample = beat * section * (0.65 * Math.Sin(t * 2 * Math.PI * 220)
                    + 0.25 * Math.Sin(t * 2 * Math.PI * 1300));
                writer.Write((short)(sample * 28000));
            }
        }
    }
}
'@
[LongWaveformFixture]::Write((Join-Path $qaRoot 'three-minute-tail.wav'))
Write-Output (Join-Path $qaRoot 'three-minute-tail.wav')
