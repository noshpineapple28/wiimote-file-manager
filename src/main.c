#include <stdio.h> /* for printf */
#include <time.h>  /* for timing downloads */

#include "wiiuse.h" /* for wiimote_t, classic_ctrl_t, etc */

#include <stdlib.h>

#ifndef WIIUSE_WIN32
#include <unistd.h> /* for usleep */
#endif
#include "io.h"

#define MAX_WIIMOTES 1
#define MAX_PAYLOAD 5300

// holds the size of our payload we expect
uint32_t payload_size = 0;
// holds how much of the payload we've gotten
uint32_t payload_received = 0;
// leds of the wiimote
int leds = 0x00;

typedef struct header
{
    uint32_t file_size;
    uint32_t file_size_on_remote;
    uint16_t total_remotes;
    uint16_t curr_remote_num;
} header;

void print_progress(wiimote *remote, char *title, float rec, float tot)
{
    char completed[51];
    // update progress bar
    float p = rec / tot;
    int i   = 0.0;
    char a = 177, b = 219;
    do
    {
        completed[i] = (i < (50.0 * p)) ? b : a;
    } while (++i < 50.0);
    completed[i] = '\0';

    // update LEDS
    if (p <= 0.25)
    {
        leds = 0x00;
        wiiuse_set_leds(remote, leds);
    } else if (p <= 0.5)
    {
        leds = (1 << 4);
        wiiuse_set_leds(remote, leds);
    } else if (p <= 0.75)
    {
        leds = (1 << 5) | (1 << 4);
        wiiuse_set_leds(remote, leds);
    } else
    {
        leds = (1 << 6) | (1 << 5) | (1 << 4);
        wiiuse_set_leds(remote, leds);
    }

    printf("%s [%s]     %4dB / %4dB\r", title, completed, payload_received, payload_size);
}

void alert_remote(wiimote *remote)
{
    wiiuse_toggle_rumble(remote);
    Sleep(175);
    wiiuse_toggle_rumble(remote);
    Sleep(50);
    wiiuse_toggle_rumble(remote);
    Sleep(200);
    wiiuse_toggle_rumble(remote);
}

uint16_t convert_to_uint16(uint8_t *p_value)
{
    uint32_t least_sig       = (0x0000 | p_value[1]);
    uint32_t most_sig        = (0x0000 | p_value[0]) << 8;
    uint32_t converted_value = least_sig | most_sig;
    return converted_value;
}

uint32_t convert_to_uint32(uint8_t *p_value)
{
    uint32_t least_sig        = (0x00000000 | p_value[3]);
    uint32_t second_least_sig = (0x00000000 | p_value[2]) << 8;
    uint32_t second_most_sig  = (0x00000000 | p_value[1]) << 16;
    uint32_t most_sig         = (0x00000000 | p_value[0]) << 24;
    uint32_t converted_value  = (least_sig | second_least_sig) | (second_most_sig | most_sig);
    return converted_value;
}

int32_t findSize(char *file_name)
{
    // opening the file in read mode
    FILE *fp = fopen(file_name, "rb");

    // checking if the file exist or not
    if (fp == NULL)
    {
        return -1;
    }

    fseek(fp, 0L, SEEK_END);

    // calculating the size of the file
    long int res = ftell(fp);

    // closing the file
    fclose(fp);
    payload_size = res;

    return res;
}

short any_wiimote_connected(wiimote **wm, int wiimotes)
{
    int i;
    if (!wm)
    {
        return 0;
    }

    for (i = 0; i < wiimotes; i++)
    {
        if (wm[i] && WIIMOTE_IS_CONNECTED(wm[i]))
        {
            return 1;
        }
    }

    return 0;
}

wiimote **connect_remotes()
{
    int found, connected;

    wiimote **wiimotes = wiiuse_init(MAX_WIIMOTES);
    found              = wiiuse_find(wiimotes, MAX_WIIMOTES, 5);
    if (!found)
    {
        printf("No wiimotes found.\n");
        return 0;
    }
    connected = wiiuse_connect(wiimotes, MAX_WIIMOTES);
    if (connected)
    {
        printf("Connected to %i wiimotes (of %i found).\n", connected, found);
    } else
    {
        printf("Failed to connect to any wiimote.\n");
        return 0;
    }

    return wiimotes;
}

int read_from_wiimote(wiimote *remote, char *buffer, unsigned int address)
{
    wiiuse_read_data_sync(remote, 0x01, address, 16, buffer);

    return 1;
}

int write_to_wiimote(wiimote *remote, char *buffer, unsigned int address)
{
    int res = wiiuse_write_data(remote, address, buffer, 16);

    return 1;
}

