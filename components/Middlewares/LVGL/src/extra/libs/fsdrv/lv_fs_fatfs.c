/**
 * @file lv_fs_fatfs.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../../lvgl.h"

#if LV_USE_FS_FATFS
#include "ff.h"

/*********************
 *      DEFINES
 *********************/

#if LV_FS_FATFS_LETTER == '\0'
    #error "LV_FS_FATFS_LETTER must be an upper case ASCII letter"
#endif
uint8_t fs_open_flag = 1;
uint8_t fs_read_flag = 1;
uint8_t fs_write_flag = 1;
uint8_t fs_dir_open_flag = 1;
uint8_t fs_dir_read_flag = 1;
__attribute__((weak)) bool lv_fs_fatfs_access_begin(void)
{
    return true;
}

__attribute__((weak)) void lv_fs_fatfs_access_end(void)
{
}

__attribute__((weak)) bool lv_fs_fatfs_session_begin(void)
{
    return true;
}

__attribute__((weak)) void lv_fs_fatfs_session_end(void)
{
}

__attribute__((weak)) FRESULT lv_fs_fatfs_file_close(FIL *file_p)
{
    FRESULT res = FR_NOT_READY;
    if(lv_fs_fatfs_access_begin()) {
        res = f_close(file_p);
        lv_fs_fatfs_access_end();
    }
    lv_fs_fatfs_session_end();
    return res;
}
/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void fs_init(void);

static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode);
static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p);
static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br);
static lv_fs_res_t fs_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw);
static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence);
static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p);
static void * fs_dir_open(lv_fs_drv_t * drv, const char * path);
static lv_fs_res_t fs_dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn);
static lv_fs_res_t fs_dir_close(lv_fs_drv_t * drv, void * dir_p);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_fs_fatfs_init(void)
{
    /*----------------------------------------------------
     * Initialize your storage device and File System
     * -------------------------------------------------*/
    fs_init();

    /*---------------------------------------------------
     * Register the file system interface in LVGL
     *--------------------------------------------------*/

    /*Add a simple drive to open images*/
    static lv_fs_drv_t fs_drv; /*A driver descriptor*/
    lv_fs_drv_init(&fs_drv);

    /*Set up fields...*/
    fs_drv.letter = LV_FS_FATFS_LETTER;
    fs_drv.cache_size = LV_FS_FATFS_CACHE_SIZE;

    fs_drv.open_cb = fs_open;
    fs_drv.close_cb = fs_close;
    fs_drv.read_cb = fs_read;
    fs_drv.write_cb = fs_write;
    fs_drv.seek_cb = fs_seek;
    fs_drv.tell_cb = fs_tell;

    fs_drv.dir_close_cb = fs_dir_close;
    fs_drv.dir_open_cb = fs_dir_open;
    fs_drv.dir_read_cb = fs_dir_read;

    lv_fs_drv_register(&fs_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your Storage device and File system.*/
static void fs_init(void)
{
    /*Initialize the SD card and FatFS itself.
     *Better to do it in your code to keep this library untouched for easy updating*/
}

/**
 * Open a file
 * @param drv pointer to a driver where this function belongs
 * @param path path to the file beginning with the driver letter (e.g. S:/folder/file.txt)
 * @param mode read: FS_MODE_RD, write: FS_MODE_WR, both: FS_MODE_RD | FS_MODE_WR
 * @return pointer to FIL struct or NULL in case of fail
 */
static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    uint8_t flags = 0;

    if(mode == LV_FS_MODE_WR) flags = FA_WRITE | FA_OPEN_ALWAYS;
    else if(mode == LV_FS_MODE_RD) flags = FA_READ;
    else if(mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;

    if(!lv_fs_fatfs_session_begin()) return NULL;

    FIL * f = lv_mem_alloc(sizeof(FIL));
    if(f == NULL) {
        lv_fs_fatfs_session_end();
        return NULL;
    }
    lv_memset_00(f, sizeof(FIL));

    if(!lv_fs_fatfs_access_begin()) {
        lv_mem_free(f);
        lv_fs_fatfs_session_end();
        return NULL;
    }

	fs_open_flag = 0;
    FRESULT res = f_open(f, path, flags);
	fs_open_flag = 1;
    lv_fs_fatfs_access_end();
    if(res == FR_OK) {
        return f;
    }
    else {
        lv_mem_free(f);
        lv_fs_fatfs_session_end();
        return NULL;
    }
}

/**
 * Close an opened file
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FIL variable. (opened with fs_open)
 * @return LV_FS_RES_OK: no error, the file is read
 *         any error from lv_fs_res_t enum
 */
static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p)
{
    LV_UNUSED(drv);
    FRESULT res = lv_fs_fatfs_file_close(file_p);
    lv_mem_free(file_p);
    return res == FR_OK ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

/**
 * Read data from an opened file
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FIL variable.
 * @param buf pointer to a memory block where to store the read data
 * @param btr number of Bytes To Read
 * @param br the real number of read bytes (Byte Read)
 * @return LV_FS_RES_OK: no error, the file is read
 *         any error from lv_fs_res_t enum
 */
static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    LV_UNUSED(drv);
	if(!lv_fs_fatfs_access_begin()) {
        if(br != NULL) *br = 0;
        return LV_FS_RES_UNKNOWN;
    }
	UINT bytes_read = 0;
	fs_read_flag = 0;
    FRESULT res = f_read(file_p, buf, btr, &bytes_read);
	fs_read_flag = 1;
    lv_fs_fatfs_access_end();
    if(br != NULL) *br = bytes_read;
    if(res == FR_OK) return LV_FS_RES_OK;
    else return LV_FS_RES_UNKNOWN;
}

/**
 * Write into a file
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FIL variable
 * @param buf pointer to a buffer with the bytes to write
 * @param btw Bytes To Write
 * @param bw the number of real written bytes (Bytes Written). NULL if unused.
 * @return LV_FS_RES_OK or any error from lv_fs_res_t enum
 */
static lv_fs_res_t fs_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw)
{
    LV_UNUSED(drv);
	if(!lv_fs_fatfs_access_begin()) {
        if(bw != NULL) *bw = 0;
        return LV_FS_RES_UNKNOWN;
    }
	UINT bytes_written = 0;
	fs_write_flag = 0;
    FRESULT res = f_write(file_p, buf, btw, &bytes_written);
	fs_write_flag = 1;
    lv_fs_fatfs_access_end();
    if(bw != NULL) *bw = bytes_written;
    if(res == FR_OK) return LV_FS_RES_OK;
    else return LV_FS_RES_UNKNOWN;
}

