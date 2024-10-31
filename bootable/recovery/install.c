/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "install.h"
#include "mincrypt/rsa.h"
#include "minui/minui.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "roots.h"
#include "verifier.h"
#include "deltaupdate_config.h"

#define ASSUMED_UPDATE_BINARY_NAME  "META-INF/com/google/android/update-binary"
#define ASSUMED_DELTAUPDATE_BINARY_NAME  "META-INF/com/google/android/ipth_dua"
#define RUN_DELTAUPDATE_AGENT  "/tmp/ipth_dua"
#define PUBLIC_KEYS_FILE "/res/keys"

#define RADIO_DIFF_NAME "radio.diff"


static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
bool update_dsp_image[3];
char *dsp_image_name[] = {DSP1_DIFF_IMAGE_NAME, DSP2_DIFF_IMAGE_NAME, DSP3_DIFF_IMAGE_NAME};
char *dsp_image_output_path[] = { DSP1_DIFF_EXTRACT_PATH, DSP2_DIFF_EXTRACT_PATH, DSP3_DIFF_EXTRACT_PATH};
char *dua_update_image_name[] = { DUA_DSP1_HANDLE, DUA_DSP2_HANDLE, DUA_DSP3_HANDLE };

const ZipEntry* dsp_diff;
bool diff_image_found;

#define MAX_DSP_MBN_IMAGES 3

// Callback invoked by mzProcessZipEntryContents to write uncompressed data to flash
static bool flash_mbn_data(const unsigned char *data, int dataLen, void *cookie) {
    ssize_t ret;
    MtdWriteContext *ctx = (MtdWriteContext *) cookie;
    if (cookie == NULL) {
        LOGE("unexpected null ptr\n");
        return false;
    }

    ret = mtd_write_data(ctx, data, dataLen);
    if (ret != dataLen) {
        LOGE("error writing buffer: return value %d, expected %d\n", ret, dataLen);
        return false;
    }
    return true;
}

// Flash dsp*.mbn files included in the zip
static bool install_mbns(const ZipArchive *zip)
{
    int i;
    const char *mbn_names[MAX_DSP_MBN_IMAGES] = {"dsp1.mbn", "dsp2.mbn", "dsp3.mbn"};
    const char *mbn_partitions[MAX_DSP_MBN_IMAGES] = {"dsp1", "dsp2", "dsp3"};
    const ZipEntry *mbn_entry;
    const MtdPartition *part;
    size_t total_size, erase_size, write_size;
    MtdWriteContext *ctx;

    if (mtd_scan_partitions() < 0) {
        LOGE("error scanning mtd partitions\n");
        return false;
    }

    for (i = 0; i < MAX_DSP_MBN_IMAGES; i++)
    {
        mbn_entry = mzFindZipEntry(zip, mbn_names[i]);
        if (mbn_entry != NULL)
        {
            LOGI("found full mbn %s\n", mbn_names[i]);
            part = mtd_find_partition_by_name(mbn_partitions[i]);
            if (part == NULL) {
                LOGE("couldn't find partition %s\n", mbn_partitions[i]);
                return false;
            }
            if (mtd_partition_info(part, &total_size, &erase_size, &write_size) < 0) {
                LOGE("couldn't get partition info for %s\n", mbn_partitions[i]);
                return false;
            }
            if (total_size < mbn_entry->uncompLen) {
                LOGE("not enough room in partition %s for file of size %d "
                     "(have %d bytes)!\n", mbn_partitions[i], total_size,
                     mbn_entry->uncompLen);
                return false;
            }

            ctx = mtd_write_partition(part);
            if (ctx == NULL) {
                LOGE("couldn't open write context for %s\n", mbn_partitions[i]);
                return false;
            }
            if (!mzProcessZipEntryContents(zip, mbn_entry, flash_mbn_data, ctx)) {
                LOGE("error writing image\n");
                (void) mtd_write_close(ctx);
                return false;
            }
            if (mtd_write_close(ctx) < 0) {
                LOGE("error closing write context");
                return false;
            }
            LOGI("updating %s complete: wrote %d bytes\n",
                 mbn_partitions[i], mbn_entry->uncompLen);
        }
    }

    return true;
}

