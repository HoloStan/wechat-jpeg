/*
 * single_jpeg.c
 *
 * Unified JPEG Encode + Decode (RGBA)
 * - Encoder: stb_image_write (RGBA → JPEG)
 * - Decoder: PicoJPEG (JPEG → RGBA)
 *
 * Exported API (统一命名):
 *
 *   // Decode JPEG → RGBA
 *   int      jpeg_probe(const uint8_t* jpegPtr, uint32_t jpegSize);
 *   uint32_t jpeg_get_width(void);
 *   uint32_t jpeg_get_height(void);
 *   uint32_t jpeg_get_comps(void);      // always 4 (RGBA)
 *   int      jpeg_decode_rgba(const uint8_t* jpegPtr, uint32_t jpegSize,
 *                             uint8_t* outPtr, uint32_t outBufSize);
 *
 *   // Encode RGBA → JPEG
 *   unsigned char* jpeg_encode_rgba(unsigned char* rgba,
 *                                   int width, int height,
 *                                   int quality, int* outSize);
 *   void     jpeg_free_buffer(unsigned char* ptr);
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

/****************************************************
 *                      ENCODE
 ****************************************************/

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x)
#include "stb_image_write.h"

typedef struct
{
  unsigned char *data;
  int size;
  int capacity;
} JpegBuffer;

/* stb callback: writes into growing memory buffer */
static void jpeg_write_callback(void *context, void *data, int len)
{
  JpegBuffer *buf = (JpegBuffer *)context;

  if (buf->size + len > buf->capacity)
  {
    int newCap = buf->capacity * 2 + len;
    unsigned char *newBuf = (unsigned char *)realloc(buf->data, newCap);
    if (!newBuf)
      return;

    buf->data = newBuf;
    buf->capacity = newCap;
  }

  memcpy(buf->data + buf->size, data, len);
  buf->size += len;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
unsigned char *jpeg_encode_rgba(
    unsigned char *rgba,
    int width,
    int height,
    int quality,
    int *outSize)
{
  JpegBuffer buf;
  buf.data = (unsigned char *)malloc(1024);
  buf.size = 0;
  buf.capacity = 1024;

  if (!buf.data)
    return NULL;

  stbi_write_jpg_to_func(
      jpeg_write_callback,
      &buf,
      width,
      height,
      4, /* RGBA */
      rgba,
      quality);

  *outSize = buf.size;
  return buf.data;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void jpeg_free_buffer(unsigned char *ptr)
{
  free(ptr);
}

/****************************************************
 *                      DECODE
 ****************************************************/

#include "picojpeg.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

/* global input state for PicoJPEG callback */
static const u8 *g_srcBuf = 0;
static u32 g_srcSize = 0;
static u32 g_srcOfs = 0;

/* decoded meta */
static u32 g_width = 0;
static u32 g_height = 0;
static u32 g_comps = 4; /* always RGBA */

/* PicoJPEG input callback */
static u8 pjpeg_need_bytes_callback(u8 *pBuf, u8 buf_size,
                                    u8 *pBytes_actually_read,
                                    void *pCallback_data)
{
  (void)pCallback_data;

  u32 remaining = (g_srcOfs < g_srcSize) ? (g_srcSize - g_srcOfs) : 0;
  u32 toRead = (remaining < buf_size) ? remaining : buf_size;

  if (toRead && pBuf)
  {
    const u8 *src = g_srcBuf + g_srcOfs;
    for (u32 i = 0; i < toRead; ++i)
      pBuf[i] = src[i];
  }

  g_srcOfs += toRead;
  *pBytes_actually_read = (u8)toRead;
  return 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int jpeg_probe(const u8 *jpegPtr, u32 jpegSize)
{
  if (!jpegPtr || jpegSize == 0)
    return 1;

  g_srcBuf = jpegPtr;
  g_srcSize = jpegSize;
  g_srcOfs = 0;

  pjpeg_image_info_t info;
  u8 status = pjpeg_decode_init(&info, pjpeg_need_bytes_callback, 0, 0);
  if (status)
  {
    g_width = g_height = 0;
    return 1;
  }

  g_width = info.m_MCUSPerRow * info.m_MCUWidth;
  g_height = info.m_MCUSPerCol * info.m_MCUHeight;
  g_comps = 4;

  g_srcOfs = 0;
  return 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
u32 jpeg_get_width(void) { return g_width; }

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
u32 jpeg_get_height(void) { return g_height; }

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
u32 jpeg_get_comps(void) { return g_comps; }

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int jpeg_decode_rgba(const u8 *jpegPtr, u32 jpegSize, u8 *outPtr, u32 outBufSize)
{
  if (!jpegPtr || jpegSize == 0 || !outPtr || outBufSize == 0)
    return 3;

  g_srcBuf = jpegPtr;
  g_srcSize = jpegSize;
  g_srcOfs = 0;

  pjpeg_image_info_t info;
  u8 status = pjpeg_decode_init(&info, pjpeg_need_bytes_callback, 0, 0);
  if (status)
  {
    g_width = g_height = 0;
    return 1;
  }

  u32 width = info.m_MCUSPerRow * info.m_MCUWidth;
  u32 height = info.m_MCUSPerCol * info.m_MCUHeight;

  u64 needed = (u64)width * (u64)height * 4ULL;
  if (needed > outBufSize)
  {
    g_width = g_height = 0;
    return 2;
  }

  u32 row_pitch = width * 4U;

  u32 mcu_x = 0, mcu_y = 0;

  for (;;)
  {
    status = pjpeg_decode_mcu();
    if (status)
    {
      if (status == PJPG_NO_MORE_BLOCKS)
        break;

      g_width = g_height = 0;
      return 3;
    }

    u8 mcuW = info.m_MCUWidth;
    u8 mcuH = info.m_MCUHeight;

    for (u8 by = 0; by < mcuH; ++by)
    {
      u32 y = mcu_y * mcuH + by;
      if (y >= height)
        continue;

      for (u8 bx = 0; bx < mcuW; ++bx)
      {
        u32 x = mcu_x * mcuW + bx;
        if (x >= width)
          continue;

        u32 idx = (u32)by * mcuW + (u32)bx;

        u8 r = info.m_pMCUBufR[idx];
        u8 g = info.m_pMCUBufG[idx];
        u8 b = info.m_pMCUBufB[idx];

        u8 *dst = outPtr + (y * row_pitch) + (x * 4U);
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = 255;
      }
    }

    mcu_x++;
    if (mcu_x == info.m_MCUSPerRow)
    {
      mcu_x = 0;
      mcu_y++;
    }
  }

  g_width = width;
  g_height = height;
  g_comps = 4;
  return 0;
}