int compare_buffers(char *buf1, char *buf2)
{
    int i = 0;
    while (i < 16)
    {
        // they are different! EXIT
        if (buf1[i] != buf2[i])
        {
            // print the vals that didnt match
            printf("\nDIDNT MATCH: %d != %d", buf1[i], buf2[i]);
            printf("\nDIDNT MATCH: %x != %x", buf1[i], buf2[i]);
            printf("\nDIDNT MATCH: %d", i);
            return 0;
        }
        i++;
    }

    return 1;
}

int write_file(wiimote *remote, char *buffer, char *file_name, int address, FILE *fp, int restarted)
{
    int res             = findSize(file_name);
    int failures        = 0;
    char header_buf[16] = {(payload_size >> 24),
                           (payload_size << 8) >> 24,
                           (payload_size << 16) >> 24,
                           (payload_size << 24) >> 24,
                           payload_size >> 24,
                           (payload_size << 8) >> 24,
                           (payload_size << 16) >> 24,
                           (payload_size << 24) >> 24,
                           0x00,
                           0x01,
                           0x00,
                           0x01,
                           0x00,
                           0x00,
                           0x00,
                           0x00};
    char *buf_ptr       = buffer;
    char check_buf[16]; // used to redownload and check state

    if (!address)
    {
        // get the file size and setup payload/payload size
        if (res == -1)
        {
            printf("[ERROR] File not found. Please make sure your file exists\n");
            fclose(fp);
            return -2;
        }

        if (payload_size <= 0 || payload_size >= MAX_PAYLOAD)
        {
            printf("[ERROR] File size %d is invalid\n", payload_size);
            return 0;
        }

        printf("[INFO] Beginning upload of file of size %dB\n", payload_size);
        print_progress(remote, "UPLOAD PROGRESS:", payload_received, payload_size, 1);
        int failures     = 0;
        payload_received = 0;
        // write header file
        do
        {
            write_to_wiimote(remote, header_buf, address);
            time_t start = time(NULL);
            read_from_wiimote(remote, check_buf, address);
            time_t end = time(NULL);
            // if timed out, exit
            if (end - start >= 5)
            {
                printf("\n[ERROR] Request timed out. Restarting soon...\n");
                return address;
            }
        } while (!compare_buffers(header_buf, check_buf) && failures++ < 10);

        if (failures >= 10)
        {
            printf("[ERROR] Upload timed out. Restarting soon...\n");
            return address;
        }
        address += 0x10;
    } else
    {
        printf("[INFO] Resuming upload. %dB out of %dB\n", payload_received, payload_size);
        buf_ptr += address - 0x10;
    }

    int i = 0;
    while (payload_received < payload_size)
    {
        // only read the file if we ARENT restarting the download
        while (i++ < 16 && !restarted)
        {
            fread(buf_ptr++, sizeof(char), 1, fp);
        }
        // if we hit 16, reset everything
        failures = 0;
        do
        {
            write_to_wiimote(remote, buffer, address);
            time_t start = time(NULL);
            read_from_wiimote(remote, check_buf, address);
            time_t end = time(NULL);
            // if timed out, exit
            if (end - start >= 5)
            {
                printf("\n[ERROR] Upload timed out. Restarting soon...\n");
                return address;
            }
        } while (!compare_buffers(buffer, check_buf) && failures++ < 10);
        // this will occur if matches SUCK
        if (failures >= 10)
        {
            printf("\n[ERROR] Upload timed out. Restarting soon...\n");
            return address;
        }

        // reset vals
        i       = 0;
        buf_ptr = buffer;
        address += 0x10;
        payload_received += 16;
        restarted = 0; // if we successfully upload, and we restarted a upload, set restarted to false
        if (payload_received > payload_size)
        {
            payload_received = payload_size;
        }

        // print progress, continue reading file
        print_progress(remote, "UPLOAD PROGRESS:", payload_received, payload_size, 1);
    }

    // close file and alert user
    fclose(fp);
    wiiuse_set_leds(remote, 0xF0);
    alert_remote(remote);

    return -1;
}

