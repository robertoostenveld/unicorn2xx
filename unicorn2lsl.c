/*
 * This application reads EEG data from the Unicorn from a serial-over-bluetooth device
 * and writes it to a LabStreamingLayer (LSL) stream.
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
#include "lsl_c.h"

#if defined __linux__ || defined __APPLE__
// Linux and macOS code goes here
#include <unistd.h>
#define min(x, y) ((x)<(y) ? x : y)
#define max(x, y) ((x)>(y) ? x : y)
#elif defined _WIN32
// Windows code goes here
#endif

/* Helper function for serial port error handling. */
int check(enum sp_return result);

/* Helper function for stopping properly. */
void signal_handler(int signum);

/* Helper function to generate random UID string. */
void rand_str(char *, size_t);

char start_acq[]      = {0x61, 0x7C, 0x87};
char stop_acq[]       = {0x63, 0x5C, 0xC5};
char start_response[] = {0x00, 0x00, 0x00};
char stop_response[]  = {0x00, 0x00, 0x00};
char start_sequence[] = {0xC0, 0x00};
char stop_sequence[]  = {0x0D, 0x0A};

#define FSAMPLE     (250)
#define NCHANS      (16)
#define STRLEN      (80)
#define PACKETSIZE  (45)
#define TIMEOUT     (5000)
#define LSLSTREAM   "Unicorn"
#define LSLTYPE     "EEG"
#define LSLBUFFER   (360)

struct sp_port *port = NULL;
int running = 1;

int main(int argc, char **argv)
{
        char line[STRLEN], outputStream[STRLEN], outputUID[STRLEN];
        int inputDevice = 0;
        struct sp_port **port_list = NULL;
        unsigned long counter = 0;

        printf("Getting port list.\n");
        check(sp_list_ports(&port_list));

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

        printf("Select port [%d]: ", inputDevice);
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                inputDevice = inputDevice;
        else
                inputDevice = atoi(line);

        memset(outputStream, 0, STRLEN);
        sprintf(outputStream, LSLSTREAM);
        printf("LSL stream name [%s]: ", LSLSTREAM);
        fgets(line, STRLEN, stdin);
        if (strlen(line)>1)
                strncpy(outputStream, line, strlen(line)-1);

        /* copy the selected port, clear the others */
        check(sp_copy_port(port_list[inputDevice], &port));
        sp_free_port_list(port_list);

        printf("Opening port %s (%s).\n", sp_get_port_name(port), sp_get_port_description(port));
        check(sp_open(port, SP_MODE_READ_WRITE));

        printf("Setting port to 115200, 8N1, no flow control.\n");
        check(sp_set_baudrate(port, 115200));
        check(sp_set_bits(port, 8));
        check(sp_set_parity(port, SP_PARITY_NONE));
        check(sp_set_stopbits(port, 1));
        check(sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE));

        unsigned char *buf = malloc(PACKETSIZE);
        memset(buf, 0, PACKETSIZE);

        if (sp_blocking_write(port, start_acq, 3, TIMEOUT)!=3) {
                printf("Cannot start data stream.\n");
                goto cleanup0;
        }

        int result = sp_blocking_read(port, buf, 3, TIMEOUT);
        if (result!=3 || buf[0]!=0x00 || buf[1]!=0x00 || buf[2]!=0x00) {
                printf("Incorrect response.\n");
                goto cleanup0;
        }

        printf("Started data stream.\n");

        signal(SIGINT, signal_handler);
#ifndef _WIN32
        signal(SIGHUP, signal_handler);
        signal(SIGUSR1, signal_handler);
        signal(SIGUSR2, signal_handler);
#endif

        /* initialize the LSL stream */
        rand_str(outputUID, 8);
        lsl_streaminfo info = lsl_create_streaminfo(outputStream, LSLTYPE, NCHANS, FSAMPLE, cft_float32, outputUID);
        printf("Opened LSL stream.\n");
        printf("LSL name = %s\n", outputStream);
        printf("LSL type = %s\n", LSLTYPE);
        printf("LSL uid = %s\n", outputUID);

        /* add some meta-data fields to it */
        lsl_xml_ptr desc = lsl_get_desc(info);
        lsl_xml_ptr acquisition = lsl_append_child(desc, "acquisition");
        lsl_append_child_value(acquisition, "manufacturer", "Gtec");
        lsl_append_child_value(acquisition, "model", "Unicorn");
        lsl_append_child_value(acquisition, "precision", "24");
        lsl_xml_ptr chns = lsl_append_child(desc, "channels");
        const char *label[NCHANS] = {"eeg1","eeg2","eeg3","eeg4","eeg5","eeg6","eeg7","eeg8","accelX","accelY","accelZ","gyroX","gyroY","gyroZ","battery","counter"};
        const char *unit[NCHANS] = {"uV","uV","uV","uV","uV","uV","uV","uV","g","g","g","deg/s","deg/s","deg/s","percent","integer"};
        const char *type[NCHANS] = {"EEG","EEG","EEG","EEG","EEG","EEG","EEG","EEG","ACCEL","ACCEL","ACCEL","GYRO","GYRO","GYRO","BATTERY","COUNTER"};
        for (int c=0; c<NCHANS; c++) {
                printf("LSL channel %2d: %8s, %8s, %8s\n", c+1, label[c], unit[c], type[c]);
                lsl_xml_ptr chn = lsl_append_child(chns, "channel");
                lsl_append_child_value(chn, "label", label[c]);
                lsl_append_child_value(chn, "unit", unit[c]);
                lsl_append_child_value(chn, "type", type[c]);
        }

        lsl_outlet outlet = lsl_create_outlet(info, 0, LSLBUFFER);

        while (running) {
                int result = sp_blocking_read(port, buf, PACKETSIZE, TIMEOUT);
                if (result!=PACKETSIZE || buf[0]!=0xC0 || buf[1]!=0x00) {
                        printf("Cannot read packet.\n");
                        goto cleanup2;
                }
                else {
                        float dat[16];
                        counter++;

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

                        dat[14] = (buf[2] & 0x0F) * 100. / 15.;
                        dat[15] = (unsigned long)buf[39] | (unsigned long)buf[40] << 8 | (unsigned long)buf[41] << 16 | (unsigned long)buf[42] << 24;

                        /* write this sample to LSL */
                        lsl_push_sample_f(outlet, dat);

                        /* give some feedback on screen */
                        if ((counter % FSAMPLE)==0) {
                                printf("Wrote %lu samples.\n", counter);
                        }
                }
        }

cleanup2:
        sp_blocking_write(port, stop_acq, 3, TIMEOUT);

cleanup1:
        lsl_destroy_outlet(outlet);

cleanup0:
        sp_close(port);
        sp_free_port(port);

        return 0;
}


/* Helper function for error handling. */
int check(enum sp_return result)
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

/* Helper function for stopping properly. */
void signal_handler(int signum) {
        switch (signum) {
        case SIGINT:
                printf("Received SIGINT\n");
                running = 0;
                break;
#ifndef _WIN32
        case SIGHUP:
                printf("Received SIGHUP\n");
                break;
        case SIGUSR1:
                printf("Received SIGUSR1\n");
                break;
        case SIGUSR2:
                printf("Received SIGUSR2\n");
                break;
#endif
        }
}

/* Helper function to generate random UID string. */
void rand_str(char *dest, size_t length) {
        char charset[] = "0123456789"
                         "abcdefghijklmnopqrstuvwxyz";

        while (length-- > 0) {
                size_t index = (double) rand() / RAND_MAX * (sizeof charset - 1);
                *dest++ = charset[index];
        }
        *dest = '\0';
}
