# Loudness Compensation DSP for foobar2000

A plugin for the [foobar2000](https://www.foobar2000.org/) music player that implements [loudness compensation](https://en.wikipedia.org/wiki/Loudness_compensation) as a DSP.
Its goal is to improve the listening experience at lower volumes, boosting the frequencies that human hearing does not pick up as much during quieter playback.

It assumes that listening volume is mainly controlled through the in-app volume slider and the rest of the sound system (OS, external amplifier) is kept at the same amplification.
For accurate compensation, users should measure their actual listening level in dB and input it in the plugin's configuration dialog:
(Preferences -> Playback -> DSP Manager -> Loudness Compensation DSP -> [...]).

<div align="center"><img width="646" height="679" alt="Configuration" src="https://github.com/user-attachments/assets/b048c662-19e5-45b4-bbd1-f53c397f7ce1" /></div>

## How it works
The plugin first looks up [equal-loudness contours](https://en.wikipedia.org/wiki/Equal-loudness_contour) for the current and reference loudness levels, sourced from ISO 226:2023.
These are then subtracted from each other to produce a frequency-dependent gain to be applied to the input signal. This EQ is then applied to the audio signal in real-time through a
partitioned FFT convolution algorithm. The DSP adds no additional latency and requires little processing power.

The volume control inside foobar2000 is continuously monitored and the compensation EQ is adjusted whenever the volume changes. There is a slight delay to how fast the EQ can change
that's dependent on the buffer length set in foobar (Preferences -> Playback -> Output -> Buffer length).

## Dependencies
For use:
- Windows 10 or above
- foobar2000 version 1.6 or above

For build:
- Visual Studio 2019 (v142) platform toolset
- foobar2000 SDK
- [FFTConvolver](https://github.com/HiFi-LoFi/FFTConvolver)
- [pffft](https://github.com/marton78/pffft)
