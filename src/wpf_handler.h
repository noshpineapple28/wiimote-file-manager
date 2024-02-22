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
 *
 */
#ifndef WPF_HANDLER
#define WPF_HANDLER
#include <stdint.h>
#include <stdio.h>

typedef struct WiimotePartialFile
{
    // these nums are used for the first line of the header
    int file_size;
    int cur_wpf_size;
    // the wiimotes we're on
    int tot_wpf;
    int cur_wpf;

    // file name and type
    char *file_name;
    char *file_ext;
} WiimotePartialFile;

/**
 * @brief get_file_name
 *
 * @param char* file_name - the file name we are breaking down
 * @param WiimotePartialFile* wpf - the wpf to save the gathered data to
 *
 * @returns 1 on success, 0 on failure
 *
 * prepares the file name for the wpf
 */
int get_file_name2(char *file_name, WiimotePartialFile *wpf);

/**
 * @brief find_size
 *
 * @param char* file_name - the file we are reading data from
 * @param WiimotePartialFile* wpf - the wpf structure we are updating
 *
 * @returns 1 on success, 0 on failure
 *
 * prepares a wpf pointer with data gathered from a preparation of data
 */
int32_t find_size(char *file_name, WiimotePartialFile *wpf);

/**
 * @brief prepare_data
 *
 * @param char* file_name - the file we are reading from
 * @param WiimotePartialFile* wpf - the wpf to save the gathered data to
 *
 * @returns 1 on success, 0 on failure
 *
 * prepares a wpf pointer with data gathered from a preparation of data
 */
int prepare_data(char *file_name, WiimotePartialFile *wpf);

/**
 * @brief generate_header
 *
 * @param FILE* wpf_file - the file we are writing to
 * @param WiimotePartialFile* metadata - the wpf containing info on our current wpf
 *
 * @returns 1 on success, 0 on failure
 *
 * using our current wpf, we generate a header to the current wpf file
 */
int generate_header(FILE *wpf_file, WiimotePartialFile *metadata);

/**
 * @brief generate_wpf
 *
 * @param FILE* fp - the file we are reading from
 * @param FILE* wpf_file - the wpf file we are writing to
 * @param WiimotePartialFile* metadata - the wpf containing info on our current wpf
 *
 * @returns 1 on success, 0 on failure
 *
 * using our current wpf, we fill a wpf file with all necessary info
 */
int generate_wpf(FILE *fp, FILE *wpf_file, WiimotePartialFile *metadata);

/**
 * @brief generate_wpf_file_name
 *
 * @param char* buffer - an array of length 39 to save the new file name to
 * @param WiimotePartialFile* metadata - the wpf containing info on our current wpf
 *
 * @returns 1 on success, 0 on failure
 *
 * this takes our metadata and generates a wpf file name for it
 */
int generate_wpf_file_name(char *buffer, WiimotePartialFile *metadata);

/**
 * @brief create_wpf_files
 *
 * @param char* file_name - the name of the file we are reading from
 *
 * @returns 1 on success, 0 on failure
 *
 * Takes a file, and generates a number of wpf files.
 * It will return the number of wpfs necessary to upload data
 */
int create_wpf_files(char *file_name, WiimotePartialFile *metadata);

/**
 * @brief stitch_together_wpfs
 *
 * @param WiimotePartialFile* metadata - the wpf containing info necessary to redownload the image
 *
 * @returns 1 on success, 0 on failure
 *
 * Using the WPF struct generated from downloaded data, creates a
 *      file that will have the same name as the OG file
 * It will read from the .wpf files downloaded, splice them into the
 *      generated file, and remove the .wpfs
 */
int stitch_together_wpfs(WiimotePartialFile *wpf);

#endif