/**
 * Set the read write pointer. Also expand the file size if necessary.
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FIL variable. (opened with fs_open )
 * @param pos the new position of read write pointer
 * @param whence only LV_SEEK_SET is supported
 * @return LV_FS_RES_OK: no error, the file is read
 *         any error from lv_fs_res_t enum
 */
static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    if(!lv_fs_fatfs_access_begin()) return LV_FS_RES_UNKNOWN;
    FRESULT res = FR_INVALID_PARAMETER;
    switch(whence) {
        case LV_FS_SEEK_SET:
            res = f_lseek(file_p, pos);
            break;
        case LV_FS_SEEK_CUR:
            res = f_lseek(file_p, f_tell((FIL *)file_p) + pos);
            break;
        case LV_FS_SEEK_END:
            res = f_lseek(file_p, f_size((FIL *)file_p) + pos);
            break;
        default:
            break;
    }
    lv_fs_fatfs_access_end();
    return res == FR_OK ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

/**
 * Give the position of the read write pointer
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FIL variable.
 * @param pos_p pointer to to store the result
 * @return LV_FS_RES_OK: no error, the file is read
 *         any error from lv_fs_res_t enum
 */
static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    LV_UNUSED(drv);
    if(!lv_fs_fatfs_access_begin()) return LV_FS_RES_UNKNOWN;
    *pos_p = f_tell((FIL *)file_p);
    lv_fs_fatfs_access_end();
    return LV_FS_RES_OK;
}

/**
 * Initialize a 'DIR' variable for directory reading
 * @param drv pointer to a driver where this function belongs
 * @param path path to a directory
 * @return pointer to an initialized 'DIR' variable
 */
static void * fs_dir_open(lv_fs_drv_t * drv, const char * path)
{
    LV_UNUSED(drv);
    if(!lv_fs_fatfs_session_begin()) return NULL;
    FF_DIR * d = lv_mem_alloc(sizeof(FF_DIR));
    if(d == NULL) {
        lv_fs_fatfs_session_end();
        return NULL;
    }

    if(!lv_fs_fatfs_access_begin()) {
        lv_mem_free(d);
        lv_fs_fatfs_session_end();
        return NULL;
    }

	fs_dir_open_flag = 0;
    FRESULT res = f_opendir(d, path);
	fs_dir_open_flag = 1;
    lv_fs_fatfs_access_end();
    if(res != FR_OK) {
        lv_mem_free(d);
        d = NULL;
        lv_fs_fatfs_session_end();
    }
    return d;
}

/**
 * Read the next filename from a directory.
 * The name of the directories will begin with '/'
 * @param drv pointer to a driver where this function belongs
 * @param dir_p pointer to an initialized 'DIR' variable
 * @param fn pointer to a buffer to store the filename
 * @return LV_FS_RES_OK or any error from lv_fs_res_t enum
 */
static lv_fs_res_t fs_dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn)
{
    LV_UNUSED(drv);
    if(!lv_fs_fatfs_access_begin()) return LV_FS_RES_UNKNOWN;
    FRESULT res;
    FILINFO fno;
    fn[0] = '\0';

    do {
		fs_dir_read_flag = 0;
        res = f_readdir(dir_p, &fno);
		fs_dir_read_flag = 1;
        if(res != FR_OK) {
            lv_fs_fatfs_access_end();
            return LV_FS_RES_UNKNOWN;
        }

        if(fno.fattrib & AM_DIR) {
            lv_snprintf(fn, LV_FS_MAX_FN_LENGTH, "/%s", fno.fname);
        }
        else lv_snprintf(fn, LV_FS_MAX_FN_LENGTH, "%s", fno.fname);

    } while(strcmp(fn, "/.") == 0 || strcmp(fn, "/..") == 0);

    lv_fs_fatfs_access_end();
    return LV_FS_RES_OK;
}

/**
 * Close the directory reading
 * @param drv pointer to a driver where this function belongs
 * @param dir_p pointer to an initialized 'DIR' variable
 * @return LV_FS_RES_OK or any error from lv_fs_res_t enum
 */
static lv_fs_res_t fs_dir_close(lv_fs_drv_t * drv, void * dir_p)
{
    LV_UNUSED(drv);
    FRESULT res = FR_NOT_READY;
    if(lv_fs_fatfs_access_begin()) {
        res = f_closedir(dir_p);
        lv_fs_fatfs_access_end();
    }
    lv_mem_free(dir_p);
    lv_fs_fatfs_session_end();
    return res == FR_OK ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

#else /*LV_USE_FS_FATFS == 0*/

#if defined(LV_FS_FATFS_LETTER) && LV_FS_FATFS_LETTER != '\0'
    #warning "LV_USE_FS_FATFS is not enabled but LV_FS_FATFS_LETTER is set"
#endif

#endif /*LV_USE_FS_POSIX*/

