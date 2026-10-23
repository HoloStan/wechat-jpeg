/*
 * single_png.c
 *
 * PNG-only decoder based on the original jpeg.c API.
 * - Decoder: LodePNG (PNG -> RGBA)
 * - The exported function names and return-code conventions are kept
 *   compatible with jpeg.c so the caller can use the same WASM interface.
 *
 * Exported API:
 *   int      jpeg_probe(const uint8_t* imagePtr, uint32_t imageSize);
 *   uint32_t jpeg_get_width(void);
 *   uint32_t jpeg_get_height(void);
 *   uint32_t jpeg_get_comps(void); // always 4 (RGBA)
 *   int      jpeg_decode_rgba(const uint8_t* imagePtr, uint32_t imageSize,
 *                             uint8_t* outPtr, uint32_t outBufSize);
 *
 * This file intentionally handles PNG only. JPEG input is rejected.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define PNG_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define PNG_KEEPALIVE
#endif

#include "lodepng.h"

/****************************************************
 *                      ENCODE
 * Keep the original jpeg.c encoder API unchanged.
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

PNG_KEEPALIVE
unsigned char *jpeg_encode_rgba(
    unsigned char *rgba,
    int width,
    int height,
    int quality,
    int *outSize)
{
  if (!rgba || width <= 0 || height <= 0 || !outSize)
    return NULL;

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

  if (buf.size <= 0)
  {
    free(buf.data);
    *outSize = 0;
    return NULL;
  }

  *outSize = buf.size;
  return buf.data;
}

PNG_KEEPALIVE
void jpeg_free_buffer(unsigned char *ptr)
{
  free(ptr);
}

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

/* Decoded metadata; same getter API as jpeg.c. */
static u32 g_width = 0;
static u32 g_height = 0;
static u32 g_comps = 4;

/* PNG signature: 89 50 4E 47 0D 0A 1A 0A */
static int is_png_data(const u8 *data, u32 size)
{
  static const u8 signature[8] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return data && size >= sizeof(signature) &&
         memcmp(data, signature, sizeof(signature)) == 0;
}

PNG_KEEPALIVE
int jpeg_probe(const u8 *jpegPtr, u32 jpegSize)
{
  if (!jpegPtr || jpegSize == 0 || !is_png_data(jpegPtr, jpegSize))
  {
    g_width = g_height = 0;
    g_comps = 4;
    return 1;
  }

  unsigned width = 0;
  unsigned height = 0;
  LodePNGState state;
  lodepng_state_init(&state);
  unsigned error = lodepng_inspect(
      &width, &height, &state, jpegPtr, (size_t)jpegSize);
  lodepng_state_cleanup(&state);

  if (error != 0 || width == 0 || height == 0)
  {
    g_width = g_height = 0;
    g_comps = 4;
    return 1;
  }

  g_width = (u32)width;
  g_height = (u32)height;
  g_comps = 4;
  return 0;
}

PNG_KEEPALIVE
u32 jpeg_get_width(void)
{
  return g_width;
}

PNG_KEEPALIVE
u32 jpeg_get_height(void)
{
  return g_height;
}

PNG_KEEPALIVE
u32 jpeg_get_comps(void)
{
  return g_comps;
}

PNG_KEEPALIVE
int jpeg_decode_rgba(
    const u8 *jpegPtr,
    u32 jpegSize,
    u8 *outPtr,
    u32 outBufSize)
{
  if (!jpegPtr || jpegSize == 0 || !outPtr || outBufSize == 0)
    return 3;

  if (!is_png_data(jpegPtr, jpegSize))
  {
    g_width = g_height = 0;
    g_comps = 4;
    return 1;
  }

  unsigned width = 0;
  unsigned height = 0;
  LodePNGState state;
  lodepng_state_init(&state);
  unsigned error = lodepng_inspect(
      &width, &height, &state, jpegPtr, (size_t)jpegSize);
  lodepng_state_cleanup(&state);

  if (error != 0 || width == 0 || height == 0)
  {
    g_width = g_height = 0;
    g_comps = 4;
    return 1;
  }

  /* Check output size without overflowing the multiplication. */
  u64 needed = (u64)width * (u64)height * 4ULL;
  if (needed > (u64)outBufSize || needed > (u64)SIZE_MAX)
  {
    g_width = g_height = 0;
    g_comps = 4;
    return 2;
  }

  unsigned char *decoded = NULL;
  unsigned decodedWidth = 0;
  unsigned decodedHeight = 0;
  error = lodepng_decode32(
      &decoded, &decodedWidth, &decodedHeight,
      jpegPtr, (size_t)jpegSize);

  if (error != 0 || !decoded)
  {
    free(decoded);
    g_width = g_height = 0;
    g_comps = 4;
    return (int)error;
  }

  /* Use the dimensions returned by the actual decode. */
  u64 decodedSize = (u64)decodedWidth * (u64)decodedHeight * 4ULL;
  if (decodedWidth == 0 || decodedHeight == 0 ||
      decodedSize > (u64)outBufSize || decodedSize > (u64)SIZE_MAX)
  {
    free(decoded);
    g_width = g_height = 0;
    g_comps = 4;
    return 2;
  }

  memcpy(outPtr, decoded, (size_t)decodedSize);
  free(decoded);

  g_width = (u32)decodedWidth;
  g_height = (u32)decodedHeight;
  g_comps = 4;
  return 0;
}
