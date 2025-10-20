#ifndef GIF_H
#define GIF_H 1

#include <stddef.h>
#include <stdint.h>

typedef uint8_t gu8;
typedef uint16_t gu16;
typedef uint32_t gu32;
typedef uint64_t gu64;

typedef int8_t gi8;
typedef int16_t gi16;
typedef int32_t gi32;
typedef int64_t gi64;

typedef size_t gusize;

#define GIF_VERSION_87A 0
#define GIF_VERSION_89A 1

#define GIF_SUCCESS      0
#define GIF_ERR_NOMEM    -1
#define GIF_ERR_EOF      -2
#define GIF_ERR_BAD_DATA -3
#define GIF_ERR_FAULT    -4

#define GIF_FLAG_GCT 1

#define GIF_CODE_NO_PREFIX 0xFFFF

#define GIF_IMAGE_FLAG_LCT   (1 << 0)
#define GIF_IMAGE_FLAG_FRAME (1 << 1)

#define GIF_FRAME_FLAG_TRANSPARENT (1 << 0)
#define GIF_FRAME_FLAG_USER_INPUT  (1 << 1)

#define GIF_FRAME_DISPOSE_NONE    1
#define GIF_FRAME_DISPOSE_ALL     2
#define GIF_FRAME_DISPOSE_RESTORE 3

struct gif_color_table
{
  gu16 num_colors;
  gu8 *colors;
};

struct gif_code
{
  gu16 len;
  gu16 prefix_code;
  gu8 inuse;
  gu8 index;
  gu8 first_index;
};

struct gif_frame
{
  gu8 flags;
  gu8 disposal_method;
  gu16 delay_time;
  gu8 transparent_index;
};

struct gif_image
{
  struct gif *gif;
  gu16 x, y, width, height;
  gu8 flags;
  struct gif_color_table lct;
  struct gif_frame frame;
  gu8 *indices;
};

struct gif
{
  gu8 version;
  gu16 width;
  gu16 height;
  gu8 flags;
  gu8 bg_index;
  struct gif_color_table gct;
  gu32 num_images, images_cap;
  struct gif_image *images;
};

int gif_parse (struct gif *gif, size_t size, const char *buf);
void gif_free (struct gif *gif);

struct gif_color_table *gif_image_get_palette (struct gif_image *image);

const char *gif_strerr (int gif_err);

#endif
