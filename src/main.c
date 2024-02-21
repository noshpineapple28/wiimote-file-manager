#include <stdio.h> /* for printf */
#include <stdlib.h>
#include <time.h> /* for timing downloads */

#include "wiiuse.h" /* for wiimote_t, classic_ctrl_t, etc */
#include "io.h"

#include "wpf_handler.h"

#ifndef WIIUSE_WIN32
#include <unistd.h> /* for usleep */
#endif

#define MAX_WIIMOTES 2
#define MAX_WIIMOTE_PAYLOAD 5360

// holds the size of our payload we expect
uint32_t payload_size = 0;
// holds how much of the payload we've gotten
uint32_t payload_received = 0;
// leds of the wiimote
int leds = 0x00;
// the total num of wiimotes for a download
int tot_wiimotes = 0;
// current wiimote
int cur_wiimote = 0;

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
    Sleep(200);
    wiiuse_set_leds(remote, 0x00);
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
    Sleep(1);

    if (end - start >= 5)
    {
        printf("\n[ERROR] Process timed out. Restarting soon...\n");
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
            return -1;
    } while (!compare_buffers(buffer1, buffer2) && failures++ < 10);
    // this will occur if matches SUCK or keep sucking
    if (failures >= 10)
    {
        printf("\n[ERROR] Upload timed out. Restarting soon...\n");
        return -1;
    }

    return 1;
}

int write_file(wiimote *remote, char *buffer, char *file_name, int address, FILE *fp, int restarted)
{
    char *buf_ptr = buffer;
    char check_buf[16]; // used to redownload and check state
    int res = findSize(file_name);
    buf_ptr += address;

    int i = 0;
    // upload the rest of the file
    while (payload_received < payload_size)
    {
        // only read the file if we ARENT restarting the download
        while (i++ < 16 && !restarted)
            fread(buf_ptr++, 1, sizeof(char), fp);
        // if we hit 16, reset everything
        if (validate_upload(remote, buffer, check_buf, address) == -1)
            return address;

        // reset vals
        restarted = 0; // if we successfully upload, and we restarted a upload, set restarted to false
        i         = 0;
        buf_ptr   = buffer;
        address += 0x10;
        payload_received += 16;

        // print progress, continue reading file
        print_progress(remote, "UPLOAD PROGRESS:", (float)payload_received, (float)payload_size);
    }
    payload_received = payload_size;
    print_progress(remote, "UPLOAD PROGRESS:", (float)payload_received, (float)payload_size);

    // close file and alert user
    fclose(fp);
    alert_remote(remote);

    return -1;
}

void handle_upload_request(wiimote **wiimotes, char *file_name, WiimotePartialFile *wpf)
{
    int address = 0;            // result from an operation
    FILE *fp;               // the file we are reading/writing to
    char buffer[16];        // used to hold data received/sent
    int restarted_task = 0; // set if we ever fail a task
    // setup wpf
    char wpf_name[39];

    // set up metadata
    create_wpf_files(file_name, wpf);
    generate_wpf_file_name(wpf_name, wpf);
    fopen_s(&fp, wpf_name, "rb");
    printf("[INFO] Creating and uploading %s\n", wpf_name);

    do
    {
        address = write_file(wiimotes[wpf->cur_wpf - 1], buffer, file_name, address, fp, restarted_task);

        // handle resulting output
        if (address == -1)
        {
            // we finished writing wpf file, now we download it!
            printf("[INFO] Removing %s\n", wpf_name);
            remove(wpf_name);
            // move up a wpf
            wpf->cur_wpf++;
            if (wpf->cur_wpf > wpf->tot_wpf)
            {
                printf("[INFO] All wpf's written. Cleaning up.\n");
                break;
            }
            // open the next wpf
            generate_wpf_file_name(wpf_name, wpf);
            printf("[INFO] Creating and uploading %s\n", wpf_name);
            fopen_s(&fp, wpf_name, "rb");
        } else if (address >= 0)
        {
            // this prepares to call the method to restart at a specific point
            restarted_task = 1;
            // completely restart the app
            wiiuse_cleanup(wiimotes, MAX_WIIMOTES);
            wiimotes = connect_remotes();
            printf("\n");
        }
    } while (any_wiimote_connected(wiimotes, MAX_WIIMOTES) && address != -2);
}

