#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x)
#include "stb_image_write.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// ---- 关键：我们用自定义的回调把 JPEG 写到内存 ----

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
    unsigned char *newData = (unsigned char *)realloc(buf->data, newCap);
    if (!newData)
      return;
    buf->data = newData;
    buf->capacity = newCap;
  }

  memcpy(buf->data + buf->size, data, len);
  buf->size += len;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
unsigned char *encode_jpeg_rgba(
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

  // 使用 stb_image_write 提供的自定义回调
  stbi_write_jpg_to_func(
      jpeg_write_callback,
      &buf,
      width,
      height,
      4, // RGBA
      rgba,
      quality);

  *outSize = buf.size;
  return buf.data; // JS 负责 free()
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void free_buffer(unsigned char *ptr)
{
  free(ptr);
}
