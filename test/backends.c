#include "backend.h"
#include "backends.h"
#include "image.h"
#include "source.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

static int setup(void **state)
{
  struct backends *answer = backends_create();
  if (!answer) {
    return -1;
  }
  *state = answer;
  return 0;
}

static int teardown(void **state)
{
  backends_free(*state);
  return 0;
}

static void test_open_garbage_fails(void **state)
{
  char data[] = {1, 2, 3, 4};
  struct imv_source *src;
  enum backend_result res =
      backends_open_memory(*state, &data, sizeof(data), &src);
  assert_int_equal(res, BACKEND_UNSUPPORTED);
}

static const uint8_t SAMPLE_WIDTH = 3;
static const uint8_t SAMPLE_HEIGHT = 3;
static const uint8_t SAMPLE_RAW[] = {0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00,
    0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0x88, 0x88, 0x88,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff};

static int imv_min(int a, int b) { return a < b ? a : b; }
static int imv_max(int a, int b) { return a > b ? a : b; }

static void assert_bitmap_equal_to_sample(
    const struct imv_bitmap *bitmap, int tolerance)
{
  assert_int_equal(bitmap->width, SAMPLE_WIDTH);
  assert_int_equal(bitmap->height, SAMPLE_HEIGHT);
  if (tolerance == 0) {
    assert_memory_equal(bitmap->data, &SAMPLE_RAW, sizeof(SAMPLE_RAW));
  } else {
    for (size_t i = 0; i < sizeof(SAMPLE_RAW); i++) {
      assert_in_range(bitmap->data[i],
          imv_max((int)SAMPLE_RAW[i] - tolerance, 0),
          imv_min((int)SAMPLE_RAW[i] + tolerance, UINT8_MAX));
    }
  }
}

static void test_opening_sample_from_memory(
    struct backends *backends, void *data, size_t len, int tolerance)
{
  struct imv_source *src;
  assert_int_equal(
      backends_open_memory(backends, data, len, &src), BACKEND_SUCCESS);

  struct imv_image *image;
  int frametime;
  assert_true(imv_source_load_first_frame(src, &image, &frametime));

  assert_non_null(image);
  assert_int_equal(frametime, 0);
  const struct imv_bitmap *bitmap = imv_image_get_bitmap(image);
  assert_non_null(bitmap);
  assert_bitmap_equal_to_sample(bitmap, tolerance);

  imv_image_free(image);
  imv_source_free(src);
}

#ifdef IMV_BACKEND_LIBTIFF
extern unsigned char sample_tiff[];
extern unsigned int sample_tiff_len;
static void test_open_ttf_file(void **state)
{
  test_opening_sample_from_memory(*state, sample_tiff, sample_tiff_len, 0);
}
#endif

#ifdef IMV_BACKEND_LIBPNG
extern unsigned char sample_png[];
extern unsigned int sample_png_len;
static void test_open_png_file(void **state)
{
  test_opening_sample_from_memory(*state, sample_png, sample_png_len, 0);
}
#endif

#ifdef IMV_BACKEND_LIBWEBP
extern unsigned char sample_webp[];
extern unsigned int sample_webp_len;
static void test_open_webp_file(void **state)
{
  test_opening_sample_from_memory(*state, sample_webp, sample_webp_len, 0);
}
#endif

#ifdef IMV_BACKEND_LIBJPEG
extern unsigned char sample_jpeg[];
extern unsigned int sample_jpeg_len;
static void test_open_jpeg_file(void **state)
{
  test_opening_sample_from_memory(*state, sample_jpeg, sample_jpeg_len, 20);
}

#ifdef IMV_BACKEND_LCMS2
extern unsigned char sample_cmyk_jpeg[];
extern unsigned int sample_cmyk_jpeg_len;
static void test_open_cmyk_jpeg_file(void **state)
{
  test_opening_sample_from_memory(
      *state, sample_cmyk_jpeg, sample_cmyk_jpeg_len, 20);
}
#endif
#endif

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_open_garbage_fails),
#ifdef IMV_BACKEND_LIBTIFF
      cmocka_unit_test(test_open_ttf_file),
#endif
#ifdef IMV_BACKEND_LIBPNG
      cmocka_unit_test(test_open_png_file),
#endif
#ifdef IMV_BACKEND_LIBWEBP
      cmocka_unit_test(test_open_webp_file),
#endif
#ifdef IMV_BACKEND_LIBJPEG
      cmocka_unit_test(test_open_jpeg_file),
#ifdef IMV_BACKEND_LCMS2
      cmocka_unit_test(test_open_cmyk_jpeg_file),
#endif
#endif
  };

  return cmocka_run_group_tests(tests, setup, teardown);
}
