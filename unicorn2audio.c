/*
 * This application reads EEG data from the Unicorn from a serial-over-bluetooth device
 * upsamples it to an audio sampling rate and writes it to a (virtual) audio device. When
 * used with SoudFlower, BlackHole or VB-Audio Network, this can be used to process EEG
 * data in Ableton Live, Max/MSP or PureData.
 *
 * The unicorn has 8+3+3+1+1=16 channels, but here we will only use the 8 EEG channels.
 * The float32 audio output needs to be scaled between -1 and 1, and the magnitude of the
 * different channel types varies too much to come up with a good scaling for all.
 *
 *
 * Copyright (C) 2022, Robert Oostenveld
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <signal.h>

#include "libserialport.h"
#include "portaudio.h"
#include "samplerate.h"

#if defined __linux__ || defined __APPLE__
// Linux and macOS code goes here
#include <unistd.h>
#define min(x, y) ((x)<(y) ? x : y)
#define max(x, y) ((x)>(y) ? x : y)
#elif defined _WIN32
// Windows code goes here
#endif

/* Helper function for serial port error handling. */
int sp_check(enum sp_return result);

/* Helper functions for stopping neatly. */
void signal_handler(int signum);
void stream_finished(void *userData);

/* Helper function to read and parse one sample. */
int unicorn_pull_sample(struct sp_port *port, float *dat);

/* Helper function for low-pass filtering. */
#define smooth(old, new, lambda) ((1.0-lambda)*(old) + (lambda)*(new))

#define SAMPLETYPE    paFloat32
#define BLOCKSIZE     (0.01)  // in seconds
#define BUFFERSIZE    (2.00)  // in seconds
#define DEFAULTRATE   (44100.0)
#define FSAMPLE       (250)
#define NCHAN         (16)
#define STRLEN        (80)
#define PACKETSIZE    (45)
#define TIMEOUT       (5000)

char start_acq[]      = {0x61, 0x7C, 0x87};
char stop_acq[]       = {0x63, 0x5C, 0xC5};
char start_response[] = {0x00, 0x00, 0x00};
char stop_response[]  = {0x00, 0x00, 0x00};
char start_sequence[] = {0xC0, 0x00};
char stop_sequence[]  = {0x0D, 0x0A};

struct sp_port *port = NULL;
int keepRunning = 1;

typedef struct {
        float *data;
        unsigned long frames;
} dataBuffer_t;

dataBuffer_t inputData, outputData;

SRC_STATE* resampleState = NULL;
SRC_DATA resampleData;
int srcErr;

float inputRate, outputRate, resampleRatio;
short enableResample = 0, enableUpdate = 0;
int channelCount, outputBlocksize, inputBufsize, outputBufsize;
float outputLimit = 1.;