// If the package contains an update binary, extract it and run it.
static int
try_update_binary(const char *path, ZipArchive *zip, int* wipe_cache) {
    int i;
    const ZipEntry* binary_entry =
            mzFindZipEntry(zip, ASSUMED_UPDATE_BINARY_NAME);
    if (binary_entry == NULL) {
        mzCloseZipArchive(zip);
        return INSTALL_CORRUPT;
    }
    LOGI("try_update_binary(path(%s))\n",path);

    diff_image_found = false;
    for(i = 0; i < MAX_DSP_DIFF_IMAGES; i++)
    {
        dsp_diff = mzFindZipEntry(zip, dsp_image_name[i]);
        update_dsp_image[i] = false;
        if (dsp_diff)
        {
            diff_image_found = true;
            update_dsp_image[i] = true;
            LOGI("%s found \n", dsp_image_name[i]);
            char *diff_file = dsp_image_output_path[i];
            int fd_diff = creat(diff_file, 0777);

            if (fd_diff < 0){
                LOGE("cant make %s \n", diff_file);
                mzCloseZipArchive(zip);
                return INSTALL_ERROR;
            }
            else
            {
                bool ok_diff = mzExtractZipEntryToFile(zip, dsp_diff, fd_diff);
                close(fd_diff);

                if(!ok_diff){
                    LOGE("Cant copy %d \n", dsp_image_name[i]);
                    mzCloseZipArchive(zip);
                    return INSTALL_ERROR;
                }
            }
        }
    }

    if(!diff_image_found)
        LOGI("No radio diff images found \n");

    char* binary = "/tmp/update_binary";
    unlink(binary);
    int fd = creat(binary, 0755);
    if (fd < 0) {
        mzCloseZipArchive(zip);
        LOGE("Can't make %s\n", binary);
        return INSTALL_ERROR;
    }
    bool ok = mzExtractZipEntryToFile(zip, binary_entry, fd);
    close(fd);

    if (!ok) {
        LOGE("Can't copy %s\n", ASSUMED_UPDATE_BINARY_NAME);
        return INSTALL_ERROR;
    }

    int pipefd[2];
    pipe(pipefd);

    // When executing the update binary contained in the package, the
    // arguments passed are:
    //
    //   - the version number for this interface
    //
    //   - an fd to which the program can write in order to update the
    //     progress bar.  The program can write single-line commands:
    //
    //        progress <frac> <secs>
    //            fill up the next <frac> part of of the progress bar
    //            over <secs> seconds.  If <secs> is zero, use
    //            set_progress commands to manually control the
    //            progress of this segment of the bar
    //
    //        set_progress <frac>
    //            <frac> should be between 0.0 and 1.0; sets the
    //            progress bar within the segment defined by the most
    //            recent progress command.
    //
    //        firmware <"hboot"|"radio"> <filename>
    //            arrange to install the contents of <filename> in the
    //            given partition on reboot.
    //
    //            (API v2: <filename> may start with "PACKAGE:" to
    //            indicate taking a file from the OTA package.)
    //
    //            (API v3: this command no longer exists.)
    //
    //        ui_print <string>
    //            display <string> on the screen.
    //
    //   - the name of the package zip file.
    //
    char *recovery_version = "3";
    char** args = malloc(sizeof(char*) * 5);
    args[0] = binary;
    args[1] = recovery_version;   // defined in Android.mk
    args[2] = malloc(10);
    sprintf(args[2], "%d", pipefd[1]);
    args[3] = (char*)path;
    args[4] = NULL;
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        execv(binary, args);
        LOGE("Can't run %s (%s)\n", binary, strerror(errno));
        _exit(-1);
    }
    close(pipefd[1]);

    *wipe_cache = 0;

    char buffer[1024];
    FILE* from_child = fdopen(pipefd[0], "r");
    while (fgets(buffer, sizeof(buffer), from_child) != NULL) {
        char* command = strtok(buffer, " \n");
        if (command == NULL) {
            continue;
        } else if (strcmp(command, "progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            char* seconds_s = strtok(NULL, " \n");

            float fraction = strtof(fraction_s, NULL);
            int seconds = strtol(seconds_s, NULL, 10);

            ui_show_progress(fraction * (1-VERIFICATION_PROGRESS_FRACTION),
                             seconds);
        } else if (strcmp(command, "set_progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            float fraction = strtof(fraction_s, NULL);
            ui_set_progress(fraction);
        } else if (strcmp(command, "ui_print") == 0) {
            char* str = strtok(NULL, "\n");
            if (str) {
                ui_print("%s", str);
            } else {
                ui_print("\n");
            }
        } else if (strcmp(command, "wipe_cache") == 0) {
            *wipe_cache = 1;
        } else {
            LOGE("unknown command [%s]\n", command);
        }
    }
    fclose(from_child);

    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("Error in %s\n(Status %d)\n", path, WEXITSTATUS(status));
        return INSTALL_ERROR;
    }

    if (!install_mbns(zip)) {
        LOGE("Installing MBNs failed\n");
        mzCloseZipArchive(zip);
        return INSTALL_ERROR;
    }

    mzCloseZipArchive(zip);
    return INSTALL_SUCCESS;
}

