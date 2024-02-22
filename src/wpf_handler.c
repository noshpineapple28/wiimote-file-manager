/**
 * wpf_handler
 * author: Nat Manoucheri
 * last revision: 2/19/2024
 *
 * purpose: to generate .wpf files and then
 *      upload them to wii remotes. the safest way
 *      to ensure all data is correctly sent to the
 *      remotes is by first creating .wpf files to send
 *      to them
 *  wpf stands for Wiimote Partial File. A portion of an
 *      original file is saved in this document, and then
 *      sent to the remote. Any file with the extension
 *      of .wpf and correct heading as a result will be able
 *      to immediately send data, no preparation necessary
 */

#include "wpf_handler.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define MAX_FILE_SIZE 5312

int get_file_name2(char *file_name, WiimotePartialFile *wpf)
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
            ext_len   = ((int)strlen(file_name)) - i - 1;
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
        wpf->file_name[i] = file_name[last_slash_index + 1 + i];
        i++;
    }
    wpf->file_name[i++] = '\0';
    while (i < 16)
    {
        wpf->file_name[i++] = 0xcc;
    }
    // write extension
    i = 0;
    while (i < ext_len)
    {
        wpf->file_ext[i] = file_name[last_slash_index + file_len + 2 + i];
        i++;
    }
    wpf->file_ext[i++] = '\0';
    while (i < 16)
    {
        wpf->file_ext[i++] = 0xcc;
    }

    return 1;
}

int32_t find_size(char *file_name, WiimotePartialFile *wpf)
{
    // opening the file in read mode
    FILE *fp;
    fopen_s(&fp, file_name, "rb");

    // checking if the file exist or not
    if (fp == NULL)
    {
        return 0;
    }

    fseek(fp, 0L, SEEK_END);

    // calculating the size of the file
    long int res = ftell(fp);

    // closing the file
    wpf->file_size = res;
    fclose(fp);

    return res;
}

int prepare_data(char *file_name, WiimotePartialFile *wpf)
{
    // sets tot file size
    int size = (int)find_size(file_name, wpf);
    if (!size)
    {
        printf("[ERROR] Invalid file size\n");
        return 0;
    }
    // sets file name, file ext
    int res = get_file_name2(file_name, wpf);
    if (!res)
    {
        printf("[ERROR] Invalid file name\n");
        return 0;
    }

    // sets total wpfs, and cur wpf size
    int total_wpfs = (int)ceil(((float)size) / ((float)MAX_FILE_SIZE));
    wpf->tot_wpf   = total_wpfs;
    wpf->cur_wpf   = 1;
    // set cur wpf size
    if (total_wpfs == 1) // if only one wpf, then set size = file size
    {
        wpf->cur_wpf_size = size;
    } else
    {
        wpf->cur_wpf_size = MAX_FILE_SIZE;
    }

    return 1;
}

int generate_header(FILE *wpf_file, WiimotePartialFile *metadata)
{
    char header_buf[16] = {(metadata->file_size >> 24),
                           (metadata->file_size << 8) >> 24,
                           (metadata->file_size << 16) >> 24,
                           (metadata->file_size << 24) >> 24,

                           (metadata->cur_wpf_size >> 24),
                           (metadata->cur_wpf_size << 8) >> 24,
                           (metadata->cur_wpf_size << 16) >> 24,
                           (metadata->cur_wpf_size << 24) >> 24,

                           ((metadata->tot_wpf) >> 8) << 8,
                           ((metadata->tot_wpf) << 8) >> 8,
                           ((metadata->cur_wpf) >> 8) << 8,
                           ((metadata->cur_wpf) << 8) >> 8,
                           0x00,
                           0x00,
                           0x00,
                           0x00};

    fwrite(header_buf, sizeof(char), 16, wpf_file);
    fwrite(metadata->file_name, sizeof(char), 16, wpf_file);
    fwrite(metadata->file_ext, sizeof(char), 16, wpf_file);

    return 1;
}

