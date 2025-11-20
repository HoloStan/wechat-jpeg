/*
 * picojpeg_wasm_rgba.c
 *
 * WASM-friendly wrapper around PicoJPEG, modified to output 4 components (RGBA).
 *
 * Exports:
 *   int wasm_probe_jpeg(const uint8_t* jpegPtr, uint32_t jpegSize);
 *   uint32_t wasm_get_width(void);
 *   uint32_t wasm_get_height(void);
 *   uint32_t wasm_get_comps(void);        // now = 4
 *   int wasm_decode_jpeg(const uint8_t* jpegPtr, uint32_t jpegSize,
 *                        uint8_t* outPtr, uint32_t outBufSize);
 *
 * Return codes:
 *   0 = success
 *   1 = init failed
 *   2 = output buffer too small
 *   3 = decode error
 */

#include "picojpeg.h"
#include <stdint.h>

/* basic aliases */
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

/* input buffer (global for callback) */
static const u8 *g_srcBuf = 0;
static u32 g_srcSize = 0;
static u32 g_srcOfs = 0;

/* decoded image info */
static u32 g_width = 0;
static u32 g_height = 0;
static u32 g_comps = 4; /* output RGBA */

/* PicoJPEG input callback */
static u8 pjpeg_need_bytes_callback(u8 *pBuf, u8 buf_size,
                                    u8 *pBytes_actually_read,
                                    void *pCallback_data)
{
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

/* Probe JPEG header */
int wasm_probe_jpeg(const u8 *jpegPtr, u32 jpegSize)
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

  g_width = (u32)info.m_MCUSPerRow * (u32)info.m_MCUWidth;
  g_height = (u32)info.m_MCUSPerCol * (u32)info.m_MCUHeight;
  g_comps = 4;

  g_srcOfs = 0;
  return 0;
}

u32 wasm_get_width(void) { return g_width; }
u32 wasm_get_height(void) { return g_height; }
u32 wasm_get_comps(void) { return g_comps; }

/* Decode JPEG → RGBA */
int wasm_decode_jpeg(const u8 *jpegPtr, u32 jpegSize, u8 *outPtr, u32 outBufSize)
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

  u32 width = (u32)info.m_MCUSPerRow * (u32)info.m_MCUWidth;
  u32 height = (u32)info.m_MCUSPerCol * (u32)info.m_MCUHeight;

  /* RGBA 4 bytes */
  u64 needed = (u64)width * (u64)height * 4ULL;
  if (needed > (u64)outBufSize)
  {
    g_width = g_height = 0;
    return 2; /* output too small */
  }

  u32 row_pitch = width * 4U; /* 4 bytes each pixel */

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
      u32 y = mcu_y * (u32)mcuH + (u32)by;
      if (y >= height)
        continue;

      for (u8 bx = 0; bx < mcuW; ++bx)
      {
        u32 x = mcu_x * (u32)mcuW + (u32)bx;
        if (x >= width)
          continue;

        u32 idx = (u32)by * (u32)mcuW + (u32)bx;

        u8 r = info.m_pMCUBufR[idx];
        u8 g = info.m_pMCUBufG[idx];
        u8 b = info.m_pMCUBufB[idx];

        u8 *dst = outPtr + (y * row_pitch) + (x * 4U);
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = 255; /* alpha 固定 255 */
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
