cmake_minimum_required(VERSION 3.10)

project(unicorn2xx VERSION 1.0)

add_executable(unicorn2txt unicorn2txt.c)
add_executable(unicorn2lsl unicorn2lsl.c)
add_executable(unicorn2audio unicorn2audio.c)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(/usr/local/include)
include_directories(external/lsl/include external/portaudio/include external/samplerate/include external/serialport/include)

if (UNIX)
endif()

if (WIN32)
endif()

if (APPLE)
# these are needed by the static libportaudio.a that is installed by homebrew
target_link_libraries(unicorn2audio "-framework CoreServices -framework CoreFoundation -framework AudioUnit -framework AudioToolbox -framework CoreAudio")

# these are needed by the static libserialport.a that is installed by homebrew
target_link_libraries(unicorn2txt   "-framework IOKit -framework CoreFoundation")
target_link_libraries(unicorn2lsl   "-framework IOKit -framework CoreFoundation")
target_link_libraries(unicorn2audio "-framework IOKit -framework CoreFoundation")

# this is needed for the static liblsl
target_link_libraries(unicorn2lsl c++)
endif()

# use static libraries where possible to facilitate distribution of the executable
find_library(SERIALPORT NAMES libserialport.a libserialport.lib PATHS external/serialport/lib /usr/local/lib)
find_library(PORTAUDIO NAMES libportaudio.a portaudio.lib portaudio_static_x64.lib portaudio_x64.lib PATHS external/portaudio/lib /usr/local/lib)
find_library(RESAMPLE NAMES libsamplerate.a samplerate.lib PATHS external/samplerate/lib /usr/local/lib)
find_library(LSL NAMES liblsl.a lsl.lib PATHS external/lsl/lib /usr/local/lib)

target_link_libraries(unicorn2txt   ${SERIALPORT})
target_link_libraries(unicorn2lsl   ${SERIALPORT} ${LSL})
target_link_libraries(unicorn2audio ${SERIALPORT} ${PORTAUDIO} ${RESAMPLE})