// Reads a file containing one or more public keys as produced by
// DumpPublicKey:  this is an RSAPublicKey struct as it would appear
// as a C source literal, eg:
//
//  "{64,0xc926ad21,{1795090719,...,-695002876},{-857949815,...,1175080310}}"
//
// (Note that the braces and commas in this example are actual
// characters the parser expects to find in the file; the ellipses
// indicate more numbers omitted from this example.)
//
// The file may contain multiple keys in this format, separated by
// commas.  The last key must not be followed by a comma.
//
// Returns NULL if the file failed to parse, or if it contain zero keys.
static RSAPublicKey*
load_keys(const char* filename, int* numKeys) {
    RSAPublicKey* out = NULL;
    *numKeys = 0;

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        LOGE("opening %s: %s\n", filename, strerror(errno));
        goto exit;
    }

    int i;
    bool done = false;
    while (!done) {
        ++*numKeys;
        out = realloc(out, *numKeys * sizeof(RSAPublicKey));
        RSAPublicKey* key = out + (*numKeys - 1);
        if (fscanf(f, " { %i , 0x%x , { %u",
                   &(key->len), &(key->n0inv), &(key->n[0])) != 3) {
            goto exit;
        }
        if (key->len != RSANUMWORDS) {
            LOGE("key length (%d) does not match expected size\n", key->len);
            goto exit;
        }
        for (i = 1; i < key->len; ++i) {
            if (fscanf(f, " , %u", &(key->n[i])) != 1) goto exit;
        }
        if (fscanf(f, " } , { %u", &(key->rr[0])) != 1) goto exit;
        for (i = 1; i < key->len; ++i) {
            if (fscanf(f, " , %u", &(key->rr[i])) != 1) goto exit;
        }
        fscanf(f, " } } ");

        // if the line ends in a comma, this file has more keys.
        switch (fgetc(f)) {
            case ',':
                // more keys to come.
                break;

            case EOF:
                done = true;
                break;

            default:
                LOGE("unexpected character between keys\n");
                goto exit;
        }
    }

    fclose(f);
    return out;

exit:
    if (f) fclose(f);
    free(out);
    *numKeys = 0;
    return NULL;
}

static int
really_install_package(const char *path, int* wipe_cache)
{
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_print("Finding update package...\n");
    ui_show_indeterminate_progress();
    LOGI("Update location: %s\n", path);

    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return INSTALL_CORRUPT;
    }

    ui_print("Opening update package...\n");
#if ENABLE_SIGNATURE_CHECK
    int numKeys;
    RSAPublicKey* loadedKeys = load_keys(PUBLIC_KEYS_FILE, &numKeys);
    if (loadedKeys == NULL) {
        LOGE("Failed to load keys\n");
        return INSTALL_CORRUPT;
    }
    LOGI("%d key(s) loaded from %s\n", numKeys, PUBLIC_KEYS_FILE);

    // Give verification half the progress bar...
    ui_print("Verifying update package...\n");
    ui_show_progress(
            VERIFICATION_PROGRESS_FRACTION,
            VERIFICATION_PROGRESS_TIME);

    int err;
    err = verify_file(path, loadedKeys, numKeys);
    free(loadedKeys);
    LOGI("verify_file returned %d\n", err);
    if (err != VERIFY_SUCCESS) {
        LOGE("signature verification failed\n");
        return INSTALL_CORRUPT;
    }
#endif
    /* Try to open the package.
     */
    int err;
    ZipArchive zip;
    err = mzOpenZipArchive(path, &zip);
    if (err != 0) {
        LOGE("Can't open %s\n(%s)\n", path, err != -1 ? strerror(err) : "bad");
        return INSTALL_CORRUPT;
    }

    /*Do CRC check for files in zip package
     */
    int zip_file_count;
    LOGI( "number of files in zip is %d \n",zip.numEntries);
    for(zip_file_count = 0; zip_file_count < zip.numEntries; zip_file_count++)
    {
        LOGI("verifying file at index %d\n",zip_file_count);
        if(!mzIsZipEntryIntact(&zip, &zip.pEntries[zip_file_count])){
            LOGI("Zip archive corrupt at entry %d\n",zip_file_count);
            return INSTALL_CORRUPT;
        }
    }
    /* Verify and install the contents of the package.
     */
    ui_print("Installing update...\n");
    return try_update_binary(path, &zip, wipe_cache);
}

int
install_package(const char* path, int* wipe_cache, const char* install_file)
{
    FILE* install_log = fopen_path(install_file, "w");
    if (install_log) {
        fputs(path, install_log);
        fputc('\n', install_log);
    } else {
        LOGE("failed to open last_install: %s\n", strerror(errno));
    }
    int result = really_install_package(path, wipe_cache);
    if (install_log) {
        fputc(result == INSTALL_SUCCESS ? '1' : '0', install_log);
        fputc('\n', install_log);
        fclose(install_log);
    }
    return result;
}

