#include <stdlib.h>
#include <string.h>
#include <libheif/heif.h>

#include "backend.h"
#include "bitmap.h"
#include "image.h"
#include "log.h"
#include "source_private.h"

struct private {
  struct heif_image *img;
};

static void free_private(void *raw_private)
{
  if (!raw_private) {
    return;
  }
  struct private *private = raw_private;
  heif_image_release(private->img);
  free(private);
}

static void copy_with_stride(unsigned char *dst, const unsigned char *src, int width, int height, int stride) {
  for (int i = 0; i < height; i++) {
    memcpy(&dst[i * width * BYTES_PER_CHANNEL], &src[i * stride],
        width * BYTES_PER_CHANNEL);
  }
}

static void load_image(void *raw_private, struct imv_image **image, int *frametime)
{
  *image = NULL;
  *frametime = 0;

  struct private *private = raw_private;

  int stride;
  const uint8_t *data = heif_image_get_plane_readonly(
      private->img, heif_channel_interleaved, &stride);

  struct imv_bitmap bmp = imv_bitmap_alloc(
      heif_image_get_width(private->img, heif_channel_interleaved),
      heif_image_get_height(private->img, heif_channel_interleaved));
  if (!bmp.data) {
    return;
  }

  if (bmp.width * BYTES_PER_CHANNEL == stride) {
    memcpy(bmp.data, data, imv_bitmap_size(bmp));
  } else {
    copy_with_stride(bmp.data, data, bmp.width, bmp.height, stride);
  }

  *image = imv_image_create_from_bitmap(bmp);
}

static const struct imv_source_vtable vtable = {
  .load_first_frame = load_image,
  .free = free_private,
};

struct heif_error get_primary_image(struct heif_context *ctx, struct heif_image **img)
{
  struct heif_image_handle *handle;
  struct heif_error err = heif_context_get_primary_image_handle(ctx, &handle);
  if (err.code != heif_error_Ok) {
    imv_log(IMV_ERROR, "libheif: failed to get image handle (%s)\n", err.message);
    return err;
  }

  err = heif_decode_image(handle, img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
  if (err.code != heif_error_Ok) {
    imv_log(IMV_ERROR, "libheif: failed to decode image (%s)\n", err.message);
  }

  heif_image_handle_release(handle);
  return err;
}

static enum backend_result open_path(const char *path, struct imv_source **src)
{
  struct heif_context *ctx = heif_context_alloc();
  struct heif_error err = heif_context_read_from_file(ctx, path, NULL); // TODO: error
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    if (err.code == heif_error_Input_does_not_exist) {
      return BACKEND_BAD_PATH;
    }
    return BACKEND_UNSUPPORTED;
  }

  struct heif_image *img;
  err = get_primary_image(ctx, &img);
  heif_context_free(ctx);
  if (err.code != heif_error_Ok) {
    return BACKEND_UNSUPPORTED;
  }

  struct private *private = malloc(sizeof *private);
  private->img = img;
  *src = imv_source_create(&vtable, private);
  return BACKEND_SUCCESS;
}

static enum backend_result open_memory(void *data, size_t len, struct imv_source **src)
{
  struct heif_context *ctx = heif_context_alloc();
  struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    return BACKEND_UNSUPPORTED;
  }

  struct heif_image *img;
  err = get_primary_image(ctx, &img);
  heif_context_free(ctx);
  if (err.code != heif_error_Ok) {
    return BACKEND_UNSUPPORTED;
  }

  struct private *private = malloc(sizeof *private);
  private->img = img;
  *src = imv_source_create(&vtable, private);
  return BACKEND_SUCCESS;
}

static enum backend_result init(void)
{
    struct heif_error err = heif_init(NULL);
    if (err.code != heif_error_Ok) {
        imv_log(IMV_ERROR, "libheif: failed to initialize backend (%s)\n", err.message);
        heif_deinit();
        return BACKEND_UNSUPPORTED;
    }
    return BACKEND_SUCCESS;
}

static void uninit(void)
{
    heif_deinit();
}

const struct imv_backend imv_backend_libheif = {
  .name = "libheif",
  .description = "ISO/IEC 23008-12:2017 HEIF file format decoder and encoder.",
  .website = "http://www.libheif.org",
  .license = "GNU Lesser General Public License",
  .open_path = &open_path,
  .open_memory = &open_memory,
  .init = &init,
  .uninit = &uninit,
};
