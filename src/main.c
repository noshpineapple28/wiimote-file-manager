#include <stdio.h> /* for printf */
#include <stdlib.h>
#include <time.h> /* for timing downloads */

#include "wiiuse.h" /* for wiimote_t, classic_ctrl_t, etc */
#include "io.h"

#ifndef WIIUSE_WIN32
#include <unistd.h> /* for usleep */
#endif

#define MAX_WIIMOTES 1
#define MAX_WIIMOTE_PAYLOAD 5300

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
    int i   = 0;
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
    wiiuse_set_leds(remote, 0xF0);
    wiiuse_toggle_rumble(remote);
    Sleep(175);
    wiiuse_set_leds(remote, 0x00);
    wiiuse_toggle_rumble(remote);
    Sleep(50);
    wiiuse_toggle_rumble(remote);
    Sleep(200);
    wiiuse_set_leds(remote, 0xF0);
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
    FILE *fp;
    fopen_s(&fp, file_name, "rb");

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

int get_file_name(char *file_name, char *name, char *ext)
{
    int last_slash_index = 0;
    int file_len         = 0;
    int ext_len          = 0;
    int ext_index        = 0;

    // read from file, and get pos of filename
    int i = 0;
    while (file_name[i] != '\0')
    {
        if (file_name[i] == '\\' || file_name[i] == '/')
        {
            last_slash_index = i;
        }
        if (file_name[i] == '.')
        {
            file_len  = i - last_slash_index - 1;
            ext_len   = (int)strlen(file_name) - i - 1;
            ext_index = i;
        }
        i++;
    }
    if (ext_index < last_slash_index)
    {
        file_len = ((int)strlen(file_name)) - last_slash_index;
    }

    // write filename
    i = 0;
    while (i < file_len)
    {
        name[i] = file_name[last_slash_index + 1 + i];
        i++;
    }
    while (i < 16)
    {
        name[i++] = 0xcc;
    }
    // write extension
    i = 0;
    while (i < ext_len)
    {
        ext[i] = file_name[last_slash_index + file_len + 2 + i];
        i++;
    }
    while (i < 16)
    {
        ext[i++] = 0xcc;
    }

    return 1;
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
    time_t start = time(NULL);
    wiiuse_read_data_sync(remote, 0x01, address, 16, buffer);
    time_t end = time(NULL);

    if (end - start >= 5)
    {
        printf("\n[ERROR] Download timed out. Restarting soon...\n");
        return -1;
    }

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
            return 0;
        }
        i++;
    }

    return 1;
}

int validate_upload(wiimote *remote, char *buffer1, char *buffer2, int address)
{
    int failures = 0;

    do
    {
        write_to_wiimote(remote, buffer1, address);
        if (read_from_wiimote(remote, buffer2, address) == -1)
        {
            return -1;
        }
    } while (!compare_buffers(buffer1, buffer2) && failures++ < 10);
    // this will occur if matches SUCK or keep sucking
    if (failures >= 10)
    {
        printf("\n[ERROR] Upload timed out. Restarting soon...\n");
        return -1;
    }

    return 1;
}

int upload_header(wiimote *remote, char *file_name, FILE *fp)
{
    int res             = findSize(file_name);
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
    char check_buf[16];

    // get the file size and setup payload/payload size
    if (res == -1)
    {
        printf("[ERROR] File not found. Please make sure your file exists\n");
        fclose(fp);
        return -2;
    }

    if (payload_size <= 0 || payload_size >= MAX_WIIMOTE_PAYLOAD)
    {
        printf("[ERROR] File size %d is invalid\n", payload_size);
        return 0x00;
    }

    printf("[INFO] Beginning upload of file of size %dB\n", payload_size);
    payload_received = 0;
    // write header file
    if (validate_upload(remote, header_buf, check_buf, 0x00) == -1)
    {
        return 0x00;
    }

    // read filename/ext from chosen doc
    char name[16];
    char ext[16];
    get_file_name(file_name, name, ext);
    // write filename
    if (validate_upload(remote, name, check_buf, 0x10) == -1)
    {
        return 0x00;
    }
    // write file extension
    if (validate_upload(remote, ext, check_buf, 0x20) == -1)
    {
        return 0x00;
    }

    return 1;
}

int write_file(wiimote *remote, char *buffer, char *file_name, int address, FILE *fp, int restarted)
{
    char *buf_ptr = buffer;
    char check_buf[16]; // used to redownload and check state

    // setup header
    if (address < 0x30)
    {
        // if upload timed out, exit
        if (upload_header(remote, file_name, fp) == 0x00)
        {
            return 0x00;
        }
        address = 0x30;
        print_progress(remote, "UPLOAD PROGRESS:", (float)payload_received, (float)payload_size);
    } else
    {
        printf("[INFO] Resuming upload. %dB out of %dB\n", payload_received, payload_size);
        buf_ptr += address - 0x30;
    }

    int i = 0;
    // upload the rest of the file
    while (payload_received < payload_size)
    {
        // only read the file if we ARENT restarting the download
        while (i++ < 16 && !restarted)
        {
            fread(buf_ptr++, sizeof(char), 1, fp);
        }
        // if we hit 16, reset everything
        if (validate_upload(remote, buffer, check_buf, address) == -1)
        {
            return address;
        }

        // reset vals
        i       = 0;
        buf_ptr = buffer;
        address += 0x10;
        payload_received = payload_received > payload_size ? payload_size : payload_received + 16;
        restarted        = 0; // if we successfully upload, and we restarted a upload, set restarted to false

        // print progress, continue reading file
        print_progress(remote, "UPLOAD PROGRESS:", (float)payload_received, (float)payload_size);
    }

    // close file and alert user
    fclose(fp);
    alert_remote(remote);

    return -1;
}