/*******************************************************************************************************/
int resample_buffers(void) {
        resampleData.src_ratio      = resampleRatio;
        resampleData.end_of_input   = 0;
        resampleData.data_in        = inputData.data;
        resampleData.input_frames   = inputData.frames;
        resampleData.data_out       = outputData.data + outputData.frames * channelCount;
        resampleData.output_frames  = outputBufsize - outputData.frames;

        /* check whether there is data in the input buffer */
        if (inputData.frames==0)
                return 0;

        /* check whether there is room for new data in the output buffer */
        if (outputData.frames==outputBufsize)
                return 0;

        int srcErr = src_process (resampleState, &resampleData);
        if (srcErr)
        {
                printf("ERROR: Cannot resample the input data\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                exit(srcErr);
        }

        /* the output data buffer increased */
        outputData.frames += resampleData.output_frames_gen;

        /* the input data buffer decreased */
        size_t len = (inputData.frames - resampleData.input_frames_used) * channelCount * sizeof(float);
        memcpy(inputData.data, inputData.data + resampleData.input_frames_used * channelCount, len);
        inputData.frames -= resampleData.input_frames_used;

        return 0;
}

/*******************************************************************************************************/
int update_ratio(void) {
        float nominal = (float)outputRate/inputRate;
        float estimate = nominal + (0.5*outputBufsize - outputData.frames) / outputBlocksize;

        /* do not change the ratio by too much */
        estimate = min(estimate, 1.2*nominal);
        estimate = max(estimate, 0.8*nominal);

        /* allow some variation of the target buffer size */
        /* it should fall between the lower and upper range */
        float verylow   = (0.40*outputBufsize);
        float low       = (0.48*outputBufsize);
        float high      = (0.52*outputBufsize);
        float veryhigh  = (0.60*outputBufsize);

        /* this is called every 0.01 seconds, hence lambda=1.0*BLOCKSIZE implements a 1 second smoothing
           and 10*BLOCKSIZE implements a 0.1 second smoothing */
        if (outputData.frames<verylow)
                resampleRatio = smooth(resampleRatio, estimate, 10. * BLOCKSIZE);
        else if (outputData.frames<low)
                resampleRatio = smooth(resampleRatio, estimate, 1. * BLOCKSIZE);
        else if (outputData.frames>high)
                resampleRatio = smooth(resampleRatio, estimate, 1. * BLOCKSIZE);
        else if (outputData.frames>veryhigh)
                resampleRatio = smooth(resampleRatio, estimate, 10. * BLOCKSIZE);
        else
                resampleRatio = smooth(resampleRatio, nominal, 10. * BLOCKSIZE);

        // printf("%lu\t%f\t%f\t%f\n", outputData.frames, nominal, estimate, resampleRatio);

        return 0;
}

/*******************************************************************************************************/
static int output_callback(const void *input,
                           void *output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData)
{
        float *data = (float *)output;
        dataBuffer_t *outputData = (dataBuffer_t *)userData;
        unsigned int newFrames = min(frameCount, outputData->frames);

        size_t len = newFrames * channelCount * sizeof(float);
        memcpy(data, outputData->data, len);

        len = (frameCount - newFrames) * channelCount * sizeof(float);
        memset(data + newFrames * channelCount, 0, len);

        len = (outputData->frames - newFrames) * channelCount * sizeof(float);
        memcpy(outputData->data, outputData->data + newFrames * channelCount, len);

        outputData->frames -= newFrames;

        for (unsigned int i = 0; i < (newFrames * channelCount); i++)
                outputLimit = max(outputLimit, fabsf(data[i]));

        if (enableResample)
                resample_buffers();

        if (enableUpdate)
                update_ratio();

        return paContinue;
}

/*******************************************************************************************************/
int main(int argc, char **argv)
{
        char line[STRLEN];
        FILE *fp;
        int inputDevice = 0;
        float bufferSize, blockSize;
        struct sp_port **port_list = NULL;
        float eegdata[NCHAN], eegfilt[NCHAN];
        float lambda = 0.0002772; // 0.9997228; // this implements a 10-second exponential decay at 250 Hz
        unsigned long samplesReceived = 0;

        /* variables that are specific for PortAudio */
        unsigned int outputDevice;
        PaStream *outputStream;
        PaStreamParameters outputParameters;
        PaError paErr = paNoError;
        unsigned int numDevices;
        const PaDeviceInfo *deviceInfo;

        /* STAGE 1: Initialize the input serial port. */

        printf("Getting port list.\n");
        sp_check(sp_list_ports(&port_list));

        /* Iterate through the ports. When port_list[i] is NULL
         * this indicates the end of the list. */
        for (int i = 0; port_list[i] != NULL; i++) {
                struct sp_port *port = port_list[i];

                /* Get the name of the port. */
                char *port_name = sp_get_port_name(port);
                char *port_description = sp_get_port_description(port);

                /* try to identify the serial port with a name or description like UN-20211209 */
                if (strstr(port_name, "UN")!=0 || strstr(port_description, "UN")!=0)
                        inputDevice = i;

                printf("port %d: %s\n", i, port_name);
        }

        printf("Select serial port [%d]: ", inputDevice);
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                inputDevice = inputDevice;
        else
                inputDevice = atoi(line);

        printf("Buffer size in seconds [%.4f]: ", BUFFERSIZE);
        fgets(line, STRLEN, stdin);
        if (strlen(line) == 1)
                bufferSize = BUFFERSIZE;
        else
                bufferSize = atof(line);

        printf("Block size in seconds [%.4f]: ", BLOCKSIZE);
        fgets(line, STRLEN, stdin);
        if (strlen(line) == 1)
                blockSize = BLOCKSIZE;
        else
                blockSize = atof(line);

        /* copy the selected port, clear the others */
        sp_check(sp_copy_port(port_list[inputDevice], &port));
        sp_free_port_list(port_list);

        printf("Opening port %s (%s).\n", sp_get_port_name(port), sp_get_port_description(port));
        sp_check(sp_open(port, SP_MODE_READ_WRITE));

        printf("Setting port to 115200, 8N1, no flow control.\n");
        sp_check(sp_set_baudrate(port, 115200));
        sp_check(sp_set_bits(port, 8));
        sp_check(sp_set_parity(port, SP_PARITY_NONE));
        sp_check(sp_set_stopbits(port, 1));
        sp_check(sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE));

        inputRate = FSAMPLE;
        inputBufsize = bufferSize * inputRate;

        /* STAGE 2: Initialize the output audio port. */

        printf("PortAudio version: 0x%08X\n", Pa_GetVersion());

        /* Initialize library before making any other calls. */
        paErr = Pa_Initialize();
        if(paErr != paNoError) {
                printf("ERROR: Cannot initialize PortAudio.\n");
                printf("ERROR: %s\n", Pa_GetErrorText(paErr));
                goto cleanup2;
        }

        numDevices = Pa_GetDeviceCount();
        if (numDevices <= 0) {
                printf("ERROR: No audio devices available.\n");
                paErr = numDevices;
                goto cleanup2;
        }

        printf("Number of host APIs = %d\n", Pa_GetHostApiCount());
        printf("Number of devices = %d\n", numDevices);
        for (int i = 0; i < numDevices; i++) {
                deviceInfo = Pa_GetDeviceInfo(i);
                if (Pa_GetHostApiCount() == 1)
                        printf("device %2d - %s (%d in, %d out)\n", i,
                               deviceInfo->name,
                               deviceInfo->maxInputChannels,
                               deviceInfo->maxOutputChannels);
                else
                        printf("device %2d - %s - %s (%d in, %d out)\n", i,
                               Pa_GetHostApiInfo(deviceInfo->hostApi)->name,
                               deviceInfo->name,
                               deviceInfo->maxInputChannels,
                               deviceInfo->maxOutputChannels);
        }
        printf("Select output device [%d]: ", Pa_GetDefaultOutputDevice());
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                outputDevice = Pa_GetDefaultOutputDevice();
        else
                outputDevice = atoi(line);

        printf("Output sampling rate [%.0f]: ", DEFAULTRATE);
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                outputRate = DEFAULTRATE;
        else
                outputRate = atof(line);

        channelCount = 8; // the automatic scaling messes up when using all 16 channels
        deviceInfo = Pa_GetDeviceInfo(outputDevice);
        printf("Number of channels [%d]: ", min(channelCount, deviceInfo->maxOutputChannels));
        fgets(line, STRLEN, stdin);
        if (strlen(line) == 1)
                channelCount = min(channelCount, deviceInfo->maxOutputChannels);
        else
                channelCount = min(channelCount, atoi(line));

        printf("outputDevice = %d\n", outputDevice);
        printf("outputRate = %f\n", outputRate);
        printf("channelCount = %d\n", channelCount);

        outputParameters.device = outputDevice;
        outputParameters.channelCount = channelCount;
        outputParameters.sampleFormat = SAMPLETYPE;
        outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = NULL;

        outputBufsize = bufferSize * outputRate;
        outputBlocksize = blockSize * outputRate;

        paErr = Pa_OpenStream(
                &outputStream,
                NULL,
                &outputParameters,
                outputRate,
                outputBlocksize,
                paNoFlag,
                output_callback,
                &outputData);
        if(paErr != paNoError)
        {
                printf("ERROR: Cannot open output stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText(paErr));
                goto cleanup2;
        }

        printf("Opened output stream with %d channels at %.0f Hz.\n", channelCount, outputRate);

        /* ensure a gracefull exit when the audio stream is closed */
        Pa_SetStreamFinishedCallback(&outputStream, stream_finished);

        /* STAGE 3: Initialize the resampling. */

        inputData.frames = 0;
        inputData.data = NULL;
        if ((inputData.data = malloc(inputBufsize * channelCount * sizeof(float))) == NULL)
                goto cleanup3;
        else
                memset(inputData.data, 0, inputBufsize * channelCount * sizeof(float));

        outputData.frames = 0;
        outputData.data = NULL;
        if ((outputData.data = malloc(outputBufsize * channelCount * sizeof(float))) == NULL)
                goto cleanup3;
        else
                memset(outputData.data, 0, outputBufsize * channelCount * sizeof(float));

        printf("Setting up %s rate converter with %s\n",
               src_get_name (SRC_SINC_MEDIUM_QUALITY),
               src_get_description (SRC_SINC_MEDIUM_QUALITY));

        resampleState = src_new (SRC_SINC_MEDIUM_QUALITY, channelCount, &srcErr);
        if (resampleState == NULL) {
                printf("ERROR: Cannot set up resample state.\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                goto cleanup3;
        }

        /* STAGE 4: Start the streams. */

        unsigned char *buf = malloc(PACKETSIZE);
        memset(buf, 0, PACKETSIZE);

        if (sp_blocking_write(port, start_acq, 3, TIMEOUT)!=3) {
                printf("Cannot start data stream.\n");
                goto cleanup4;
        }

        int result = sp_blocking_read(port, buf, 3, TIMEOUT);
        if (result!=3 || buf[0]!=0x00 || buf[1]!=0x00 || buf[2]!=0x00) {
                printf("Incorrect response.\n");
                goto cleanup4;
        }

        printf("Started data stream.\n");

        signal(SIGINT, signal_handler);

        printf("Flushing initial data...\n");

        /* discard the first few seconds, this tends to have weird values */
        while (samplesReceived<5*FSAMPLE) {
                if (unicorn_pull_sample(port, eegdata)!=0) {
                        printf("Cannot read packet.\n");
                        goto cleanup4;
                }
                samplesReceived++;
        }
        samplesReceived = 0;

        if (unicorn_pull_sample(port, eegdata)!=0) {
                printf("Cannot read packet.\n");
                goto cleanup4;
        };

        /* initialize a lowpass filter */
        for (int i=0; i<channelCount; i++)
                eegfilt[i] = eegdata[i];

        /* apply a lowpass filter */
        for (int i=0; i<channelCount; i++)
                eegdata[i] -= smooth(eegfilt[i], eegdata[i], lambda);

        printf("Filling buffer...\n");

        /* wait one second to fill the input buffer halfway */
        while (samplesReceived<inputBufsize/2)
        {
                if (unicorn_pull_sample(port, eegdata)!=0) {
                        printf("Cannot read packet.\n");
                        goto cleanup4;
                }
                samplesReceived++;

                /* apply a lowpass filter */
                for (int i=0; i<channelCount; i++)
                        eegdata[i] -= smooth(eegfilt[i], eegdata[i], lambda);

                /* add the current sample to the input buffer and increment the counter */
                for (unsigned int i = 0; i < channelCount; i++) {
                        outputLimit = max(outputLimit, fabsf(eegdata[i]));
                        inputData.data[inputData.frames * channelCount + i] = eegdata[i] / outputLimit;
                }
                inputData.frames++;
        }

        resampleRatio = outputRate / inputRate;
        printf("Initial resampleRatio = %f\n", resampleRatio);

        srcErr = src_set_ratio (resampleState, resampleRatio);
        if (srcErr) {
                printf("ERROR: Cannot set resampling ratio.\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                goto cleanup4;
        }

        paErr = Pa_StartStream(outputStream);
        if(paErr != paNoError) {
                printf("ERROR: Cannot start output audio stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText(paErr));
                goto cleanup4;
        }

        printf("Started output audio stream.\n");

        /* enable the resampling and the dynamic updating of the samplerate ratio */
        enableResample = 1;
        enableUpdate = 1;
        keepRunning = 1;

        printf("Processing data...\n");

        while (keepRunning) {
                if (unicorn_pull_sample(port, eegdata)!=0) {
                        printf("Cannot read packet.\n");
                        goto cleanup4;
                }
                samplesReceived++;

                /* apply a lowpass filter */
                for (int i=0; i<channelCount; i++)
                        eegdata[i] -= smooth(eegfilt[i], eegdata[i], lambda);

                /* add the current sample to the input buffer and increment the counter */
                for (unsigned int i = 0; i < channelCount; i++) {
                        outputLimit = max(outputLimit, fabsf(eegdata[i]));
                        inputData.data[inputData.frames * channelCount + i] = eegdata[i] / outputLimit;
                }
                inputData.frames++;

                if ((samplesReceived % FSAMPLE)==0)
                        printf("Processed %lu samples, resampleRatio = %.2f, outputLimit = %.2f\n", samplesReceived, resampleRatio, outputLimit);
        }

/* each of the stages comes with its own cleanup section */
cleanup4:
        enableResample = 0;
        enableUpdate = 0;
        Pa_StopStream(outputStream);
        sp_blocking_write(port, stop_acq, 3, TIMEOUT);

cleanup3:
        if (resampleState)
                src_delete (resampleState);
        if (inputData.data)
                free(inputData.data);
        if (outputData.data)
                free(outputData.data);

cleanup2:
        Pa_Terminate();

cleanup1:
        sp_close(port);
        sp_free_port(port);

        return 0;
}

/*******************************************************************************************************/
/* Helper function to read and parse one EEG data sample. */
int unicorn_pull_sample(struct sp_port *port, float *dat)
{
        unsigned char buf[45];
        int result = sp_blocking_read(port, buf, PACKETSIZE, TIMEOUT);
        if (result!=PACKETSIZE || buf[0]!=0xC0 || buf[1]!=0x00) {
                return 1;
        }

        float battery = (buf[2] & 0x0F) * 100. / 15.;
        unsigned long counter = (unsigned long)buf[39] | (unsigned long)buf[40] << 8 | (unsigned long)buf[41] << 16 | (unsigned long)buf[42] << 24;

        for (int ch=0; ch<8; ch++) {
                unsigned long val = (unsigned long)buf[3+ch*3] << 16 | (unsigned long)buf[4+ch*3] << 8 | (unsigned long)buf[5+ch*3];
                if (val & 0x00800000) {
                        val = val | 0xFF000000;
                }
                dat[ch] = (float)val * 4500000. / 50331642.;
        }

        for (int ch=0; ch<3; ch++) {
                short val = (short)buf[27+ch*2] | (short)buf[28+ch*2] << 8;
                dat[8+ch] = (float)val / 4096.;
        }

        for (int ch=0; ch<3; ch++) {
                short val = (short)buf[33+ch*2] | (short)buf[34+ch*2] << 8;
                dat[11+ch] = (float)val / 32.8;
        }

        /* collect and copy the output data */
        dat[14] = battery;
        dat[15] = counter;

        return 0;
}

/*******************************************************************************************************/
/* Helper function for error handling. */
int sp_check(enum sp_return result)
{
        char *error_message;
        switch (result) {
        case SP_ERR_ARG:
                printf("Error: Invalid argument.\n");
                abort();
        case SP_ERR_FAIL:
                error_message = sp_last_error_message();
                printf("Error: Failed: %s\n", error_message);
                sp_free_error_message(error_message);
                abort();
        case SP_ERR_SUPP:
                printf("Error: Not supported.\n");
                abort();
        case SP_ERR_MEM:
                printf("Error: Couldn't allocate memory.\n");
                abort();
        case SP_OK:
        default:
                return result;
        }
}

/*******************************************************************************************************/
/* Helper function for stopping properly. */
void stream_finished(void *userData) {
        keepRunning = 0;
        return;
}

/*******************************************************************************************************/
/* Helper function for stopping properly. */
void signal_handler(int signum) {
        switch (signum) {
        case SIGINT:
                printf("SIGINT\n");
                keepRunning = 0;
                break;
        case SIGHUP:
                printf("SIGHUP\n");
                break;
        case SIGKILL:
                printf("SIGKILL\n");
                break;
        case SIGUSR1:
                printf("SIGUSR1\n");
                break;
        case SIGUSR2:
                printf("SIGUSR2\n");
                break;
        }
        return;
}
