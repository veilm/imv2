#ifndef IMV_BITMAP_H
#define IMV_BITMAP_H

#include <stdint.h>
#include <stdlib.h>

#define BYTES_PER_CHANNEL 4

struct imv_bitmap {
  int32_t width;
  int32_t height;
  uint8_t *data;
};

/* Gets the size of the `data` buffer in bytes. */
size_t imv_bitmap_size(struct imv_bitmap bmp);

/* Allocate a new bitmap, `data` will be NULL if the allocation failed. */
struct imv_bitmap imv_bitmap_alloc(int32_t width, int32_t height);

/* Clean up a bitmap */
void imv_bitmap_free(struct imv_bitmap bmp);

#endif