int download_header(wiimote *remote, char *file_name)
{
    char buffer[16];

    printf("[INFO] Estimating size...\n");
    if (read_from_wiimote(remote, buffer, 0x00) == -1)
    {
        return -1;
    }
    header *ret = (header *)buffer; // reads header info
    // set nums
    payload_size     = convert_to_uint32((uint8_t *)&(ret->file_size_on_remote));
    payload_received = 0;
    // exit if corrupted
    if (payload_size <= 0 || payload_size >= MAX_WIIMOTE_PAYLOAD)
    {
        printf("[ERROR] Download size of %dB is invalid\n", payload_size);
        return -2;
    }

    Sleep(1); // read file NAME
    if (read_from_wiimote(remote, buffer, 0x10) == -1)
    {
        return -1;
    }
    char *cp = file_name;
    int i    = 0;
    while (i < 16 && buffer[i] != -52)
    {
        *cp++ = buffer[i++];
    }

    Sleep(1); // read file EXTENSION
    if (read_from_wiimote(remote, buffer, 0x20) == -1)
    {
        return -1;
    }
    i = 0;
    while (i < 16 && buffer[i] != -52)
    {
        if (i == 0)
        {
            *cp++ = '.';
        }
        *cp++ = buffer[i++];
    }
    *cp = '\0';
    Sleep(1);

    printf("[INFO] File found: %s\n", file_name);
    printf("[INFO] Beginning download. Total size is %dB\n", payload_size);

    return 1;
}

int download_file(wiimote *remote, char *file_buffer, char *file_name, int address)
{
    char *file_pos = file_buffer;

    // get header data
    if (address < 0x30)
    {
        switch (download_header(remote, file_name))
        {
        case 1:
            address = 0x30; // success! continue
            print_progress(remote, "DATA DOWNLOADED:", (float)payload_received, (float)payload_size);
            break;
        case -1:
            return 0x00; // timed out
        case -2:
            return -2; // invalid header
        }
    } else
    {
        printf("[INFO] Resuming download. %dB out of %dB\n", payload_received, payload_size);
        file_pos += address - 0x30;
    }

    // read 16 at a time
    // this downloads the file at file_pos of the total buffer
    while (payload_received < payload_size)
    {
        if (read_from_wiimote(remote, file_pos, address) == -1)
        {
            return address;
        }

        // sometimes the buffer hasn't been written correctly, so pause and wait
        Sleep(1);

        // update payload and file pos
        payload_received = payload_received > payload_size ? payload_size : payload_received + 16;
        file_pos += 16;
        address += 0x10;
        print_progress(remote, "DATA DOWNLOADED:", (float)payload_received, (float)payload_size);
    }

    // save downloaded data
    printf("\n[INFO] Data successfully downloaded. Preparing to write to file\n");
    FILE *fp;
    fopen_s(&fp, file_name, "wb");
    fwrite(file_buffer, payload_size, sizeof(char), fp);
    // end download
    printf("[INFO] Data successfully written\n");
    alert_remote(remote);
    fclose(fp);

    return -1;
}

void run_selected_process(wiimote **wiimotes, char *file_name, int mode)
{
    // start of app
    wiiuse_set_leds(wiimotes[0], 0x00);
    int address = 0x00;                 // where on the remote we are
    int fails   = 0;                    // how many times a process failed
    char buffer[MAX_WIIMOTE_PAYLOAD];   // used to hold data received/sent
    int res;                            // result from an operation
    FILE *fp;                           // the file we are reading/writing to

    if (!mode) // init data
    {
        fopen_s(&fp, file_name, "rb");
        printf("[INFO] Preparing to upload file to: %s\n", file_name);
    } else
    {
        printf("[INFO] Preparing to download a file...\n");
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
            res = download_file(wiimotes[0], buffer, file_name, address);
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
    if (argc == 2) // upload
    {
        mode      = 0;
        file_name = argv[1];
        if (findSize(file_name) == -1 || !payload_size)
        {
            printf("[ERROR] File '%s' does not exist, or is empty. Please select an existing file to upload",
                   file_name);
        }
    } else if (argc == 1) // download
    {
        char file[34];
        file_name = file;
        mode      = 1;
    } else // help
    {
        printf("[ERROR] Invalid arguments. Valid args:\n\n<file_name>\tIf given a valid file, will attempt "
               "to upload it\n"
               "NONE       \tWill attempt to download the file on remote\n");
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
    printf("\n[INFO] Exiting...\n");
    Sleep(550);
    wiiuse_set_leds(wiimotes[0], 0x00);
    Sleep(200); // wait for remote to turn off
    wiiuse_cleanup(wiimotes, MAX_WIIMOTES);

    return 0;
}
