# Unicorn2xx

This repository includes a number of command-line applications that receive data from the Unicorn Hybrid Black 8-channel EEG system, and stream the data to various other interfaces. These applications can be compiled on macOS, Linux and Windows.

Prior to the Unicorn connecting, the LED gives short flashes every second. After connecting it blinks on and off in a regular pace. When streaming the LED is constantly on. The Bluetooth protocol is documented in a PDF that is hosted on https://github.com/unicorn-bi/Unicorn-Suite-Hybrid-Black.

All of these applications stream up to 16 channels: EEG 1 to 8, Accelerometer X, Y, Z, Gyroscope X, Y, Z, Battery Level and Counter. Please note that the Unicorn Recorder and UnicornLSL application that are part of the Windows suite have a 17th channel with a Validation Indicator.

If you encounter Bluetooth connection problems on macOS, such as the LED keeps giving short flashes which indicates that it is not connecting, open a terminal and type

    sudo pkill bluetoothd

## Unicorn2txt

This streams the EEG data to the screen or to a tab-separated text file.

## Unicorn2lsl

This streams the EEG data to [LabStreamingLayer (LSL)](https://labstreaminglayer.readthedocs.io).

## Unicorn2audio

This resamples the EEG data to an audio sample rate and streams it as float32 values to a virtual (or real) audio interface. This can for example be used with [BlackHole](https://github.com/ExistentialAudio/BlackHole) or SoundFlower on macOS, or [VB-Audio Cable](https://vb-audio.com/Cable/index.htm) on Windows.

Since the float32 audio output must be scaled between -1 and +1, the `unicorn2audio` application implements a high-pass filter to remove electrode offsets and drifts. This also means that the offset and slow fluctuations in the accelerometer battery and counter channels is removed. Furthermore, it implements an automatic scaling to fit the signal amplitude between -1 and +1. The scaling is automaticallu adjusted to the most extreme values that are observed.

# Compiling

```
mkdir build
cd build
cmake ..
cmake --build .
```

# External dependencies

- <https://sigrok.org/wiki/Libserialport> for all applications
- <https://labstreaminglayer.readthedocs.io> for `unicorn2lsl`
- <http://www.portaudio.com> and <http://libsndfile.github.io/libsamplerate> for `unicorn2audio`

You can install these with your platform-specific package manager (homebrew, apt, yum), after which they will end up in `/usr/local/lib` and `/usr/local/include`. You can also install them manually in the `external` directory. In that case the directory layout should be

```
external/
├── serialport
│   ├── include
│   └── lib
├── lsl
│   ├── include
│   └── lib
├── portaudio
│   ├── include
│   └── lib
└── samplerate
    ├── include
    └── lib
```

# Alternatives

- The [Unicorn Software Suite](https://www.unicorn-bi.com/) includes a Windows-only Unicorn2lsl application that streams to LSL.
- [BrainFlow](https://brainflow.readthedocs.io/en/stable/SupportedBoards.html#unicorn) includes support for the Unicorn.
- [unicorn2lsl.py](https://robertoostenveld.nl/unicorn2lsl/) is a pure Python implementation that streams to LSL.
- [FieldTrip](https://www.fieldtriptoolbox.org/development/realtime/unicorn/) includes a pure MATLAB implementation that streams to the FieldTrip buffer.
- Here is a [GitHub repository](https://github.com/mesca/unicorn-lsl) with another alternative that is implemented in C++ (work in progress).