int download_file(wiimote *remote, char *file_buffer, char *file_name, int address, FILE *fp)
{
    char buffer[16]; // general reader buffer
    char *file_pos = file_buffer;
    // get header data
    if (!address)
    {
        printf("[INFO] Estimating size...\n");
        read_from_wiimote(remote, buffer, address);
        header *ret = (header *)buffer; // reads header info
        // set nums
        payload_size     = convert_to_uint32(&(ret->file_size_on_remote));
        payload_received = 0;
        // exit if corrupted
        if (payload_size <= 0 || payload_size >= MAX_PAYLOAD)
        {
            printf("[ERROR] Download size of %dB is invalid\n", payload_size);
            return -2;
        }
        printf("[INFO] Beginning download. Total size is %dB\n", payload_size);
        address += 0x10;
        print_progress(remote, "DATA DOWNLOADED:", payload_received, payload_size, 1);
    } else
    {
        printf("[INFO] Resuming download. %dB out of %dB\n", payload_received, payload_size);
        file_pos += address - 0x10;
    }

    // now create a buffer to hold the file

    // read 16 at a time
    // this downloads the file at file_pos of the total buffer
    while (payload_received < payload_size)
    {
        time_t start = time(NULL);
        read_from_wiimote(remote, file_pos, address);
        time_t end = time(NULL);
        if (end - start >= 5)
        {
            printf("\n[ERROR] Download timed out. Restarting soon...\n");
            return address;
        }

        // sometimes the buffer hasn't been written correctly, so pause and wait
        Sleep(1);

        // update payload and file pos
        payload_received += 16;
        file_pos += 16;
        address += 0x10;
        if (payload_received > payload_size)
        {
            payload_received = payload_size;
        }
        print_progress(remote, "DATA DOWNLOADED:", payload_received, payload_size, 1);
    }

    // save downloaded data
    printf("\n[INFO] Data successfully downloaded. Preparing to write to file\n");
    fwrite(file_buffer, payload_size, sizeof(char), fp);
    printf("[INFO] Data successfully written\n");
    wiiuse_set_leds(remote, 0xF0);
    alert_remote(remote);
    fclose(fp);

    return -1;
}

void run_selected_process(wiimote **wiimotes, char *file_name, int mode)
{
    // start of app
    wiiuse_set_leds(wiimotes[0], 0x00);
    int address = 0x00;       // where on the remote we are
    int fails   = 0;          // how many times a process failed
    char buffer[MAX_PAYLOAD]; // used to hold data received/sent
    int res;                  // result from an operation
    FILE *fp;                 // the file we are reading/writing to

    if (!mode) // init data
    {
        fp = fopen(file_name, "rb");
        printf("[INFO] Preparing to upload file to: %s\n", file_name);
    } else
    {
        fp = fopen(file_name, "wb");
        printf("[INFO] Preparing to download file to: %s\n", file_name);
    }
    int restarted_task = 0; // set if we ever fail a task
    while (any_wiimote_connected(wiimotes, MAX_WIIMOTES) && fails++ < 10)
    {
        switch (mode)
        {
        case 0:
            res = write_file(wiimotes[0], buffer, file_name, address, fp, restarted_task);
            break;
        case 1:
            res = download_file(wiimotes[0], buffer, file_name, address, fp);
            break;
        }

        // handle resulting output
        if (res == -2 || res == -1) // if a fatal error, or a success results, exit
        {
            break;
        } else if (res >= 0)
        {
            // this prepares to call the method to restart at a specific point
            address        = res;
            restarted_task = 1;
            // completely restart the app
            wiiuse_cleanup(wiimotes, MAX_WIIMOTES);
            wiimotes = connect_remotes();
            printf("\n");
            continue;
        }
    }
    if (fails >= 10)
    {
        printf("[ERROR] Process failed 10 times.\n        In the event that the large number of failures is "
               "due to a broken connection, program is terminating.\n        Try again later\n\n");
    }
}

/**
 *  @brief main()
 *
 *  Connect to up to two wiimotes and print any events
 *  that occur on either device.
 */
int main(int argc, char **argv)
{
    /**
     * 0 - UPLOAD
     * 1 - DOWNLOAD
     */
    int mode;
    char *file_name;
    if (argc == 3)
    {
        if (!strcmp(argv[1], "-u") || !strcmp(argv[1], "--upload"))
        {
            mode      = 0;
            file_name = argv[2];
            if (findSize(file_name) == -1 || !payload_size)
            {
                printf(
                    "[ERROR] File '%s' does not exist, or is empty. Please select an existing file to upload",
                    file_name);
            }
        } else if (!strcmp(argv[1], "-d") || !strcmp(argv[1], "--download"))
        {
            mode      = 1;
            file_name = argv[2];
        }
    } else if (argc == 2) // two arg start
    {
        mode = 0;
        if (!argv[1])
        {
            printf("[ERROR] No file given\n");
            return 0;
        }
        file_name = argv[1];
        if (findSize(file_name) == -1)
        {
            mode = 1;
        }
    } else
    {
        printf("[ERROR] Invalid arguments. Valid args:\n\n-u || --upload  \tSet mode to upload a file\n-d || "
               "--download\tSet mode to download a file\n<file_name>     \tAfter selecting mode, give a file "
               "location to read from\n");
        return 0;
    }

    // setup wiiuse
    wiimote **wiimotes = connect_remotes();

#ifndef WIIUSE_WIN32
    usleep(200000);
#else
    Sleep(200);
#endif

    printf("\n================================\n\n");

    run_selected_process(wiimotes, file_name, mode);

    // wait for rumble input to end
    Sleep(500);
    wiiuse_set_leds(wiimotes[0], 0x00);
    Sleep(100); // wait for remote to turn off
    wiiuse_cleanup(wiimotes, MAX_WIIMOTES);

    return 0;
}
