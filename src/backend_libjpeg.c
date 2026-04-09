#include "backend.h"
#include "bitmap.h"
#include "log.h"
#include "image.h"
#include "source.h"
#include "source_private.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <turbojpeg.h>
#ifdef IMV_BACKEND_LCMS2
#include <jpeglib.h>
#include <lcms2.h>
#endif

struct private {
  int fd;
  void *data;
  size_t len;
  tjhandle jpeg;
  int width;
  int height;
  int subsamp;
  int colorspace;
};

static void free_private(void *raw_private)
{
  if (!raw_private) {
    return;
  }
  struct private *private = raw_private;
  tjDestroy(private->jpeg);
  if (private->fd >= 0) {
    munmap(private->data, private->len);
    close(private->fd);
  }

  free(private);
}

static int decompress_from_rgba(struct private *private, void *bitmap)
{
  return tjDecompress2(private->jpeg, private->data, private->len, bitmap,
      private->width, 0, private->height, TJPF_RGBA, TJFLAG_FASTDCT);
}

#ifdef IMV_BACKEND_LCMS2
static void extract_icc_profile(struct private *private,
    unsigned char **icc_data, unsigned int *icc_length, unsigned char *inverted)
{
  *icc_data = NULL;
  *icc_length = 0;

  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);

  jpeg_create_decompress(&cinfo);
  jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);
  jpeg_save_markers(&cinfo, JPEG_APP0 + 14, 0xFFFF);

  jpeg_mem_src(&cinfo, private->data, private->len);
  jpeg_read_header(&cinfo, TRUE);

  if (jpeg_read_icc_profile(&cinfo, icc_data, icc_length)) {
    imv_log(IMV_DEBUG, "ICC profile extracted: %u bytes\n", *icc_length);
  }

  if (cinfo.saw_Adobe_marker && cinfo.Adobe_transform == 2) {
    *inverted = 1;
  } else if (cinfo.num_components == 4) {
    int cid0 = cinfo.comp_info[0].component_id;
    int cid1 = cinfo.comp_info[1].component_id;
    int cid2 = cinfo.comp_info[2].component_id;
    int cid3 = cinfo.comp_info[3].component_id;

    if (cid0 == 0x01 && cid1 == 0x02 && cid2 == 0x03 && cid3 == 0x04) {
      // YCCK encoding, not inverted
      *inverted = 0;
    } else if (cid0 == 'C' && cid1 == 'M' && cid2 == 'Y' && cid3 == 'K') {
      // Adobe CMYK encoding, inverted
      // https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3c17063ef1ab43f5877f19d670dc39497c5cd036/libjpeg.txt#L1569-L1582
      *inverted = 1;
    } else {
      imv_log(IMV_DEBUG, "Unknown component ids");
    }
  } else {
    imv_log(IMV_DEBUG, "Wrong number of component ids");
  }

  jpeg_destroy_decompress(&cinfo);
}

static int naive_cmyk_conversion(
    unsigned char *cmyk_buffer, struct private *private, unsigned char *bitmap)
{
  imv_log(IMV_WARNING, "No usable embedded ICC profile found, using default "
                       "conversion. Colors may not be accurate\n");
  for (int i = 0; i < private->height * private->width * 4; i += 4) {
    unsigned char c = cmyk_buffer[i + 0];
    unsigned char m = cmyk_buffer[i + 1];
    unsigned char y = cmyk_buffer[i + 2];
    unsigned char k = cmyk_buffer[i + 3];

    bitmap[i + 0] = ((255 - c) * (255 - k)) / 255;
    bitmap[i + 1] = ((255 - m) * (255 - k)) / 255;
    bitmap[i + 2] = ((255 - y) * (255 - k)) / 255;
    bitmap[i + 3] = 255;
  }

  return 0;
}

static void cmc_error_handler(
    cmsContext context, cmsUInt32Number error_code, const char *text)
{
  (void)context;
  imv_log(IMV_ERROR, "lcms2 error: %s (%d)\n", text, error_code);
}

static int icc_cmyk_conversion(cmsHPROFILE cmyk_profile,
    unsigned char *cmyk_buffer, void *bitmap, struct private *private)
{
  // lcms2 cannot handle alpha channels when converting between different
  // formats, i.e. CMYK to RGBA, so we make sure the alpha channel is
  // initialized at 0xff (see https://github.com/mm2/Little-CMS/issues/281)
  memset(bitmap, 0xff, private->height * private->width * 4);
  cmsSetLogErrorHandler(cmc_error_handler);
  cmsHPROFILE output_profile = cmsCreate_sRGBProfile();
  cmsHTRANSFORM transform = cmsCreateTransform(cmyk_profile, TYPE_CMYK_8,
      output_profile, TYPE_RGBA_8, INTENT_PERCEPTUAL, 0);

  if (transform != NULL) {
    cmsDoTransform(
        transform, cmyk_buffer, bitmap, private->width * private->height);
    cmsDeleteTransform(transform);
  } else {
    imv_log(IMV_ERROR, "Could not create image transform\n");
    return -1;
  }

  cmsCloseProfile(output_profile);
  return 0;
}

