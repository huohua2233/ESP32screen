/**
 ******************************************************************************
 * @file        exfuns.c
 * @version     V1.0
 * @brief       FATFS 扩展代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "exfuns.h"
#include <stdlib.h>


#define FILE_MAX_TYPE_NUM       7       /* 最多FILE_MAX_TYPE_NUM个大类 */
#define FILE_MAX_SUBT_NUM       7       /* 最多FILE_MAX_SUBT_NUM个小类 */
#define EXFUNS_PATH_CAPACITY     (512U + 1U)

static size_t exfuns_string_length(const char *value, size_t limit)
{
    size_t length = 0;
    while (length < limit && value[length] != '\0')
    {
        length++;
    }
    return length;
}

static bool exfuns_path_copy(uint8_t *destination, size_t capacity, const char *source)
{
    if (destination == NULL || source == NULL || capacity == 0)
    {
        return false;
    }

    size_t source_length = exfuns_string_length(source, capacity);
    if (source_length >= capacity)
    {
        return false;
    }

    memcpy(destination, source, source_length + 1);
    return true;
}

static bool exfuns_path_append(uint8_t *destination, size_t capacity, const char *source)
{
    if (destination == NULL || source == NULL || capacity == 0)
    {
        return false;
    }

    size_t destination_length = exfuns_string_length((const char *)destination, capacity);
    if (destination_length >= capacity)
    {
        return false;
    }

    size_t available = capacity - destination_length;
    size_t source_length = exfuns_string_length(source, available);
    if (source_length >= available)
    {
        return false;
    }

    memcpy(destination + destination_length, source, source_length + 1);
    return true;
}

/* 文件类型定义 */
static const char *FILE_TYPE_TBL[FILE_MAX_TYPE_NUM][FILE_MAX_SUBT_NUM] = {
    {"BIN"," "," "," "," "," "," "},                        /* BIN文件 */
    {"LRC"," "," "," "," "," "," "},                        /* LRC文件 */
    {"NES", "SMS"," "," "," "," "," "},                     /* NES/SMS文件 */
    {"TXT", "C", "H"," "," "," "," "},                      /* 文本文件 */
    {"WAV", "MP3", "OGG", "FLAC", "AAC", "WMA", "MID"},     /* 音乐文件 */
    {"DB","BMP", "JPG", "JPEG", "GIF","PNG"," "},           /* 图片文件 */
    {"AVI"," "," "," "," "," "," "},                        /* 视频文件 */
};

/******************************************************************************************/
/* 公共文件区, 使用malloc的时候 */

/* 逻辑磁盘工作区(在调用任何FATFS相关函数之前,必须先给fs申请内存) */
FATFS *fs[FF_VOLUMES];  

/******************************************************************************************/


/**
 * @brief       为exfuns申请内存
 * @param       无
 * @retval      0, 成功; 1, 失败.
 */
