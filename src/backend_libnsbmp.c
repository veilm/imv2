#include "backend.h"
#include "bitmap.h"
#include "image.h"
#include "source_private.h"

#include <libnsbmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct private {
  bmp_image img;
  void *data;
};

static const size_t BPP = 4;

static void free_private(void *raw_private)
{
  if (!raw_private) {
    return;
  }

  struct private *private = raw_private;
  bmp_finalise(&private->img);
  free(private->data);
  free(private);
}

static void* bitmap_create(int width, int height, unsigned int state)
{
  (void)state; // We always clear and use 4 bpp
  return calloc(1, width * height * BPP);
}

static void bitmap_destroy(void *bitmap)
{
  free(bitmap);
}

static unsigned char* bitmap_get_buffer(void *bitmap)
{
  return bitmap;
}

static void load_image(void *raw_private, struct imv_image **image, int *frametime)
{
  *image = NULL;
  *frametime = 0;

  struct private *priv = raw_private;

  if (bmp_decode(&priv->img) != BMP_OK) {
    return;
  }
  struct imv_bitmap bmp = imv_bitmap_alloc(priv->img.width, priv->img.height);
  if (!bmp.data) {
    return;
  }
  memcpy(bmp.data, priv->img.bitmap, imv_bitmap_size(bmp));
  *image = imv_image_create_from_bitmap(bmp);
}

static enum backend_result open_memory_internal(void *data, size_t len, struct
                                                imv_source **src, bool own_data)
{
  static bmp_bitmap_callback_vt bmp_cbs = {
    bitmap_create,
    bitmap_destroy,
    bitmap_get_buffer,
  };

  static struct imv_source_vtable vtable = {
    .load_first_frame = load_image,
    .free = free_private
  };

  bmp_image img;
  if (bmp_create(&img, &bmp_cbs) != BMP_OK ||
      bmp_analyse(&img, len, data) != BMP_OK) {
    if (own_data) {
      free(data);
    }
    return BACKEND_UNSUPPORTED;
  }
  struct private *priv = calloc(1, sizeof *priv);
  if (!priv) {
    if (own_data) {
      free(data);
    }
    return BACKEND_UNSUPPORTED;
  }
  priv->img = img;
  if(own_data) {
    priv->data = data;
  }
  *src = imv_source_create(&vtable, priv);
  return BACKEND_SUCCESS;
}

static enum backend_result open_memory(void *data, size_t len, struct imv_source **src)
{
  return open_memory_internal(data, len, src, false);
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

  return open_memory_internal(data, len, src, true);
}

const struct imv_backend imv_backend_libnsbmp = {
  .name = "libnsbmp",
  .description = "BMP and ICO decoding library from the NetSurf project",
  .website = "https://www.netsurf-browser.org/projects/libnsbmp/",
  .license = "MIT",
  .open_path = &open_path,
  .open_memory = &open_memory,
};
