/**
 ****************************************************************************************************
 * @file        jpeg.c
 * @author      ALIENTEK
 * @brief       JPEG code
 * @license     Copyright (C) 2020-2030, ALIENTEK
 ****************************************************************************************************
 * @attention
 *
 * platform     : ALIENTEK DNESP32S3 board
 * website      : www.alientek.com
 * forum        : www.openedv.com/forum.php
 *
 * change logs  :
 * version      data         notes
 * V1.0         20240430     the first version
 *
 ****************************************************************************************************
 */

 #include "jpeg.h"
 #include <stdlib.h>


 /* The TJPGD outside the ROM code is newer and has different return type in decode callback */
 typedef int jpeg_decode_out_t;
 JDEC jpeg_dev;                  /* Decompressor object structure */
 
 /**
  * @brief       Input function for jpeg decoder. Just returns bytes from the inData field of the JpegDev structure
  * @param       jd       : Decompressor object structure
  * @param       buf      : Pointer to the source buffer
  * @param       num      : Lenght
  * @retval      Number of bytes read
  */
 static unsigned int infunc(JDEC *decoder, uint8_t *buf, unsigned int len)
 {
     UINT rb = 0;                            /* Read nd bytes from the input strem */
     FIL *dev = (FIL *)decoder->device;
 
     if (buf)
     {
         FRESULT res = sd_f_read(dev, buf, len, &rb);
         return (res == FR_OK) ? rb : 0;
     }
     else                                    /* Skip nd bytes on the input stream */
     {
         FRESULT res = sd_f_lseek(dev, f_tell(dev) + len);
         return (res == FR_OK) ? len : 0;
     }
 }
 
 /**
  * @brief       Output function. Re-encodes the RGB888 data from the decoder as big-endian RGB565 and stores it in the outData array of the JpegDev structure
  * @param       decoder : Decompressor object structure
  * @param       bitmap  : bitmap data 
  * @param       rect    : Rectangular structure
  * @retval      1:success
  */
 static jpeg_decode_out_t outfunc(JDEC *decoder, void *bitmap, JRECT *rect)
 {
     JDEC *jd = (JDEC *) decoder;
     uint8_t *in = (uint8_t *) bitmap;
 
     for (int y = rect->top; y <= rect->bottom; y++)
     {
         for (int x = rect->left; x <= rect->right; x++)
         {
             if (y < jd->screenHeight && x < jd->screenWidth)
             {
                 jd->outData[y][x] = rgb565(in[0], in[1], in[2]);
             }
 
             in += 3;
         }
     }
 
     return 1;
 }
 
 /**
  * @brief       Specifies scaling factor N for output. The output image is descaled to 1 / 2 ^ N (N = 0 to 3)
  * @param       screenWidth     : Display width
  * @param       screenHeight    : Display height
  * @param       decodeWidth     : Decoding width
  * @param       decodeHeight    : Decoding height
  * @retval      0:Failed;1:2x ratio;2:4 times the proportion;3:Original size
  */
 uint8_t getScale(int screenWidth, int screenHeight, uint16_t decodeWidth, uint16_t decodeHeight)
 {
     if (screenWidth >= decodeWidth && screenHeight >= decodeHeight)
     {
         return 0;
     }
 
     double scaleWidth = (double)decodeWidth / (double)screenWidth;
     double scaleHeight = (double)decodeHeight / (double)screenHeight;
     double scale = scaleWidth;
 
     if (scaleWidth < scaleHeight)
     {
         scale = scaleHeight;
     }
 
     if (scale <= 2.0)
     {
         return 1;
     }
 
     if (scale <= 4.0)
     {
         return 2;
     }
 
     return 3;
 }
 
 /**
  * @brief       Decode the jpeg ``image.jpg`` embedded into the program file into pixel data
  * @param       pixels          : rgb565 format
  * @param       file            : File name
  * @param       screenWidth     : Display width
  * @param       screenHeight    : Display height
  * @param       imageWidth      : image width
  * @param       imageHeight     : image height
  * @retval      0:Failed;1:2x ratio;2:4 times the proportion;3:Original size
  */
 esp_err_t decode_jpeg(pixel_jpeg ***pixels, char * file, int screenWidth, int screenHeight, int * imageWidth, int * imageHeight)
 {
     char *work = NULL;
     FIL *f_jpeg = NULL;
     FRESULT open_res = FR_OK;
     pixel_jpeg *pixel_data = NULL;
     *pixels = NULL;
     JRESULT res = JDR_OK;
     esp_err_t ret = ESP_OK;
     uint32_t jd_work_size = 6144 + 4096;
 
     /* Allocate the work space for the jpeg decoder */
     work = malloc(jd_work_size);
 
     if (work == NULL)
     {
         ESP_LOGE(__FUNCTION__, "Cannot allocate workspace");
         ret = ESP_ERR_NO_MEM;
         goto err;
     }
     
     f_jpeg = (FIL *)calloc(1, sizeof(FIL));
     if (f_jpeg == NULL)
     {
         ESP_LOGE(__FUNCTION__, "Cannot allocate file handle");
         ret = ESP_ERR_NO_MEM;
         goto err;
     }
 
     open_res = sd_f_open(f_jpeg, (const TCHAR *)file, FA_READ);
     if (open_res != FR_OK)
     {
         ESP_LOGW(__FUNCTION__, "Image file not found [%s]", file);
         ret = ESP_ERR_NOT_FOUND;
         free(f_jpeg);
         f_jpeg = NULL;
         goto err;
     }
 
     /* Prepare and decode the jpeg */
     res = jd_prepare(&jpeg_dev, (void *)infunc, work, jd_work_size, f_jpeg);
 
     if (res != JDR_OK)
     {
         ESP_LOGE(__FUNCTION__, "Image decoder: jd_prepare failed (%d)", res);
         ret = ESP_ERR_NOT_SUPPORTED;
         goto err;
     }
 
     /* Calculate Scaling factor */
     uint8_t scale = getScale(screenWidth, screenHeight, jpeg_dev.width, jpeg_dev.height);
 
     /* Calculate image size */
     double factor = 1.0;
     if (scale == 1) factor = 0.5;
     if (scale == 2) factor = 0.25;
     if (scale == 3) factor = 0.125;
     *imageWidth = (double)jpeg_dev.width * factor;
     *imageHeight = (double)jpeg_dev.height * factor;

     if (*imageWidth <= 0 || *imageHeight <= 0)
     {
         ret = ESP_ERR_INVALID_SIZE;
         goto err;
     }

     *pixels = malloc(sizeof(pixel_jpeg *) * (*imageHeight));

     if (*pixels == NULL)
     {
         ESP_LOGE(__FUNCTION__, "Error allocating memory for lines");
         ret = ESP_ERR_NO_MEM;
         goto err;
     }

     pixel_data = malloc((size_t)(*imageWidth) * (*imageHeight) * sizeof(pixel_jpeg));

     if (pixel_data == NULL)
     {
         ESP_LOGE(__FUNCTION__, "Error allocating memory for image");
         ret = ESP_ERR_NO_MEM;
         goto err;
     }

     for (int i = 0; i < *imageHeight; i++)
     {
         (*pixels)[i] = pixel_data + i * (*imageWidth);
     }

     jpeg_dev.outData = *pixels;
     jpeg_dev.screenWidth = *imageWidth;
     jpeg_dev.screenHeight = *imageHeight;

     /* Start to decompress the JPEG picture */
     res = jd_decomp(&jpeg_dev, (void *)outfunc, scale);
 
     if (res != JDR_OK)
     {
         ESP_LOGE(__FUNCTION__, "Image decoder: jd_decode failed (%d)", res);
         ret = ESP_ERR_NOT_SUPPORTED;
         goto err;
     }
 
     /* All done! Free the work area (as we don't need it anymore) and return victoriously */
     free(work);
     sd_f_close(f_jpeg);
     free(f_jpeg);
     return ret;
 
     /* Something went wrong! Exit cleanly, de-allocating everything we allocated */
     err:
     if (f_jpeg != NULL)
     {
         sd_f_close(f_jpeg);
         free(f_jpeg);
     }
 
     if (*pixels != NULL)
     {
         free(pixel_data);
         free(*pixels);
     }
 
	 
     free(work);
     return ret;
 }
 
 /**
  * @brief       Release image memory
  * @param       pixels          : rgb565 format
  * @param       screenWidth     : Display width
  * @param       screenHeight    : Display height
  * @retval      ESP_OK:success;Other:fail
  */
 esp_err_t release_image(pixel_jpeg ***pixels, int screenWidth, int screenHeight)
 {
     if (*pixels != NULL)
     {
         if ((*pixels)[0] != NULL)
         {
             free((*pixels)[0]);
         }
         free(*pixels);
         *pixels = NULL;
     }
 
     return ESP_OK;
 }
 
 /**
  * @brief       JPEG image decoding
  * @param       filename      : Image file path(.bmp/.jpg/.jpeg/.gif/.png etc)
  * @param       width         : Display width
  * @param       height        : Display height
  * @retval      Return JPEG decoding speed
  */
 TickType_t jpeg_decode(const char *filename, int width, int height,lcd_write_cb lcd_cb)
 {
     TickType_t startTick, endTick, diffTick;
     startTick = xTaskGetTickCount();
 
     pixel_jpeg **pixels;
     int imageWidth;
     int imageHeight;
     esp_err_t err = decode_jpeg(&pixels, (char *)filename, width, height, &imageWidth, &imageHeight);
     if (err == ESP_OK)
     {
 
         uint8_t *colors = (uint8_t *)pixels[0];
         uint16_t _width = imageWidth;
         uint16_t _height = imageHeight;

         free(pixels);
         lcd_cb(_width,_height,colors);
     }
     else
     {
         ESP_LOGE(__FUNCTION__, "decode_jpeg fail=%d", err);
     }
 
     endTick = xTaskGetTickCount();
     diffTick = endTick - startTick;
     return diffTick;
 }
 
