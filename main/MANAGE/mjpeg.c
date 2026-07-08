/**
 ****************************************************************************************************
 * @file        mjpeg.c
 * @author      ALIENTEK
 * @brief       MJPEG code
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

 #include "mjpeg.h"
 #include "esp_heap_caps.h"
 #include "esp_jpeg_dec.h"


 uint8_t * video_buf;
 struct jpeg_decompress_struct *cinfo;
 struct my_error_mgr *jerr;
 int Windows_Width = 0;
 int Windows_Height = 0;
 uint16_t imgoffx, imgoffy;                  /* The offset of the image in the x and y directions */
 typedef struct my_error_mgr* my_error_ptr;
 static jpeg_dec_handle_t *esp_jpeg_dec = NULL;
 
 /**
  * @brief       Error Exiting
  * @param       cinfo   : JPEG encoding and decoding control structure
  * @retval      None
  */
 METHODDEF(void) my_error_exit(j_common_ptr cinfo)
 {
     my_error_ptr myerr = (my_error_ptr)cinfo->err;
     (*cinfo->err->output_message) (cinfo);
     longjmp(myerr->setjmp_buffer, 1);
 }
 
 /**
  * @brief       Send a message
  * @param       cinfo       : JPEG encoding and decoding control structure
  * @param       msg_level   : Message level
  * @retval      None
  */
 METHODDEF(void) my_emit_message(j_common_ptr cinfo, int msg_level)
 {
     my_error_ptr myerr = (my_error_ptr)cinfo->err;
     if (msg_level < 0)
     {
         printf("emit msg:%d\r\n", msg_level);
         longjmp(myerr->setjmp_buffer, 1);
     }
 }
 
 /**
  * @brief       mjpegdec malloc
  * @param       None
  * @retval      None
  */
 void mjpegdec_malloc(void)
 {
     size_t raw_size = (size_t)Windows_Width * Windows_Height * 2;
     size_t buf_size = (raw_size + 63) & ~((size_t)63);
     video_buf = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
     if (video_buf == NULL)
     {
         video_buf = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
     }
     if (video_buf == NULL)
     {
         video_buf = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_8BIT);
     }
 }
 
 /**
  * @brief       mjpegdec video free
  * @param       None
  * @retval      None
  */
 void mjpegdec_video_free(void)
 {
     if (video_buf != NULL)
     {
         heap_caps_free(video_buf);
         video_buf = NULL;
     }
 }

 static uint8_t mjpegdec_esp_jpeg_open(void)
 {
     if (esp_jpeg_dec != NULL)
     {
         return 0;
     }

     jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
     esp_jpeg_dec = jpeg_dec_open(&config);
     if (esp_jpeg_dec == NULL)
     {
         return 1;
     }

     return 0;
 }

 static void mjpegdec_esp_jpeg_close(void)
 {
     if (esp_jpeg_dec != NULL)
     {
         jpeg_dec_close(esp_jpeg_dec);
         esp_jpeg_dec = NULL;
     }
 }

 static uint8_t mjpegdec_decode_esp_jpeg(uint8_t* buf, uint32_t bsize,lcd_write_cb lcd_cb)
 {
     if (bsize == 0 || video_buf == NULL) return 1;
 
     if (mjpegdec_esp_jpeg_open() != 0) return 2;
 
     jpeg_dec_io_t jpeg_io = {
         .inbuf = buf,
         .inbuf_len = bsize,
         .outbuf = video_buf,
     };
     jpeg_dec_header_info_t out_info = {0};
     jpeg_error_t ret = jpeg_dec_parse_header(esp_jpeg_dec, &jpeg_io, &out_info);
     if (ret != JPEG_ERR_OK)
     {
         mjpegdec_esp_jpeg_close();
         return 3;
     }
 
     if (out_info.width != Windows_Width || out_info.height != Windows_Height)
     {
         mjpegdec_esp_jpeg_close();
         return 4;
     }
 
     int inbuf_consumed = jpeg_io.inbuf_len - jpeg_io.inbuf_remain;
     jpeg_io.inbuf = buf + inbuf_consumed;
     jpeg_io.inbuf_len = jpeg_io.inbuf_remain;
     ret = jpeg_dec_process(esp_jpeg_dec, &jpeg_io);
     if (ret != JPEG_ERR_OK)
     {
         mjpegdec_esp_jpeg_close();
         return 5;
     }
 
     lcd_cb(Windows_Width,Windows_Height,video_buf);
     return 0;
 }
 
 /**
  * @brief       Decoding JPEG images
  * @param       buf    : Jpeg data stream array
  * @param       bsize  : Array size
  * @param       lcd_cb : Drawing function pointers
  * @retval      0:succeed; !0:failed
  */
 uint8_t mjpegdec_decode(uint8_t* buf, uint32_t bsize,lcd_write_cb lcd_cb)
 {
     JSAMPARRAY buffer;
     if (bsize == 0) return 1;
     if (mjpegdec_decode_esp_jpeg(buf, bsize, lcd_cb) == 0) return 0;
     int row_stride = 0;
     int j = 0;
     int lineR = 0;
     
     cinfo->err = jpeg_std_error(&jerr->pub);
     jerr->pub.error_exit = my_error_exit;
     jerr->pub.emit_message = my_emit_message;
     cinfo->out_color_space = JCS_RGB;
 
     if (setjmp(jerr->setjmp_buffer))
     {
         jpeg_abort_decompress(cinfo);
         jpeg_destroy_decompress(cinfo);
         return 2;
     }
 
     jpeg_create_decompress(cinfo);
 
     jpeg_mem_src(cinfo, buf, bsize);
     jpeg_read_header(cinfo, TRUE);
 
     jpeg_start_decompress(cinfo);

     row_stride = cinfo->output_width * cinfo->output_components;
 
     buffer = (*cinfo->mem->alloc_sarray)
         ((j_common_ptr)cinfo, JPOOL_IMAGE, row_stride, 1);
     
     while (cinfo->output_scanline < cinfo->output_height)
     {
         int i = 0;
 
         jpeg_read_scanlines(cinfo, buffer, 1);
         unsigned short tmp_color565;
 
         for (int k = 0; k < Windows_Width * 2; k += 2)
         {
             tmp_color565 = rgb565(buffer[0][i],buffer[0][i + 1],buffer[0][i + 2]);
             video_buf[lineR + k] = tmp_color565 & 0x00FF;
             video_buf[lineR + k + 1] =  (tmp_color565 & 0xFF00) >> 8;
 
             i += 3;
         }
         
         j++;
         lineR = j * Windows_Width * 2;
     }
     lcd_cb(Windows_Width,Windows_Height,video_buf);
     
     jpeg_finish_decompress(cinfo);
     jpeg_destroy_decompress(cinfo);
     
     return 0;
 }
 
 /**
  * @brief       mjpegdec init
  * @param       offx,offy:deviation
  * @retval      0:succeed; !0:failed
  */
 char mjpegdec_init(uint16_t offx, uint16_t offy)
 {
     Windows_Width = 0;
     Windows_Height = 0;
     cinfo = (struct jpeg_decompress_struct *)malloc(sizeof(struct jpeg_decompress_struct));
     jerr = (struct my_error_mgr *)malloc(sizeof(struct my_error_mgr));
 
     if (cinfo == NULL || jerr == NULL)
     {
         printf("[E][mjpeg.cpp] mjpegdec_init(): malloc failed to apply for memory\r\n");
         mjpegdec_free();
         return -1;
     }
 
     imgoffx = offx;
     imgoffy = offy;
 
     return 0;
 }
 
 /**
  * @brief       Mjpeg decoding completed, freeing memory
  * @param       None
  * @retval      None
  */
 void mjpegdec_free(void)
 {
     mjpegdec_esp_jpeg_close();
     free(cinfo);
     free(jerr);
     cinfo = NULL;
     jerr = NULL;
 }
 