uint8_t exfuns_init(void)
{
    uint8_t i;

    for (i = 0; i < FF_VOLUMES; i++)
    {
        if (fs[i] != NULL)
        {
            continue;
        }

        fs[i] = (FATFS *)calloc(1, sizeof(FATFS));   /* 为磁盘i工作区申请内存 */

        if (fs[i] == NULL)
        {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief       将小写字母转为大写字母,如果是数字,则保持不变.
 * @param       c : 要转换的字母
 * @retval      转换后的字母,大写
 */
uint8_t exfuns_char_upper(uint8_t c)
{
    if (c < 'A') return c;  /* 数字,保持不变. */

    if (c >= 'a')
    {
        return c - 0x20;    /* 变为大写. */
    }
    else
    {
        return c;           /* 大写,保持不变 */
    }
}

/**
 * @brief       报告文件的类型
 * @param       fname : 文件名
 * @retval      文件类型
 *   @arg       0XFF , 表示无法识别的文件类型编号.
 *   @arg       其他 , 高四位表示所属大类, 低四位表示所属小类.
 */
uint8_t exfuns_file_type(char *fname)
{
    uint8_t tbuf[5] = {0};
    const char *ext;
    const char *dot;
    size_t ext_len;

    if (fname == NULL || fname[0] == '\0')
    {
        return 0XFF;
    }

    dot = strrchr(fname, '.');
    if (dot == NULL || dot[1] == '\0')
    {
        return 0XFF;
    }

    ext = dot + 1;
    ext_len = strlen(ext);
    if (ext_len == 0 || ext_len >= sizeof(tbuf))
    {
        return 0XFF;
    }

    strcpy((char *)tbuf, ext);
    for (uint8_t i = 0; i < ext_len; i++)
    {
        tbuf[i] = exfuns_char_upper(tbuf[i]);
    }

    for (uint8_t i = 0; i < FILE_MAX_TYPE_NUM; i++)
    {
        for (uint8_t j = 0; j < FILE_MAX_SUBT_NUM; j++)
        {
            if (*FILE_TYPE_TBL[i][j] == 0) break;

            if (strcmp((const char *)FILE_TYPE_TBL[i][j], (const char *)tbuf) == 0)
            {
                return (i << 4) | j;
            }
        }
    }

    return 0XFF;
}
/**
 * @brief       获取磁盘剩余容量
 * @param       pdrv : 磁盘编号("0:"~"9:")
 * @param       total: 总容量 (KB)
 * @param       free : 剩余容量 (KB)
 * @retval      0, 正常; 其他, 错误代码
 */
uint8_t exfuns_get_free(uint8_t *pdrv, uint32_t *total, uint32_t *free)
{
    FATFS *fs1 = NULL;
    uint8_t res;
    DWORD fre_clust = 0;
    uint32_t fre_sect = 0, tot_sect = 0;

    if (pdrv == NULL || total == NULL || free == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    *total = 0;
    *free = 0;
    
    /* 得到磁盘信息及空闲簇数量 */
    if (!sd_access_begin(portMAX_DELAY))
    {
        return FR_NOT_READY;
    }
    res = (uint8_t)f_getfree((const TCHAR *)pdrv, &fre_clust, &fs1);

    if (res == FR_OK && fs1 != NULL)
    {
        tot_sect = (fs1->n_fatent - 2) * fs1->csize;    /* 得到总扇区数 */
        fre_sect = fre_clust * fs1->csize;              /* 得到空闲扇区数 */
#if FF_MAX_SS!= 512  /* 扇区大小不是512字节,则转换为512字节 */
        tot_sect *= fs1->ssize / 512;
        fre_sect *= fs1->ssize / 512;
#endif
        *total = tot_sect >> 1;     /* 单位为KB */
        *free = fre_sect >> 1;      /* 单位为KB */
    }
    else if (res == FR_OK)
    {
        res = FR_INT_ERR;
    }

    sd_access_end();

    return res;
}

/**
 * @brief       文件复制
 *   @note      将psrc文件,copy到pdst.
 *              注意: 文件大小不要超过4GB.
 *
 * @param       fcpymsg : 函数指针, 用于实现拷贝时的信息显示
 *                  pname:文件/文件夹名
 *                  pct:百分比
 *                  mode:
 *                      bit0 : 更新文件名
 *                      bit1 : 更新百分比pct
 *                      bit2 : 更新文件夹
 *                      其他 : 保留
 *                  返回值: 0, 正常; 1, 强制退出;
 *
 * @param       psrc    : 源文件
 * @param       pdst    : 目标文件
 * @param       totsize : 总大小(当totsize为0的时候,表示仅仅为单个文件拷贝)
 * @param       cpdsize : 已复制了的大小.
 * @param       fwmode  : 文件写入模式
 *   @arg       0: 不覆盖原有的文件
 *   @arg       1: 覆盖原有的文件
 *
 * @retval      执行结果
 *   @arg       0   , 正常
 *   @arg       0XFF, 强制退出
 *   @arg       其他, 错误代码
 */
uint8_t exfuns_file_copy(uint8_t(*fcpymsg)(uint8_t *pname, uint8_t pct, uint8_t mode), uint8_t *psrc, uint8_t *pdst, 
                         uint32_t totsize, uint32_t cpdsize, uint8_t fwmode)
{
    uint8_t res;
    UINT br = 0;
    UINT bw = 0;
    FIL *fsrc = 0;
    FIL *fdst = 0;
    uint8_t *fbuf = 0;
    uint8_t curpct = 0;
    unsigned long long lcpdsize = cpdsize;
    bool source_open = false;
    bool destination_open = false;
    
    fsrc = (FIL *)calloc(1, sizeof(FIL));    /* 申请内存 */
    fdst = (FIL *)calloc(1, sizeof(FIL));
    fbuf = (uint8_t *)malloc(8192);

    if (fsrc == NULL || fdst == NULL || fbuf == NULL)
    {
        res = 100;  /* 前面的值留给fatfs */
    }
    else
    {
        if (fwmode == 0)
        {
            fwmode = FA_CREATE_NEW;     /* 不覆盖 */
        }
        else 
        {
            fwmode = FA_CREATE_ALWAYS;  /* 覆盖存在的文件 */
        }
        res = sd_f_open(fsrc, (const TCHAR *)psrc, FA_READ | FA_OPEN_EXISTING);        /* 打开只读文件 */
        if (res == FR_OK)
        {
            source_open = true;
            res = sd_f_open(fdst, (const TCHAR *)pdst, FA_WRITE | fwmode);    /* 第一个打开成功,才开始打开第二个 */
            destination_open = res == FR_OK;
        }

        if (res == 0)           /* 两个都打开成功了 */
        {
            if (totsize == 0)   /* 仅仅是单个文件复制 */
            {
                totsize = fsrc->obj.objsize;
                lcpdsize = 0;
                curpct = 0;
            }
            else
            {
                curpct = (lcpdsize * 100) / totsize;            /* 得到新百分比 */
            }
            
            fcpymsg(psrc, curpct, 0X02);                        /* 更新百分比 */

            while (res == 0)    /* 开始复制 */
            {
                res = sd_f_read(fsrc, fbuf, 8192, &br);    /* 源头读出512字节 */

                if (res || br == 0)break;

                res = sd_f_write(fdst, fbuf, br, &bw);/* 写入目的文件 */
                if (res == FR_OK && bw != br)
                {
                    res = FR_DISK_ERR;
                }
                if (res != FR_OK)break;

                lcpdsize += bw;

                if (curpct != (lcpdsize * 100) / totsize)       /* 是否需要更新百分比 */
                {
                    curpct = (lcpdsize * 100) / totsize;

                    if (fcpymsg(psrc, curpct, 0X02))            /* 更新百分比 */
                    {
                        res = 0XFF;                             /* 强制退出 */
                        break;
                    }
                }
            }
        }
    }

    if (destination_open)
    {
        FRESULT close_res = sd_f_close(fdst);
        if (res == FR_OK && close_res != FR_OK)
        {
            res = (uint8_t)close_res;
        }
    }
    if (source_open)
    {
        FRESULT close_res = sd_f_close(fsrc);
        if (res == FR_OK && close_res != FR_OK)
        {
            res = (uint8_t)close_res;
        }
    }

    free(fsrc); /* 释放内存 */
    free(fdst);
    free(fbuf);
    return res;
}

/**
 * @brief       得到路径下的文件夹
 *   @note      即把路径全部去掉, 只留下文件夹名字.
 * @param       pname : 详细路径 
 * @retval      0   , 路径就是个卷标号.
 *              其他, 文件夹名字首地址
 */
uint8_t *exfuns_get_src_dname(uint8_t *pname)
{
    if (pname == NULL)
    {
        return 0;
    }

    uint8_t *component = pname;
    size_t length = 0;
    while (length < EXFUNS_PATH_CAPACITY && pname[length] != 0)
    {
        if (pname[length] == 0x5c || pname[length] == 0x2f || pname[length] == ':')
        {
            component = pname + length + 1;
        }
        length++;
    }

    if (length == EXFUNS_PATH_CAPACITY || component == pname + length)
    {
        return 0;
    }

    return component;
}

/**
 * @brief       得到文件夹大小
 *   @note      注意: 文件夹大小不要超过4GB.
 * @param       pname : 详细路径 
 * @retval      0   , 文件夹大小为0, 或者读取过程中发生了错误.
 *              其他, 文件夹大小
 */
uint32_t exfuns_get_folder_size(uint8_t *fdname)
{
    uint8_t res = 0;
    FF_DIR *fddir = 0;          /* 目录 */
    FILINFO *finfo = 0;         /* 文件信息 */
    uint8_t *pathname = 0;      /* 目标文件夹路径+文件名 */
    uint16_t pathlen = 0;       /* 目标路径长度 */
    uint32_t fdsize = 0;
    bool dir_open = false;

    fddir = (FF_DIR *)malloc(sizeof(FF_DIR));   /* 申请内存 */
    finfo = (FILINFO *)malloc(sizeof(FILINFO));

    if (fddir == NULL || finfo == NULL)res = 100;

    if (res == 0)
    {
        pathname = malloc(EXFUNS_PATH_CAPACITY);

        if (pathname == NULL)res = 101;

        if (res == 0)
        {
            if (!exfuns_path_copy(pathname, EXFUNS_PATH_CAPACITY, (const char *)fdname))
            {
                res = FR_INVALID_NAME;
            }
            else
            {
				res = sd_f_opendir(fddir, (const TCHAR *)fdname);      /* 打开源目录 */
            }
			dir_open = res == FR_OK;
            if (res == 0)           /* 打开目录成功 */
            {
                while (res == 0)    /* 开始复制文件夹里面的东东 */
                {
					res = sd_f_readdir(fddir, finfo);                      /* 读取目录下的一个文件 */
                    if (res != FR_OK || finfo->fname[0] == 0) break;    /* 错误了/到末尾了,退出 */

                    if (finfo->fname[0] == '.')continue;                /* 忽略上级目录 */

                    if (finfo->fattrib & 0X10)   /* 是子目录(文件属性,0X20,归档文件;0X10,子目录;) */
                    {
                        pathlen = strlen((const char *)pathname);       /* 得到当前路径的长度 */
                        if (!exfuns_path_append(pathname, EXFUNS_PATH_CAPACITY, "/") ||
                            !exfuns_path_append(pathname, EXFUNS_PATH_CAPACITY, finfo->fname))
                        {
                            pathname[pathlen] = 0;
                            res = FR_INVALID_NAME;
                            break;
                        }
                        //printf("\r\nsub folder:%s\r\n",pathname);     /* 打印子目录名 */
                        fdsize += exfuns_get_folder_size(pathname);     /* 得到子目录大小,递归调用 */
                        pathname[pathlen] = 0;                          /* 加入结束符 */
                    }
                    else
                    {
                        fdsize += finfo->fsize; /* 非目录,直接加上文件的大小 */
                    }
                }
            }

            free(pathname);
        }
    }

    if (dir_open)
    {
        FRESULT close_res = sd_f_closedir(fddir);
        if (res == FR_OK && close_res != FR_OK)
        {
            res = (uint8_t)close_res;
        }
    }
    free(fddir);
    free(finfo);

    if (res)
    {
        return 0;
    }
    else 
    {
        return fdsize;
    }
}

/**
 * @brief       文件夹复制
 *   @note      将psrc文件夹, 拷贝到pdst文件夹.
 *              注意: 文件大小不要超过4GB.
 *
 * @param       fcpymsg : 函数指针, 用于实现拷贝时的信息显示
 *                  pname:文件/文件夹名
 *                  pct:百分比
 *                  mode:
 *                      bit0 : 更新文件名
 *                      bit1 : 更新百分比pct
 *                      bit2 : 更新文件夹
 *                      其他 : 保留
 *                  返回值: 0, 正常; 1, 强制退出;
 *
 * @param       psrc    : 源文件夹
 * @param       pdst    : 目标文件夹
 *   @note      必须形如"X:"/"X:XX"/"X:XX/XX"之类的. 且要确认上一级文件夹存在
 *
 * @param       totsize : 总大小(当totsize为0的时候,表示仅仅为单个文件拷贝)
 * @param       cpdsize : 已复制了的大小.
 * @param       fwmode  : 文件写入模式
 *   @arg       0: 不覆盖原有的文件
 *   @arg       1: 覆盖原有的文件
 *
 * @retval      执行结果
 *   @arg       0   , 正常
 *   @arg       0XFF, 强制退出
 *   @arg       其他, 错误代码
 */
uint8_t exfuns_folder_copy(uint8_t(*fcpymsg)(uint8_t *pname, uint8_t pct, uint8_t mode), uint8_t *psrc, uint8_t *pdst, 
                           uint32_t *totsize, uint32_t *cpdsize, uint8_t fwmode)
{
    uint8_t res = 0;
    FF_DIR *srcdir = 0;     /* 源目录 */
    FF_DIR *dstdir = 0;     /* 源目录 */
    FILINFO *finfo = 0;     /* 文件信息 */
    uint8_t *fn = 0;        /* 长文件名 */

    uint8_t *dstpathname = 0;   /* 目标文件夹路径+文件名 */
    uint8_t *srcpathname = 0;   /* 源文件夹路径+文件名 */

    uint16_t dstpathlen = 0;    /* 目标路径长度 */
    uint16_t srcpathlen = 0;    /* 源路径长度 */
    bool source_dir_open = false;


    srcdir = (FF_DIR *)malloc(sizeof(FF_DIR));  /* 申请内存 */
    dstdir = (FF_DIR *)malloc(sizeof(FF_DIR));
    finfo = (FILINFO *)malloc(sizeof(FILINFO));

    if (srcdir == NULL || dstdir == NULL || finfo == NULL)res = 100;

    if (res == 0)
    {
        dstpathname = malloc(EXFUNS_PATH_CAPACITY);
        srcpathname = malloc(EXFUNS_PATH_CAPACITY);

        if (dstpathname == NULL || srcpathname == NULL)res = 101;

        if (res == 0)
        {
            if (!exfuns_path_copy(srcpathname, EXFUNS_PATH_CAPACITY, (const char *)psrc) ||
                !exfuns_path_copy(dstpathname, EXFUNS_PATH_CAPACITY, (const char *)pdst))
            {
                res = FR_INVALID_NAME;
            }
            else
            {
				res = sd_f_opendir(srcdir, (const TCHAR *)psrc);       /* 打开源目录 */
            }
			source_dir_open = res == FR_OK;

            if (res == 0)       /* 打开目录成功 */
            {
                if (!exfuns_path_append(dstpathname, EXFUNS_PATH_CAPACITY, "/"))
                {
                    res = FR_INVALID_NAME;
                }
                fn = exfuns_get_src_dname(psrc);

                if (res == FR_OK && fn == 0)    /* 卷标拷贝 */
                {
                    dstpathlen = strlen((const char *)dstpathname);
                    if ((size_t)dstpathlen + 1 >= EXFUNS_PATH_CAPACITY)
                    {
                        res = FR_INVALID_NAME;
                    }
                    else
                    {
                        dstpathname[dstpathlen] = psrc[0];          /* 记录卷标 */
                        dstpathname[dstpathlen + 1] = 0;            /* 结束符 */
                    }
                }
                else if (res == FR_OK && !exfuns_path_append(dstpathname, EXFUNS_PATH_CAPACITY, (const char *)fn))
                {
                    res = FR_INVALID_NAME;
                }

                if (res == FR_OK)
                {
                    fcpymsg(fn, 0, 0X04);                           /* 更新文件夹名 */
                    res = sd_f_mkdir((const TCHAR *)dstpathname);      /* 如果文件夹已经存在,就不创建.如果不存在就创建新的文件夹. */
                }

                if (res == FR_EXIST) res = 0;

                while (res == 0)                                /* 开始复制文件夹里面的东东 */
                {
					res = sd_f_readdir(srcdir, finfo);             /* 读取目录下的一个文件 */
                    if (res != FR_OK || finfo->fname[0] == 0)break;     /* 错误了/到末尾了,退出 */

                    if (finfo->fname[0] == '.')continue;        /* 忽略上级目录 */

                    fn = (uint8_t *)finfo->fname;               /* 得到文件名 */
                    dstpathlen = strlen((const char *)dstpathname);     /* 得到当前目标路径的长度 */
                    srcpathlen = strlen((const char *)srcpathname);     /* 得到源路径长度 */

                    if (!exfuns_path_append(srcpathname, EXFUNS_PATH_CAPACITY, "/"))
                    {
                        res = FR_INVALID_NAME;
                    }

                    if (res == FR_OK && (finfo->fattrib & 0X10))  /* 是子目录(文件属性,0X20,归档文件;0X10,子目录;) */
                    {
                        if (!exfuns_path_append(srcpathname, EXFUNS_PATH_CAPACITY, (const char *)fn))
                        {
                            res = FR_INVALID_NAME;
                        }
                        else
                        {
                            res = exfuns_folder_copy(fcpymsg, srcpathname, dstpathname, totsize, cpdsize, fwmode);   /* 拷贝文件夹 */
                        }
                    }
                    else if (res == FR_OK)     /* 非目录 */
                    {
                        if (!exfuns_path_append(dstpathname, EXFUNS_PATH_CAPACITY, "/") ||
                            !exfuns_path_append(dstpathname, EXFUNS_PATH_CAPACITY, (const char *)fn) ||
                            !exfuns_path_append(srcpathname, EXFUNS_PATH_CAPACITY, (const char *)fn))
                        {
                            res = FR_INVALID_NAME;
                        }
                        else
                        {
                            fcpymsg(fn, 0, 0X01);       /* 更新文件名 */
                            res = exfuns_file_copy(fcpymsg, srcpathname, dstpathname, *totsize, *cpdsize, fwmode);  /* 复制文件 */
                            if (res == FR_OK)
                            {
                                *cpdsize += finfo->fsize;   /* 增加一个文件大小 */
                            }
                        }
                    }

                    srcpathname[srcpathlen] = 0;    /* 加入结束符 */
                    dstpathname[dstpathlen] = 0;    /* 加入结束符 */
                }
            }

            free(dstpathname);
            free(srcpathname);
        }
    }

    if (source_dir_open)
    {
        FRESULT close_res = sd_f_closedir(srcdir);
        if (res == FR_OK && close_res != FR_OK)
        {
            res = (uint8_t)close_res;
        }
    }
    free(srcdir);
    free(dstdir);
    free(finfo);
    return res;
}
