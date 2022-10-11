# Unicorn2xx

This includes a number of command-line applications that receive data from the Unicorn Hybrid Black 8-channel EEG system, and stream the data to various other interfaces.

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