int download_header(wiimote *remote, char *buffer, char *file_name, WiimotePartialFile *wpf)
{
    char *file_pos = buffer;
    char read_buf[16];

    printf("[INFO] Estimating size...\n");
    if (read_from_wiimote(remote, file_pos, 0x00) == -1)
        return -1;

    header *ret = (header *)file_pos; // reads header info
    // set nums
    payload_size     = convert_to_uint32((uint8_t *)&(ret->file_size_on_remote));
    tot_wiimotes     = convert_to_uint16((uint8_t *)&(ret->total_remotes));
    cur_wiimote      = convert_to_uint16((uint8_t *)&(ret->curr_remote_num));
    payload_received = 0;
    // exit if corrupted
    if (payload_size <= 0 || payload_size > MAX_WIIMOTE_PAYLOAD)
    {
        printf("[ERROR] Download size of %dB is invalid\n", payload_size);
        return -2;
    }
    file_pos += 16;

    Sleep(1); // read file NAME
    if (read_from_wiimote(remote, read_buf, 0x10) == -1)
        return -1;

    int i = 0;
    while (i < 16 && read_buf[i] != -52)
    {
        wpf->file_name[i] = read_buf[i];
        file_pos[i]       = read_buf[i];
        i++;
    }
    wpf->file_name[i] = 0;
    file_pos += 16;

    Sleep(1); // read file EXTENSION
    if (read_from_wiimote(remote, read_buf, 0x20) == -1)
        return -1;

    i = 0;
    while (i < 16 && file_pos[i] != -52)
    {
        wpf->file_ext[i] = read_buf[i];
        file_pos[i]      = read_buf[i];
        i++;
    }
    wpf->file_ext[i] = 0;
    file_pos += 16;
    Sleep(1);

    sprintf_s(file_name, 39, "%s%s%d.wpf", wpf->file_name, wpf->file_ext, cur_wiimote);
    printf("[INFO] File found: %s\n", file_name);
    printf("[INFO] Beginning download. Total size is %dB\n", payload_size);

    return 1;
}

/**
 * @brief download_file
 *
 * @param wiimote *remote, char *file_buffer, char *file_name, int address
 *
 * Downloads a single file on a given remote
 */
int download_file(wiimote *remote, char *file_buffer, char *file_name, int address, WiimotePartialFile *wpf)
{
    char *file_pos = file_buffer;

    // get header data
    if (address < 0x30)
    {
        switch (download_header(remote, file_pos, file_name, wpf))
        {
        case 1:
            print_progress(remote, "DATA DOWNLOADED:", (float)payload_received, (float)payload_size);
            address = 0x30; // success! continue
            file_pos += 0x30;
            break;
        case -1:
            return 0x00; // timed out
        case -2:
            return -2; // invalid header
        }
    } else
    {
        printf("[INFO] Resuming download. %dB out of %dB\n", payload_received, payload_size);
        file_pos += address;
    }

    // read 16 at a time
    // this downloads the file at file_pos of the total buffer
    while (payload_received < payload_size)
    {
        if (read_from_wiimote(remote, file_pos, address) == -1)
            return address;

        // update payload and file pos
        payload_received += 16;
        file_pos += 16;
        address += 0x10;
        print_progress(remote, "DATA DOWNLOADED:", (float)payload_received, (float)payload_size);
    }
    payload_received = payload_size;
    print_progress(remote, "DATA DOWNLOADED:", (float)payload_received, (float)payload_size);

    // save downloaded data
    printf("\n[INFO] Data successfully downloaded. Preparing to write to file\n");
    FILE *fp;
    fopen_s(&fp, file_name, "wb");
    fwrite(file_buffer, payload_size + 0x30, sizeof(char), fp);
    // end download
    printf("[INFO] Data successfully written\n");
    alert_remote(remote);
    fclose(fp);

    return -1;
}

void handle_download_request(wiimote **wiimotes, char *file_name, WiimotePartialFile *wpf)
{
    int address = 0;                      // result from an operation
    char buffer[MAX_WIIMOTE_PAYLOAD]; // used to hold data received/sent

    do
    {
        address = download_file(wiimotes[wpf->cur_wpf - 1], buffer, file_name, address, wpf);

        // handler
        if (address >= 0)
        {
            // this prepares to call the method to restart at a specific point
            // completely restart the app
            wiiuse_cleanup(wiimotes, MAX_WIIMOTES);
            wiimotes = connect_remotes();
            printf("\n");
        } else if (address == -1)
        {
            // update download stuff
            wpf->cur_wpf++;
            if (wpf->cur_wpf > tot_wiimotes)
            {
                printf("[INFO] All wpf's written. Cleaning up.\n");
                break;
            }
        }
    } while (any_wiimote_connected(wiimotes, MAX_WIIMOTES) && address != -2);
}

void run_selected_process(wiimote **wiimotes, char *file_name, int mode)
{
    // this data is used to upload/download data
    WiimotePartialFile wpf;
    char name[17];
    char ext[17];
    wpf.file_name = name;
    wpf.file_ext  = ext;
    wpf.cur_wpf   = 1;

    switch (mode)
    {
    case 0:
        handle_upload_request(wiimotes, file_name, &wpf);
        break;
    case 1:
        handle_download_request(wiimotes, file_name, &wpf);
        break;
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
        char file[39];
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

    wiiuse_set_leds(wiimotes[0], 0x00);
    wiiuse_set_leds(wiimotes[1], 0x00);
    // char buffer[16];
    // if (read_from_wiimote(wiimotes[0], buffer, 0x1430) == -1)
    // {
    //     return -1;
    // }
    // int i = 0;
    // while (i < 16 && buffer[i] != -52)
    // {
    //     printf("%2x ", buffer[i]);
    //     i++;
    // }
    // return 0;
    run_selected_process(wiimotes, file_name, mode);

    // wait for rumble input to end
    printf("\n[INFO] Exiting...\n");
    wiiuse_cleanup(wiimotes, MAX_WIIMOTES);

    return 0;
}
