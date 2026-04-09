#include "backend.h"
#include "bitmap.h"
#include "image.h"
#include "source_private.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct private {
  uint32_t width, height;
  uint8_t *bitmap;
};

static void free_private(void *raw_private)
{
  if (!raw_private) {
    return;
  }

  struct private *private = raw_private;
  free(private->bitmap);
  free(private);
}

static void load_image(void *raw_private, struct imv_image **image, int *frametime)
{
  *image = NULL;
  *frametime = 0;

  struct private *private = raw_private;

  struct imv_bitmap bmp = imv_bitmap_alloc(private->width, private->height);
  if (!bmp.data) {
    return;
  }
  memcpy(bmp.data, private->bitmap, imv_bitmap_size(bmp));

  *image = imv_image_create_from_bitmap(bmp);

}
static const struct imv_source_vtable vtable = {
  .load_first_frame = load_image,
  .free = free_private
};

static inline uint32_t read_be_uint32(const uint8_t *data) {
  return (data[0] << 24) + (data[1] << 16) + (data[2] << 8)+ (data[3] << 0);
}

static enum backend_result open_memory(void *data, size_t len, struct imv_source **src)
{
  #define HEADER_SIZE 16
  if (len < HEADER_SIZE || memcmp("farbfeld", data, 8) != 0) {
    return BACKEND_UNSUPPORTED;
  }
  const unsigned char *bytes = data;

  struct private *priv = calloc(1, sizeof *priv);
  if (!priv) {
    return BACKEND_UNSUPPORTED;
  }
  priv->width = read_be_uint32(bytes+8);
  priv->height = read_be_uint32(bytes+12);

  if ((size_t)4 * priv->width > SIZE_MAX / priv->height) {
    return BACKEND_UNSUPPORTED;
  }
  size_t bitmap_len = priv->width * priv->height * 4;
  if (len != bitmap_len * 2 + HEADER_SIZE) {
    free(priv);
    return BACKEND_UNSUPPORTED;
  }
  priv->bitmap = malloc(bitmap_len);
  if (!priv->bitmap) {
    free(priv);
    return BACKEND_UNSUPPORTED;
  }
  for (size_t i = 0; i < bitmap_len; i++) {
    priv->bitmap[i] = bytes[2 * i + HEADER_SIZE];
  }

  *src = imv_source_create(&vtable, priv);
  return BACKEND_SUCCESS;
}

static enum backend_result open_path(const char *path, struct imv_source **src)
{
  FILE *f = fopen(path, "rb");
  if (!f) {
    return BACKEND_BAD_PATH;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  if (len <= 0 || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return BACKEND_BAD_PATH;
  }

  char *data = malloc(len);
  if (!data) {
    fclose(f);
    return BACKEND_UNSUPPORTED;
  }
  fread(data, len, 1, f);
  fclose(f);

  enum backend_result res = open_memory(data, len, src);
  free(data);
  return res;
}

const struct imv_backend imv_backend_farbfeld = {
  .name = "farbfeld",
  .description = "Lossless image format which is easy to parse, pipe and compress",
  .website = "https://tools.suckless.org/farbfeld/",
  .license = "ISC",
  .open_path = &open_path,
  .open_memory = &open_memory,
};
