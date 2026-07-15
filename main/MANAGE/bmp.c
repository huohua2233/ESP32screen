/**
 ******************************************************************************
 * @file        bmp.c
 * @version     V1.0
 * @brief       图片解码-bmp解码 代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

 #include "bmp.h"
 #include <stdlib.h>

 static uint16_t bmp_read_u16(const uint8_t *data)
 {
     return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
 }

 static uint32_t bmp_read_u32(const uint8_t *data)
 {
     return (uint32_t)data[0] |
            ((uint32_t)data[1] << 8) |
            ((uint32_t)data[2] << 16) |
            ((uint32_t)data[3] << 24);
 }

 static FRESULT bmp_read_exact(FIL *fp, void *buffer, UINT size)
 {
     UINT bytes_read = 0;
     FRESULT result = sd_f_read(fp, buffer, size, &bytes_read);
     return result == FR_OK && bytes_read == size ? FR_OK : FR_INT_ERR;
 }

 static FRESULT bmp_seek(FIL *fp, FSIZE_t offset)
 {
     FRESULT result = sd_f_lseek(fp, offset);
     return result;
 }


 /**
  * @brief       BMP图片解码
  * @param       filename      : 包含路径的文件名(.bmp/.jpg/.jpeg/.gif/.png等)
  * @param       width, height : 显示区域
  * @retval      返回BMP解码速度
  */
 TickType_t bmp_decode(const char *filename, int width, int height,lcd_write_cb lcd_cb)
 {
     TickType_t start_tick = xTaskGetTickCount();
     FIL *fp = NULL;
     uint8_t *colors = NULL;
     uint8_t *row_buffer = NULL;
     uint8_t header[54];
     bool file_open = false;

     if (filename == NULL || lcd_cb == NULL || width <= 0 || height <= 0)
     {
         return 0;
     }

     fp = (FIL *)calloc(1, sizeof(FIL));
     if (fp == NULL)
     {
         ESP_LOGW(__FUNCTION__, "No memory for file [%s]", filename);
         return 0;
     }

     FRESULT result = sd_f_open(fp, (const TCHAR *)filename, FA_READ);
     if (result != FR_OK)
     {
         ESP_LOGW(__FUNCTION__, "File not found [%s]", filename);
         goto fail;
     }
     file_open = true;

     if (bmp_read_exact(fp, header, sizeof(header)) != FR_OK || header[0] != 'B' || header[1] != 'M')
     {
         ESP_LOGW(__FUNCTION__, "File is not BMP");
         goto fail;
     }

     uint32_t declared_size = bmp_read_u32(header + 2);
     uint32_t pixel_offset = bmp_read_u32(header + 10);
     uint32_t dib_size = bmp_read_u32(header + 14);
     int32_t source_width_signed = (int32_t)bmp_read_u32(header + 18);
     int32_t source_height_signed = (int32_t)bmp_read_u32(header + 22);
     uint16_t planes = bmp_read_u16(header + 26);
     uint16_t depth = bmp_read_u16(header + 28);
     uint32_t compression = bmp_read_u32(header + 30);
     FSIZE_t file_size = f_size(fp);

     if (declared_size < sizeof(header) || declared_size > file_size || dib_size < 40 ||
         source_width_signed <= 0 || source_height_signed == 0 || source_height_signed == INT32_MIN ||
         planes != 1 || depth != 24 || compression != 0 ||
         (uint64_t)pixel_offset < 14ull + dib_size || pixel_offset > file_size)
     {
         goto fail;
     }

     uint32_t source_width = (uint32_t)source_width_signed;
     uint32_t source_height = source_height_signed < 0 ? (uint32_t)(-(int64_t)source_height_signed) : (uint32_t)source_height_signed;
     uint64_t row_size = ((uint64_t)source_width * 3u + 3u) & ~3ull;
     uint64_t pixel_end = (uint64_t)pixel_offset + row_size * source_height;

     if (row_size > UINT32_MAX || pixel_end > file_size || pixel_end > declared_size)
     {
         goto fail;
     }

     uint32_t output_width = source_width < (uint32_t)width ? source_width : (uint32_t)width;
     uint32_t output_height = source_height < (uint32_t)height ? source_height : (uint32_t)height;
     uint32_t first_column = (source_width - output_width) / 2;
     uint32_t first_row = (source_height - output_height) / 2;

     if (output_width == 0 || output_height == 0 ||
         output_width > SIZE_MAX / output_height ||
         (size_t)output_width * output_height > SIZE_MAX / 2 ||
         output_width > SIZE_MAX / 3 || (size_t)output_width * 3 > UINT32_MAX)
     {
         goto fail;
     }

     size_t output_size = (size_t)output_width * output_height * 2;
     size_t row_bytes = (size_t)output_width * 3;
     colors = (uint8_t *)malloc(output_size);
     row_buffer = (uint8_t *)malloc(row_bytes);
     if (colors == NULL || row_buffer == NULL)
     {
         goto fail;
     }

     for (uint32_t output_row = 0; output_row < output_height; output_row++)
     {
         uint32_t image_row = first_row + output_row;
         uint32_t file_row = source_height_signed < 0 ? image_row : source_height - 1 - image_row;
         uint64_t row_offset = (uint64_t)pixel_offset + (uint64_t)file_row * row_size + (uint64_t)first_column * 3u;

         if (row_offset > file_size || row_bytes > file_size - row_offset ||
             bmp_seek(fp, (FSIZE_t)row_offset) != FR_OK ||
             bmp_read_exact(fp, row_buffer, (UINT)row_bytes) != FR_OK)
         {
             goto fail;
         }

         for (uint32_t output_column = 0; output_column < output_width; output_column++)
         {
             size_t source_index = (size_t)output_column * 3;
             size_t output_index = ((size_t)output_row * output_width + output_column) * 2;
             uint16_t color = rgb565(row_buffer[source_index + 2], row_buffer[source_index + 1], row_buffer[source_index]);
             colors[output_index] = (uint8_t)(color & 0xFF);
             colors[output_index + 1] = (uint8_t)(color >> 8);
         }
     }

     free(row_buffer);
     row_buffer = NULL;
     sd_f_close(fp);
     file_open = false;
     free(fp);
     fp = NULL;

     lcd_cb(output_width, output_height, colors);
     return xTaskGetTickCount() - start_tick;

fail:
     free(row_buffer);
     free(colors);
     if (file_open)
     {
         sd_f_close(fp);
     }
     free(fp);
     return 0;
 }
 
