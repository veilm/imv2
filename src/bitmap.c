#include "bitmap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

size_t imv_bitmap_size(struct imv_bitmap bmp)
{
  assert((size_t)bmp.width <= SIZE_MAX / bmp.height / BYTES_PER_CHANNEL);
  return (size_t)bmp.width * bmp.height * BYTES_PER_CHANNEL;
}

struct imv_bitmap imv_bitmap_alloc(int32_t width, int32_t height)
{
  assert(width > 0 && height > 0);
  struct imv_bitmap result = {
      .width = width,
      .height = height,
      .data = NULL,
  };

  if ((size_t)width <= SIZE_MAX / height / BYTES_PER_CHANNEL) {
    result.data = malloc(imv_bitmap_size(result));
  }

  return result;
}

void imv_bitmap_free(struct imv_bitmap bmp) { free(bmp.data); }
