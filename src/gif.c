/*
 * Copyright (c) 2025 Zachary Lamb
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gif.h"

struct vec
{
  gusize size;
  const gu8 *buf;
};

#define LIBGIF_SLOW

#define VEC_MUST(V, S)                                                        \
  if ((V).size < (S))                                                         \
    {                                                                         \
      gif_free (gif);                                                         \
      return GIF_ERR_EOF;                                                     \
    }

#define VEC_ADVANCE(V, N)                                                     \
  do                                                                          \
    {                                                                         \
      if ((V).size < (N))                                                     \
        {                                                                     \
          gif_free (gif);                                                     \
          return GIF_ERR_EOF;                                                 \
        }                                                                     \
      (V).size -= (N);                                                        \
      (V).buf += (N);                                                         \
    }                                                                         \
  while (0)

#define VEC_ADVANCE_UNSAFE(V, N)                                              \
  do                                                                          \
    {                                                                         \
      (V).size -= (N);                                                        \
      (V).buf += (N);                                                         \
    }                                                                         \
  while (0)

#define VEC_AT_UNSAFE(V, I) (V).buf[(I)]

#define VEC_AT_U16_LE_UNSAFE(V, I) (*(gu16 *) ((V).buf + (I)))

int
gif_parse (struct gif *gif, gusize size, const char *buf)
{
  struct vec v = (struct vec) { .size = size, .buf = (const gu8 *) buf };

  memset (gif, 0, sizeof (struct gif));

  VEC_MUST (v, 0xD);

  gu8 version_byte = VEC_AT_UNSAFE (v, 4);

  if (VEC_AT_UNSAFE (v, 0) != 0x47 || VEC_AT_UNSAFE (v, 1) != 0x49
      || VEC_AT_UNSAFE (v, 2) != 0x46 || VEC_AT_UNSAFE (v, 3) != 0x38
      || (version_byte != 0x37 && version_byte != 0x39)
      || VEC_AT_UNSAFE (v, 5) != 0x61)
    return GIF_ERR_BAD_DATA;

  switch (version_byte)
    {
    case 0x37:
      gif->version = GIF_VERSION_87A;
      break;
    case 0x39:
      gif->version = GIF_VERSION_89A;
      break;
    }

  gif->width  = VEC_AT_U16_LE_UNSAFE (v, 6);
  gif->height = VEC_AT_U16_LE_UNSAFE (v, 8);

  gif->num_images = 0;
  gif->images_cap = 0;
  gif->images     = NULL;

  gu8 packed_byte = VEC_AT_UNSAFE (v, 0xA);
  gif->bg_index
      = VEC_AT_UNSAFE (v, 0xB); // NOTE: meaningless if GCT not present

  VEC_ADVANCE_UNSAFE (v, 0xD);

  if (packed_byte & 0x80)
    {
      gusize num_bytes;
      gif->flags |= GIF_FLAG_GCT;
      gif->gct.num_colors = 1 << ((packed_byte & 0x7) + 1);

      if (gif->bg_index >= gif->gct.num_colors)
        return GIF_ERR_BAD_DATA;

      num_bytes = gif->gct.num_colors * 3;
      VEC_MUST (v, num_bytes);

      gif->gct.colors = malloc (num_bytes);
      if (gif->gct.colors == NULL)
        return GIF_ERR_NOMEM;

      memcpy (gif->gct.colors, v.buf, num_bytes);

      VEC_ADVANCE_UNSAFE (v, num_bytes);
    }

  struct gif_frame frame = { 0 };
  gu8 is_frame           = 0;

read_block:
  VEC_MUST (v, 1);

  gu8 separator = VEC_AT_UNSAFE (v, 0);
  gusize bytes;

  switch (separator)
    {
    case 0x2C: // image descriptor
      {
        struct gif_image img = { 0 };
        gu8 is_interlaced    = 0;
        size_t num_colors;

        VEC_MUST (v, 0xA);

        if (gif->num_images == gif->images_cap)
          {
            gusize ncap = gif->images_cap + 8;
            struct gif_image *imgs
                = realloc (gif->images, ncap * sizeof (struct gif_image));

            if (imgs == NULL)
              {
                gif_free (gif);
                return GIF_ERR_NOMEM;
              }

            gif->images_cap = ncap;
            gif->images     = imgs;
          }

        img.gif    = gif;
        img.x      = VEC_AT_U16_LE_UNSAFE (v, 1);
        img.y      = VEC_AT_U16_LE_UNSAFE (v, 3);
        img.width  = VEC_AT_U16_LE_UNSAFE (v, 5);
        img.height = VEC_AT_U16_LE_UNSAFE (v, 7);

        if (!img.width || !img.height
            || (gu32) img.x + (gu32) img.width > (gu32) gif->width
            || (gu32) img.y + (gu32) img.height > (gu32) gif->height)
          {
            gif_free (gif);
            return GIF_ERR_BAD_DATA;
          }

        gu8 packed_byte = VEC_AT_UNSAFE (v, 9);
        VEC_ADVANCE_UNSAFE (v, 0xA);

        if (packed_byte & 0x80)
          {
            gusize num_bytes;
            img.lct.num_colors = 1 << ((packed_byte & 0x7) + 1);
            num_colors         = img.lct.num_colors;
            num_bytes          = img.lct.num_colors * 3;

            VEC_MUST (v, num_bytes);

            img.lct.colors = malloc (num_bytes);

            if (img.lct.colors == NULL)
              {
                gif_free (gif);
                return GIF_ERR_NOMEM;
              }

            memcpy (img.lct.colors, v.buf, num_bytes);

            VEC_ADVANCE_UNSAFE (v, num_bytes);
          }
        else if (!(gif->flags & GIF_FLAG_GCT))
          {
            gif_free (gif);
            return GIF_ERR_BAD_DATA;
          }
        else
          num_colors = gif->gct.num_colors;

        if (packed_byte & 0x40)
          is_interlaced = 1;

        if (is_frame)
          {
            is_frame = 0;
            img.flags |= GIF_IMAGE_FLAG_FRAME;
            img.frame = frame;
          }

        gif->images[gif->num_images++] = img;
        struct gif_image *imgp         = gif->images + (gif->num_images - 1);

        VEC_MUST (v, 2);

        struct gif_code code_table[4096];
        gu8 first_code = 1, min_lzw_code_size, lzw_code_size;
        gu16 prev_code, code = 0, code_off = 0, bit_off = 0, next_code;
        gu32 num_indices = 0, req_indices = img.width * img.height;

        imgp->indices = malloc (sizeof (gu8) * req_indices);
        if (imgp->indices == NULL)
          {
            gif_free (gif);
            return GIF_ERR_NOMEM;
          }

        min_lzw_code_size = VEC_AT_UNSAFE (v, 0);
        lzw_code_size     = min_lzw_code_size + 1;

        VEC_ADVANCE_UNSAFE (v, 1);

        bytes = VEC_AT_UNSAFE (v, 0);

        if (!bytes || (min_lzw_code_size < 2 || min_lzw_code_size > 8)
            || num_colors > (1UL << min_lzw_code_size))
          {
            gif_free (gif);
            return GIF_ERR_BAD_DATA;
          }

        next_code = (1LU << min_lzw_code_size) + 2;

        for (gu16 i = 0; i < num_colors; i++)
          {
            code_table[i].len         = 1;
            code_table[i].prefix_code = GIF_CODE_NO_PREFIX;
            code_table[i].inuse       = 1;
            code_table[i].index       = i;
            code_table[i].first_index = i;
          }

        for (gu16 i = num_colors; i < 4096; i++)
          code_table[i].inuse = 0;

        while (bytes)
          {
            VEC_MUST (v, bytes + 2);
            VEC_ADVANCE_UNSAFE (v, 1);

            // handle code stream
            while (bytes)
              {
                gu8 bits = 8 - bit_off;
                if (lzw_code_size - code_off < bits)
                  bits = lzw_code_size - code_off;

                code |= ((VEC_AT_UNSAFE (v, 0) >> bit_off) & ((1 << bits) - 1))
                        << code_off;
                bit_off += bits;
                code_off += bits;

                if (bit_off == 8)
                  {
                    bit_off = 0;
                    --bytes;
                    VEC_ADVANCE_UNSAFE (v, 1);
                  }

                if (code_off == lzw_code_size)
                  {
                    // EOI code
                    if (code == (1 << min_lzw_code_size) + 1)
                      {
                        if (bytes)
                          VEC_ADVANCE_UNSAFE (v, bytes);
                        goto end_code_stream;
                      }

                    // clear code
                    if (code == (1 << min_lzw_code_size))
                      {
                        first_code    = 1;
                        lzw_code_size = min_lzw_code_size + 1;
                        next_code     = (1UL << min_lzw_code_size) + 2;

                        // reset code table to initial state
                        for (gu16 i = num_colors; i < 4096; i++)
                          code_table[i].inuse = 0;

                        code     = 0;
                        code_off = 0;

                        continue;
                      }

                    if (first_code)
                      {
                        if (code >= num_colors)
                          {
                            gif_free (gif);
                            return GIF_ERR_BAD_DATA;
                          }

                        if (num_indices >= req_indices)
                          goto end_code_stream;

                        imgp->indices[num_indices++] = code;

                        first_code = 0;
                        goto end_code;
                      }

                    gu16 len         = code_table[prev_code].len + 1,
                         prefix_code = prev_code;
                    gu8 index;

#ifdef LIBGIF_SLOW
                    if (code > 4095)
                      {
                        gif_free (gif);
                        return GIF_ERR_FAULT;
                      }
#endif

                    if (code_table[code].inuse)
                      {
                        index = code_table[code].first_index;

                        gu16 tmplen = code_table[code].len, tmpc = code,
                             tmpi = 0;
                        gu8 stack[tmplen];

                        if (num_indices + tmplen > req_indices)
                          goto end_code_stream;

                        do
                          {
#ifdef LIBGIF_SLOW
                            if (tmpc > 4095 || tmpi >= tmplen)
                              {
                                gif_free (gif);
                                return GIF_ERR_FAULT;
                              }
#endif
                            stack[(tmplen - tmpi++) - 1]
                                = code_table[tmpc].index;
                            tmpc = code_table[tmpc].prefix_code;
                          }
                        while (tmpc != GIF_CODE_NO_PREFIX);

                        for (gu16 i = 0; i < tmplen; i++)
                          imgp->indices[num_indices++] = stack[i];
                      }
                    else
                      {
                        index = code_table[prev_code].first_index;

                        gu16 tmplen = code_table[prev_code].len + 1,
                             tmpc = prev_code, tmpi = 1;
                        gu8 stack[tmplen];

                        stack[(tmplen - 1)] = index;

                        if (num_indices + tmplen > req_indices)
                          goto end_code_stream;

                        do
                          {
#ifdef LIBGIF_SLOW
                            if (tmpc > 4095 || tmpi >= tmplen)
                              {
                                gif_free (gif);
                                return GIF_ERR_FAULT;
                              }
#endif
                            stack[(tmplen - tmpi++) - 1]
                                = code_table[tmpc].index;
                            tmpc = code_table[tmpc].prefix_code;
                          }
                        while (tmpc != GIF_CODE_NO_PREFIX);

                        for (gu16 i = 0; i < tmplen; i++)
                          imgp->indices[num_indices++] = stack[i];
                      }

                    if (num_indices > req_indices)
                      goto end_code_stream;

                    if (next_code == 4096)
                      goto end_code;

                    code_table[next_code].len         = len;
                    code_table[next_code].prefix_code = prefix_code;
                    code_table[next_code].inuse       = 1;
                    code_table[next_code].index       = index;
                    code_table[next_code].first_index
                        = code_table[prev_code].first_index;
                    ++next_code;

                    if (next_code == 4096)
                      goto end_code;

                    if (next_code == (1 << lzw_code_size))
                      ++lzw_code_size;

                  end_code:
                    prev_code = code;
                    code      = 0;
                    code_off  = 0;
                  }
              }

            bytes = VEC_AT_UNSAFE (v, 0);
          }

      end_code_stream:

        if (num_indices != req_indices)
          {
            gif_free (gif);
            return GIF_ERR_BAD_DATA;
          }

        VEC_ADVANCE (v, 1);

        if (is_interlaced)
          {
            // reconstruct proper indices from interlaced data
            const gusize indices_len = sizeof (gu8) * req_indices;
            gu8 *tmpindices          = malloc (indices_len);

            if (tmpindices == NULL)
              {
                gif_free (gif);
                return GIF_ERR_NOMEM;
              }

            static const gu32 row_increments[] = { 8, 8, 4, 2 };
            static const gu32 row_offsets[]    = { 0, 4, 2, 1 };

            const gu32 row_stride = sizeof (gu8) * imgp->width;
            gu32 g_row_offset     = 0;

            for (gu32 i = 0; i < sizeof (row_increments) / sizeof (gu32); i++)
              {
                gu32 row_increment = row_increments[i];
                gu32 row_offset    = row_offsets[i];

                if (imgp->height <= row_offset)
                  continue;

                gu32 num_rows
                    = ((imgp->height - row_offset) - 1) / row_increment + 1;

                for (gu32 j = 0; j < num_rows; j++)
                  {
#ifdef LIBGIF_SLOW
                    if ((row_increment * j + row_offset) * row_stride
                            >= indices_len
                        || (j + g_row_offset) * row_stride >= indices_len)
                      {
                        gif_free (gif);
                        return GIF_ERR_FAULT;
                      }
#endif

                    memcpy (tmpindices
                                + (row_increment * j + row_offset)
                                      * row_stride,
                            imgp->indices + (j + g_row_offset) * row_stride,
                            row_stride);
                  }

                g_row_offset += num_rows;
              }

            memcpy (imgp->indices, tmpindices, indices_len);
            free (tmpindices);
          }

        break;
      }
    case 0x21: // ext introducer
      VEC_MUST (v, 3);

      gu8 label = VEC_AT_UNSAFE (v, 1);

      VEC_ADVANCE_UNSAFE (v, 2);
      bytes = VEC_AT_UNSAFE (v, 0);

      if (label == 0xF9 && bytes == 0x4)
        {
          VEC_MUST (v, bytes + 2);

          is_frame = 1;

          gu8 packed_byte       = VEC_AT_UNSAFE (v, 1);
          frame.disposal_method = (packed_byte >> 2) & 7;

          if (frame.disposal_method == 0 || frame.disposal_method > 3)
            frame.disposal_method = GIF_FRAME_DISPOSE_NONE;

          if (packed_byte & 2)
            frame.flags |= GIF_FRAME_FLAG_USER_INPUT;

          if (packed_byte & 1)
            {
              frame.flags |= GIF_FRAME_FLAG_TRANSPARENT;
              frame.transparent_index = VEC_AT_UNSAFE (v, 4);
            }

          frame.delay_time = VEC_AT_U16_LE_UNSAFE (v, 2);

          VEC_ADVANCE_UNSAFE (v, bytes + 1);

          if ((bytes = VEC_AT_UNSAFE (v, 0)) != 0)
            {
              gif_free (gif);
              return GIF_ERR_BAD_DATA;
            }

          VEC_ADVANCE (v, 1);

          break;
        }

      while (bytes)
        {
          VEC_MUST (v, bytes + 2);
          VEC_ADVANCE_UNSAFE (v, bytes + 1);
          bytes = VEC_AT_UNSAFE (v, 0);
        }

      VEC_ADVANCE (v, 1);

      break;
    case 0x3B: // trailer
               // what if v.size > 1?
      goto finish;
    default: // unknown separator
      gif_free (gif);
      return GIF_ERR_BAD_DATA;
    }

  goto read_block;

finish:
  return GIF_SUCCESS;
}

void
gif_free (struct gif *gif)
{
  if (gif->flags & GIF_FLAG_GCT)
    free (gif->gct.colors);

  for (gu32 i = 0; i < gif->num_images; i++)
    {
      struct gif_image *img = gif->images + i;

      if (img->flags & GIF_IMAGE_FLAG_LCT)
        free (img->lct.colors);

      free (img->indices);
    }

  if (gif->num_images)
    free (gif->images);

  memset (gif, 0, sizeof (struct gif));
}

const char *
gif_strerr (int gif_err)
{
  switch (gif_err)
    {
    case GIF_SUCCESS:
      return "no error";
    case GIF_ERR_NOMEM:
      return "out of memory";
    case GIF_ERR_EOF:
      return "GIF data truncated";
    case GIF_ERR_BAD_DATA:
      return "GIF invalid data";
    case GIF_ERR_FAULT:
      return "internal error";
    default:
      return "unknown error";
    }
}

struct gif_color_table *
gif_image_get_palette (struct gif_image *image)
{
  if (image->flags & GIF_IMAGE_FLAG_LCT)
    return &image->lct;

  if (image->gif == NULL || !(image->gif->flags & GIF_FLAG_GCT))
    return NULL;

  return &image->gif->gct;
}