static int convert_cmyk_colors(unsigned char *cmyk_buffer,
    struct private *private, unsigned char *bitmap, unsigned char *icc_data,
    unsigned int icc_length)
{
  cmsHPROFILE cmyk_profile = NULL;
  if (icc_data && icc_length > 0) {
    cmyk_profile = cmsOpenProfileFromMem(icc_data, icc_length);
  }

  if (cmyk_profile == NULL) {
    return naive_cmyk_conversion(cmyk_buffer, private, bitmap);
  } else {
    int ret = icc_cmyk_conversion(cmyk_profile, cmyk_buffer, bitmap, private);
    cmsCloseProfile(cmyk_profile);
    return ret;
  }
}

static int decompress_from_cmyk(struct private *private, void *bitmap)
{
  unsigned char *icc_data = NULL;
  unsigned int icc_length = 0;
  unsigned char inverted = 0;
  extract_icc_profile(private, &icc_data, &icc_length, &inverted);

  unsigned char *cmyk_buffer = malloc(private->height * private->width * 4);
  int rcode =
      tjDecompress2(private->jpeg, private->data, private->len, cmyk_buffer,
          private->width, 0, private->height, TJPF_CMYK, TJFLAG_ACCURATEDCT);
  if (rcode) {
    free(cmyk_buffer);
    return rcode;
  }

  if (inverted) {
    for (int i = 0; i < private->width * private->height * 4; i++) {
      cmyk_buffer[i] = 255 - cmyk_buffer[i];
    }
  }

  rcode =
      convert_cmyk_colors(cmyk_buffer, private, bitmap, icc_data, icc_length);

  free(cmyk_buffer);
  free(icc_data);

  return rcode;
}
#else
static int decompress_from_cmyk(struct private *private, void *bitmap)
{
  (void)private;
  (void)bitmap;
  imv_log(IMV_ERROR, "lcms2 support is needed to show CMYK Jpeg images\n");
  return -1;
}
#endif

static void load_image(void *raw_private, struct imv_image **image, int *frametime)
{
  *image = NULL;
  *frametime = 0;

  struct private *private = raw_private;

  struct imv_bitmap bmp = imv_bitmap_alloc(private->width, private->height);
  if (!bmp.data) {
    return;
  }

  int rcode = -1;
  if (private->colorspace == TJCS_CMYK || private->colorspace == TJCS_YCCK) {
    rcode = decompress_from_cmyk(private, bmp.data);
  } else {
    rcode = decompress_from_rgba(private, bmp.data);
  }

  if (rcode) {
    int err = tjGetErrorCode(private->jpeg);
    if (err == TJERR_WARNING) {
      imv_log(IMV_WARNING, "Non fatal error while decompressing image: %s\n",
          tjGetErrorStr2(private->jpeg));
    } else {
      imv_log(IMV_ERROR, "Fatal error while decompressing image: %s\n",
          tjGetErrorStr2(private->jpeg), tjGetErrorCode(private->jpeg),
          TJERR_WARNING);
      imv_bitmap_free(bmp);
      return;
    }
  }

  *image = imv_image_create_from_bitmap(bmp);
}

static const struct imv_source_vtable vtable = {
  .load_first_frame = load_image,
  .free = free_private
};

static enum backend_result open_path(const char *path, struct imv_source **src)
{
  struct private private;

  private.fd = open(path, O_RDONLY);
  if (private.fd < 0) {
    return BACKEND_BAD_PATH;
  }

  off_t len = lseek(private.fd, 0, SEEK_END);
  if (len < 0) {
    close(private.fd);
    return BACKEND_BAD_PATH;
  }

  private.len = len;

  private.data = mmap(NULL, private.len, PROT_READ, MAP_PRIVATE, private.fd, 0);
  if (private.data == MAP_FAILED || !private.data) {
    close(private.fd);
    return BACKEND_BAD_PATH;
  }

  private.jpeg = tjInitDecompress();
  if (!private.jpeg) {
    munmap(private.data, private.len);
    close(private.fd);
    return BACKEND_UNSUPPORTED;
  }

  int rcode = tjDecompressHeader3(private.jpeg, private.data, private.len,
      &private.width, &private.height, &private.subsamp, &private.colorspace);
  if (rcode) {
    tjDestroy(private.jpeg);
    munmap(private.data, private.len);
    close(private.fd);
    return BACKEND_UNSUPPORTED;
  }

  struct private *new_private = malloc(sizeof private);
  memcpy(new_private, &private, sizeof private);

  *src = imv_source_create(&vtable, new_private);
  return BACKEND_SUCCESS;
}

static enum backend_result open_memory(void *data, size_t len, struct imv_source **src)
{
  struct private private;

  private.fd = -1;
  private.data = data;
  private.len = len;

  private.jpeg = tjInitDecompress();
  if (!private.jpeg) {
    return BACKEND_UNSUPPORTED;
  }

  int rcode = tjDecompressHeader3(private.jpeg, private.data, private.len,
      &private.width, &private.height, &private.subsamp, &private.colorspace);
  if (rcode) {
    tjDestroy(private.jpeg);
    return BACKEND_UNSUPPORTED;
  }

  struct private *new_private = malloc(sizeof private);
  memcpy(new_private, &private, sizeof private);

  *src = imv_source_create(&vtable, new_private);
  return BACKEND_SUCCESS;
}

const struct imv_backend imv_backend_libjpeg = {
  .name = "libjpeg-turbo",
  .description = "Fast JPEG codec based on libjpeg. "
                 "This software is based in part on the work "
                 "of the Independent JPEG Group.",
  .website = "https://libjpeg-turbo.org/",
  .license = "The Modified BSD License",
  .open_path = &open_path,
  .open_memory = &open_memory,
};
