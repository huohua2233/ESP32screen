/**
 ****************************************************************************************************
 * @file        png.c
 * @author      ALIENTEK
 * @brief       PNG code
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

 #include "png.h"
 #include <stdlib.h>


 /**
  * @brief       Initialize PNG
  * @param       pngle   : PNG Handle
  * @param       w       : Width
  * @param       h       : Height
  * @retval      None
  */
 void png_init(pngle_t *pngle, uint32_t w, uint32_t h)
 {
     pngle->imageWidth = w;
     pngle->imageHeight = h;
     pngle->reduction = false;
     pngle->scale_factor = 1.0;
 
     /* Calculate Reduction */
     if (pngle->screenWidth < pngle->imageWidth || pngle->screenHeight < pngle->imageHeight)
     {
         pngle->reduction = true;
         double factorWidth = (double)pngle->screenWidth / (double)pngle->imageWidth;
         double factorHeight = (double)pngle->screenHeight / (double)pngle->imageHeight;
         pngle->scale_factor = factorWidth;
         if (factorHeight < factorWidth) pngle->scale_factor = factorHeight;
         pngle->imageWidth = pngle->imageWidth * pngle->scale_factor;
         pngle->imageHeight = pngle->imageHeight * pngle->scale_factor;
     }

     if (pngle->imageWidth == 0 || pngle->imageHeight == 0)
     {
         return;
     }

     pngle->pixels = calloc(pngle->imageHeight, sizeof(pixel_png *));

     if (pngle->pixels == NULL)
     {
         return;
     }

     pngle->pixel_buf = calloc((size_t)pngle->imageWidth * pngle->imageHeight, sizeof(pixel_png));

     if (pngle->pixel_buf == NULL)
     {
         free(pngle->pixels);
         pngle->pixels = NULL;
         return;
     }

     for (uint32_t i = 0; i < pngle->imageHeight; i++)
     {
         pngle->pixels[i] = pngle->pixel_buf + i * pngle->imageWidth;
     }
 }
 
 /**
  * @brief       Store PNG decoded data in a specified storage area
  * @param       pngle   : PNG Handle
  * @param       x       : The x-coordinate of the string to be displayed
  * @param       y       : The y-coordinate of the string to be displayed
  * @param       w       : Width
  * @param       h       : Height
  * @param       rgb     : RGB color value
  * @retval      None
  */
 void png_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4])
 {
     uint32_t _x = x;
     uint32_t _y = y;
 
     if (pngle->reduction)
     {
         _x = x * pngle->scale_factor;
         _y = y * pngle->scale_factor;
     }
 
     if (pngle->pixels != NULL && _y < pngle->imageHeight && _x < pngle->imageWidth)
     {
         pngle->pixels[_y][_x] = rgb565(rgba[0], rgba[1], rgba[2]);
     }
 }
 
 /**
  * @brief       PNG decoding completion callback function
  * @param       pngle   : PNG Handle
  * @retval      None
  */
 void png_finish(pngle_t *pngle)
 {
 }
 
 /**
  * @brief       png decode
  * @param       filename      : Image file path(.bmp/.jpg/.jpeg/.gif/.png etc)
  * @param       width         : Display width
  * @param       height        : Display height
  * @param       lcd_cb        : Drawing function pointers
  * @retval      Return PNG decoding speed
  */
 TickType_t png_decode(const char *filename, int width, int height,lcd_write_cb lcd_cb)
 {
     TickType_t startTick, endTick, diffTick;
     startTick = xTaskGetTickCount();
     const TickType_t timeoutTick = pdMS_TO_TICKS(30000);
     uint16_t _width = width;
     static char buf[8192];
     size_t remain = 0;
     uint16_t _height = height;
     double display_gamma = 2.2;
     
     /* open PNG file */
     FIL* fp = NULL;
     pngle_t *pngle = NULL;
     uint8_t *colors = NULL;
     FRESULT res;
     UINT len;
     uint16_t yield_count = 0;
     fp = (FIL *)calloc(1, sizeof(FIL));
     if (fp == NULL)
     {
         return 0;
     }
     res = sd_f_open(fp, (const TCHAR *)filename, FA_READ);

     if (res != FR_OK)
     {
         free(fp);
         return 0;
     }
 
     pngle = pngle_new(width, height);
     if (pngle == NULL)
     {
         sd_f_close(fp);
         free(fp);
         return 0;
     }
     pngle_set_init_callback(pngle, png_init);
     pngle_set_draw_callback(pngle, png_draw);
     pngle_set_done_callback(pngle, png_finish);
     pngle_set_display_gamma(pngle, display_gamma);
 
     while (!f_eof(fp))
     {
         if ((xTaskGetTickCount() - startTick) > timeoutTick)
         {
             goto fail;
         }

         if (remain >= sizeof(buf))
         {
             goto fail;
         }
 
         res = sd_f_read(fp,buf + remain,sizeof(buf) - remain, &len);

         if (res != FR_OK || (len == 0 && !f_eof(fp)))
         {
             goto fail;
         }
 
         int fed = pngle_feed(pngle, buf, remain + len);
 
         if (fed < 0)
         {
             goto fail;
         }
 
         remain = remain + len - fed;
 
         if (remain > 0)
         {
             memmove(buf, buf + fed, remain);
         }

         if (++yield_count >= 16)
         {
             yield_count = 0;
             vTaskDelay(1);
         }
     }

     if (pngle->state != PNGLE_STATE_EOF || remain != 0)
     {
         goto fail;
     }

     sd_f_close(fp);
 
     _width = pngle->imageWidth;
     _height = pngle->imageHeight;

     if (pngle->pixels == NULL || pngle->pixel_buf == NULL || _width == 0 || _height == 0)
     {
         pngle_destroy(pngle, width, height);
         free(fp);
         return 0;
     }

     colors = (uint8_t *)pngle->pixel_buf;
     pngle->pixel_buf = NULL;
     free(pngle->pixels);
     pngle->pixels = NULL;
 
     lcd_cb(_width,_height,colors);
 
     free(fp);
     pngle_destroy(pngle, width, height);
 
     endTick = xTaskGetTickCount();
     diffTick = endTick - startTick;
     return diffTick;

fail:
     if (colors != NULL)
     {
         free(colors);
     }

     if (pngle != NULL)
     {
         pngle_destroy(pngle, width, height);
     }

     if (fp != NULL)
     {
         sd_f_close(fp);
         free(fp);
     }

     return 0;
 }
 