int generate_wpf(FILE *fp, FILE *wpf_file, WiimotePartialFile *metadata)
{
    if (!generate_header(wpf_file, metadata))
    {
        printf("[ERROR] Could not generate header\n");
        return 0;
    }

    char buffer[MAX_FILE_SIZE];
    fread(buffer, metadata->cur_wpf_size, sizeof(char), fp);
    fwrite(buffer, metadata->cur_wpf_size, sizeof(char), wpf_file);

    return 1;
}

int generate_wpf_file_name(char *buffer, WiimotePartialFile *metadata)
{
    sprintf_s(buffer, 39, "%s%s%d.wpf", metadata->file_name, metadata->file_ext, metadata->cur_wpf);

    return 1;
}

int create_wpf_files(char *file_name, WiimotePartialFile *metadata)
{
    FILE *fp;      // the file to read from
    FILE *cur_wpf; // the current wpf to write to

    // open file
    if (fopen_s(&fp, file_name, "rb"))
    {
        printf("[ERROR] Could not open file %s for reading\n", file_name);
        return 0;
    }
    // init metadata
    if (!prepare_data(file_name, metadata))
    {
        return 0;
    }
    // generate file_data
    char wpf_name[39]; // will hold the wpf file name
    while (metadata->cur_wpf <= metadata->tot_wpf)
    {
        if (!generate_wpf_file_name(wpf_name, metadata))
        {
            printf("[ERROR] Failed to create .wpf file. Failure at wpf num %d out of %d\n", metadata->cur_wpf,
                   metadata->tot_wpf);
            return 0;
        }
        if (fopen_s(&cur_wpf, wpf_name, "wb"))
        {
            printf("[ERROR] Could not create/write to .wpf %s\n", file_name);
            return 0;
        }
        generate_wpf(fp, cur_wpf, metadata);

        if (metadata->tot_wpf == 1)
        {
            fclose(cur_wpf);
            break;
        }

        metadata->cur_wpf_size = metadata->file_size - (MAX_FILE_SIZE * metadata->cur_wpf);
        metadata->cur_wpf++;
        fclose(cur_wpf);
    }

    fclose(fp);
    return metadata->tot_wpf;
}

int stitch_together_wpfs(WiimotePartialFile *wpf)
{
    FILE *stitched_file;
    char stitched_file_name[34];
    if (wpf->file_ext[0] != 0)
        sprintf_s(stitched_file_name, 35, "%s.%s", wpf->file_name, wpf->file_ext);
    else
        sprintf_s(stitched_file_name, 35, "%s", wpf->file_name);
    // open our file for writing
    if (fopen_s(&stitched_file, stitched_file_name, "wb"))
    {
        printf("[ERROR] Could not create file %s for stitching.\n", stitched_file_name);
        return 0;
    }
    printf("[INFO] Creating file %s\n", stitched_file_name);

    // begin stitching files
    wpf->cur_wpf = 1;
    char wpf_name[39];
    char write_buffer[MAX_FILE_SIZE];
    FILE *wpf_file;
    // run through all .wpf files downloaded and stitch together
    while (wpf->cur_wpf <= wpf->tot_wpf)
    {
        // ensure the file exists, if not, exit app
        generate_wpf_file_name(wpf_name, wpf);
        if (fopen_s(&wpf_file, wpf_name, "rb"))
        {
            printf("[ERROR] Could not read .wpf file %s. If missing, please redownload. If locked, please "
                   "close the program currently reading it.\n",
                   wpf_name);
            return 0;
        }
        printf("[INFO] Stitching file %s\n", wpf_name);
        // use the returned size, not wpf->cur_wpf_size, this func doesnt set that value
        int wpf_size = (int)find_size(wpf_name, wpf);
        // skip the first 0x30 data
        fread(write_buffer, sizeof(char), 0x30, wpf_file);
        fread(write_buffer, sizeof(char), wpf_size - 0x30, wpf_file);
        // write to the buffer
        fwrite(write_buffer, sizeof(char), wpf_size - 0x30, stitched_file);
        // close and remove the wpf
        fclose(wpf_file);
        remove(wpf_name);
        wpf->cur_wpf++;
    }
    // close and exit
    fclose(stitched_file);

    return 1;
}