int extract_deltaupdate_binary(const char *path)
{
    int err;
    ZipArchive zip;

    // Try to open the package.
    err = mzOpenZipArchive(path, &zip);
    if (err != 0) {
        LOGE("Can't open %s\n(%s)\n", path, err != -1 ? strerror(err) : "bad");
        return INSTALL_ERROR;
    }

    const ZipEntry* dua_entry =
            mzFindZipEntry(&zip, ASSUMED_DELTAUPDATE_BINARY_NAME);
    if (dua_entry == NULL) {
        mzCloseZipArchive(&zip);
       LOGE("Can't find %s\n", ASSUMED_DELTAUPDATE_BINARY_NAME);
        return INSTALL_ERROR;
    }

    char* deltaupdate_agent = RUN_DELTAUPDATE_AGENT;
    unlink(deltaupdate_agent);
    int fd = creat(deltaupdate_agent, 0755);
    if (fd < 0) {
        mzCloseZipArchive(&zip);
        LOGE("Can't make %s\n", deltaupdate_agent);
        return INSTALL_ERROR;
    }

    bool ok = mzExtractZipEntryToFile(&zip, dua_entry, fd);
    close(fd);
    mzCloseZipArchive(&zip);

    if (!ok) {
        LOGE("Can't copy %s\n", ASSUMED_DELTAUPDATE_BINARY_NAME);
        return INSTALL_ERROR;
    }

    return 0;
}

int run_modem_deltaupdate(void)
{
    int ret;
    int pipefd[2];
    int i;
    if (pipe(pipefd) < 0){
        LOGE("pipe creation failure \n");
        return INSTALL_ERROR;
    }

    for( i = 0; i < MAX_DSP_DIFF_IMAGES; i++)
    {
        if( update_dsp_image[i]){

            pid_t duapid = fork();

            if (duapid == -1)
            {
                LOGE("fork failed. Returning error.\n");
                return INSTALL_ERROR;
            }

            if (duapid == 0)
            {   //child process
                /*
                * argv[0] ipth_dua exeuable command itself
                * argv[1] false(default) - old binary update as a block / true - old
                * binary update as a file
                * argv[2] old binary file name. Will be used as partition name if argv[1]
                * is false
                * argv[3] diff package name
                * argv[4] flash memory block size in KB
                */
                char current_image_name[20];
                memset(current_image_name, '\0',sizeof(current_image_name));
                if( read(pipefd[0], current_image_name, sizeof(current_image_name))< 0){
                    LOGE(" Unable to start %s : Pipe read fail \n", RUN_DELTAUPDATE_AGENT);
                    _exit(-1);
                }
                char * current_output_file;
                if(strcmp(current_image_name, DUA_DSP1_HANDLE)==0)
                    current_output_file = DSP1_DIFF_EXTRACT_PATH;
                else if(strcmp(current_image_name, DUA_DSP2_HANDLE) == 0)
                    current_output_file =  DSP2_DIFF_EXTRACT_PATH;
                else if(strcmp(current_image_name, DUA_DSP3_HANDLE ) == 0)
                    current_output_file =  DSP3_DIFF_EXTRACT_PATH;

                LOGI("updating %s \n",current_image_name);
                char** args = malloc(sizeof(char*) * 5);
                args[0] = RUN_DELTAUPDATE_AGENT;
                args[1] = "false";
                args[2] = current_image_name;
                args[3] = current_output_file;
                args[4] = "128";
                args[5] = NULL;

                execv(RUN_DELTAUPDATE_AGENT, args);
                LOGE("E:Can't run %s (%s)\n", RUN_DELTAUPDATE_AGENT, strerror(errno));
                _exit(-1);
            }

            //parents process
            if (write(pipefd[1],dua_update_image_name[i], sizeof(dua_update_image_name[i])) < 0){
                LOGE("Failed to write dsp name parameter to pipe \n");
                return INSTALL_ERROR;
            }
            waitpid(duapid, &ret, 0);
            if (!WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
                LOGE("Error in %s\n(Status %d)\n", RUN_DELTAUPDATE_AGENT, WEXITSTATUS(ret));
                return INSTALL_ERROR;
            }
            LOGI("dsp%d update done \n",i+1);
        }
    }

    return INSTALL_SUCCESS;
}

int start_delta_modemupdate(const char *path)
{
    int ret = 0;

    if (!diff_image_found)
    {
        LOGE("No modem package available.\n");
        LOGE("No modem update needed. returning O.K\n");
        return DELTA_UPDATE_SUCCESS_200;
    }

    // If the package contains an delta update binary for modem update, extract it
    ret = extract_deltaupdate_binary(path);
    if(ret != 0)
    {
       LOGE("idev_extractDua returned error(%d)\n", ret);
       return ret;
    }

    // Execute modem update using delta update binary
    ret = run_modem_deltaupdate();
    LOGE("modem update result(%d)\n", ret);

    if(ret == 0)
	return DELTA_UPDATE_SUCCESS_200;
    else
	return ret;
}
