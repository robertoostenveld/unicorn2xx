# Unicorn2xx

This repository includes a number of command-line applications that receive data from the Unicorn Hybrid Black 8-channel EEG system, and stream the data to various other interfaces. These applications can be compiled on macOS, Linux ann Windows.

## Unicorn2txt

This streams the EEG data to the screen or a tab-separated text file.

## Unicorn2ft

This streams the EEG data to the [FieldTrip buffer](https://www.fieldtriptoolbox.org/development/realtime/).

## Unicorn2lsl

This streams the EEG data to [LabStreamingLayer (LSL)](https://labstreaminglayer.readthedocs.io).

## Unicorn2audio

This resamples the EEG data to an audio sample rate and streams it to a virtual (or real) audio interface. This can for example be used with [BlackHole](https://github.com/ExistentialAudio/BlackHole) or SoundFlower on macOS, or [VB-Audio Cable](https://vb-audio.com/Cable/index.htm) on Windows.

# External dependencies

- <https://sigrok.org/wiki/Libserialport>
- <https://labstreaminglayer.readthedocs.io>
- <http://libsndfile.github.io/libsamplerate/>
- <http://www.portaudio.com>

# Alternatives

- The [Unicorn Software Suite](https://www.unicorn-bi.com/) includes a Windows-only Unicorn2lsl application that streams to LSL.
- [BrainFlow](https://brainflow.readthedocs.io/en/stable/SupportedBoards.html#unicorn) includes support for the Unicorn.
- [unicorn2lsl.py](https://robertoostenveld.nl/unicorn2lsl/) is a pure Python implementation that streams to LSL.
- [FieldTrip](https://www.fieldtriptoolbox.org/development/realtime/unicorn/) includes a pure MATLAB implementation that streams to the FieldTrip buffer.
- Here is a [GitHub repository](https://github.com/mesca/unicorn-lsl) with another alternative that is implemented in C++ (work in progress).
