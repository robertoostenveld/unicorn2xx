#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <libserialport.h>

#if defined __linux__ || defined __APPLE__
// Linux and macOS code goes here
#include <unistd.h>
#define min(x, y) ((x)<(y) ? x : y)
#define max(x, y) ((x)>(y) ? x : y)
#elif defined _WIN32
// Windows code goes here
#endif

/* Helper function for error handling. */
int check(enum sp_return result);

/* Helper function for stopping properly. */
void signal_handler(int signum);

char start_acq[]      = {0x61, 0x7C, 0x87};
char stop_acq[]       = {0x63, 0x5C, 0xC5};
char start_response[] = {0x00, 0x00, 0x00};
char stop_response[]  = {0x00, 0x00, 0x00};
char start_sequence[] = {0xC0, 0x00};
char stop_sequence[]  = {0x0D, 0x0A};

#define STRLEN 80
#define PACKETSIZE 45
#define TIMEOUT 5000

struct sp_port *port = NULL;
int running = 1;

int main(int argc, char **argv)
{
        char line[STRLEN], outputFile[STRLEN];
        FILE *fp;
        int inputDevice = 0;
        struct sp_port **port_list = NULL;

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

        memset(outputFile, 0, STRLEN);
        printf("Output file [stdout]: ");
        fgets(line, STRLEN, stdin);
        if (strlen(line)>1)
                strncpy(outputFile, line, strlen(line)-1);

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

        if (strlen(outputFile)) {
                /* open the selected output file */
                printf("Opening file %s.\n", outputFile);
                fp = fopen(outputFile, "w");
                if (fp==NULL) {
                        printf("Cannot open file: %s\n", strerror(errno));
                        goto cleanup1;
                }
        }
        else {
                /* output goes to the screen */
                fp = stdout;
        }

        signal(SIGINT, signal_handler);
        
        fprintf(fp, "eeg1\teeg2\teeg3\teeg4\teeg5\teeg6\teeg7\teeg8\taccel1\taccel2\taccel3\tgyro1\tgyro2\tgyro3\tbattery\tcounter\n");

        while (running) {
                int result = sp_blocking_read(port, buf, PACKETSIZE, TIMEOUT);
                if (result!=PACKETSIZE || buf[0]!=0xC0 || buf[1]!=0x00) {
                        printf("Cannot read packet.\n");
                        goto cleanup2;
                }
                else {
                        float battery = (buf[2] & 0x0F) * 100. / 15.;
                        unsigned long counter = (unsigned long)buf[39] | (unsigned long)buf[40] << 8 | (unsigned long)buf[41] << 16 | (unsigned long)buf[42] << 24;
                        float eeg[8], accel[3], gyro[3];

                        for (int ch=0; ch<8; ch++) {
                                unsigned long val = (unsigned long)buf[3+ch*3] << 16 | (unsigned long)buf[4+ch*3] << 8 | (unsigned long)buf[5+ch*3];
                                if (val & 0x00800000) {
                                        val = val | 0xFF000000;
                                }
                                eeg[ch] = (float)val * 4500000. / 50331642.;
                        }

                        for (int ch=0; ch<3; ch++) {
                                short val = (short)buf[27+ch*2] | (short)buf[28+ch*2] << 8;
                                accel[ch] = (float)val / 4096.;
                        }

                        for (int ch=0; ch<3; ch++) {
                                short val = (short)buf[33+ch*2] | (short)buf[34+ch*2] << 8;
                                gyro[ch] = (float)val / 32.8;
                        }

                        fprintf(fp, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t", eeg[0], eeg[1], eeg[2], eeg[3], eeg[4], eeg[5], eeg[6], eeg[7]);
                        fprintf(fp, "%f\t%f\t%f\t", accel[0], accel[1], accel[2]);
                        fprintf(fp, "%f\t%f\t%f\t", gyro[0], gyro[1], gyro[2]);
                        fprintf(fp, "%.2f\t%lu\n", battery, counter);

                        /* give some feedback on screen when writing data to file */
                        if (strlen(outputFile) && (counter % 250)==0) {
                                printf("Wrote %lu samples.\n", counter);
                        }
                }
        }

cleanup2:
        sp_blocking_write(port, stop_acq, 3, TIMEOUT);

cleanup1:
        fclose(fp);

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
                printf("SIGINT\n");
                running = 0;
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
}
