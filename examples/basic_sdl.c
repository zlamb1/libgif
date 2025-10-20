#include <stdio.h>
#include <stdlib.h>

#include "gif.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#define READALL_CHUNK (1024 * 128)
#define READALL_NOMEM -1
#define READALL_NFILE -2
#define READALL_ERROR -3

static SDL_Window *window     = NULL;
static SDL_Renderer *renderer = NULL;

gusize current_frame = 0, num_frames = 0;

struct animation_frame
{
  struct gif_image *image;
  SDL_Texture *texture;
} *frames = NULL;

static int
readall (const char *filename, char **buf, size_t *size)
{
  FILE *fp     = NULL;
  char *tmpbuf = NULL, *tmpbuf2;
  size_t tmpread, tmpsize = 0;

  fp = fopen (filename, "rb");

  if (fp == NULL)
    return READALL_NFILE;

  tmpbuf = malloc (READALL_CHUNK);

  if (tmpbuf == NULL)
    {
      fclose (fp);
      return READALL_NOMEM;
    }

  while ((tmpread = fread (tmpbuf + tmpsize, 1, READALL_CHUNK, fp))
         == READALL_CHUNK)
    {
      tmpsize += READALL_CHUNK;
      tmpbuf2 = realloc (tmpbuf, tmpsize + READALL_CHUNK);

      if (tmpbuf2 == NULL)
        {
          free (tmpbuf);
          fclose (fp);
          return READALL_NOMEM;
        }

      tmpbuf = tmpbuf2;
    }

  tmpsize += tmpread;

  if (feof (fp) == 0)
    {
      free (tmpbuf);
      fclose (fp);
      return READALL_ERROR;
    }

  tmpbuf2 = realloc (tmpbuf, tmpsize);

  if (tmpbuf2 == NULL)
    {
      free (tmpbuf);
      fclose (fp);
      return READALL_NOMEM;
    }

  *buf  = tmpbuf2;
  *size = tmpsize;

  fclose (fp);

  return 0;
}

static int
sdl_init (struct gif *gif)
{
  if (!gif->num_images)
    return -1;

  if (!SDL_Init (SDL_INIT_VIDEO))
    {
      fprintf (stderr, "SDL3: failed to init video\n");
      return -1;
    }

  if (!SDL_CreateWindowAndRenderer ("Basic GIF Example", gif->width,
                                    gif->height, SDL_WINDOW_RESIZABLE, &window,
                                    &renderer))
    {
      fprintf (stderr, "SDL3: failed to create window\n");
      return -1;
    }

  // SDL_SetRenderVSync (renderer, 1);
  SDL_SetRenderLogicalPresentation (renderer, gif->width, gif->height,
                                    SDL_LOGICAL_PRESENTATION_LETTERBOX);

  frames = malloc (sizeof (struct animation_frame) * gif->num_images);

  if (frames == NULL)
    return -1;

  for (gusize i = 0; i < gif->num_images; i++)
    {
      struct gif_image *image         = gif->images + i;
      struct gif_color_table *palette = gif_image_get_palette (image);

      if (!(image->flags & GIF_IMAGE_FLAG_FRAME))
        continue;

      struct animation_frame *frame = frames + num_frames++;

      frame->image   = image;
      frame->texture = SDL_CreateTexture (renderer, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET,
                                          image->width, image->height);
      SDL_SetTextureScaleMode (frame->texture, SDL_SCALEMODE_NEAREST);
      SDL_SetTextureBlendMode (frame->texture, SDL_BLENDMODE_BLEND);

      SDL_SetRenderTarget (renderer, frame->texture);

      gu32 transparent_index
          = 0xFFFF; // set to out of bounds index unless exists
      if (image->frame.flags & GIF_FRAME_FLAG_TRANSPARENT)
        transparent_index = image->frame.transparent_index;

      for (size_t i = 0; i < image->width * image->height; i++)
        {
          gu8 index = image->indices[i];
          gu8 *comps;

          if (index >= palette->num_colors)
            {
              fprintf (stderr, "invalid GIF color index\n");
              return -1;
            }

          if (index == transparent_index)
            SDL_SetRenderDrawColor (renderer, 0, 0, 0, 0);
          else
            {
              comps = palette->colors + 3 * index;
              SDL_SetRenderDrawColor (renderer, comps[0], comps[1], comps[2],
                                      255);
            }

          gu32 x = i % image->width;
          gu32 y = i / image->width;
          SDL_RenderPoint (renderer, x, y);
        }
    }

  SDL_SetRenderTarget (renderer, NULL);

  return 0;
}

static void
sdl_cleanup (void)
{
  if (frames)
    {
      for (gusize i = 0; i < num_frames; i++)
        SDL_DestroyTexture (frames[i].texture);

      free (frames);
    }

  if (renderer)
    SDL_DestroyRenderer (renderer);

  if (window)
    SDL_DestroyWindow (window);
}

static void
sdl_draw_background (struct gif *gif)
{
  if (gif->flags & GIF_FLAG_GCT)
    {
      gu8 *comps = gif->gct.colors + 3 * gif->bg_index;
      SDL_SetRenderDrawColor (renderer, comps[0], comps[1], comps[2], 255);
    }
  else
    SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);

  SDL_RenderClear (renderer);
}

static void
sdl_draw_frame (struct gif *gif)
{
  for (gusize i = 0; i <= current_frame; i++)
    {
      struct animation_frame *frame = frames + i;
      gu8 disposal_method           = frame->image->frame.disposal_method;

      switch (disposal_method)
        {
        case GIF_FRAME_DISPOSE_ALL:
          sdl_draw_background (gif);
          break;
        }

      SDL_FRect dstrect = {
        .x = frame->image->x,
        .y = frame->image->y,
        .w = frame->image->width,
        .h = frame->image->height,
      };

      SDL_RenderTexture (renderer, frame->texture, NULL, &dstrect);
    }
}

int
main (int argc, const char *argv[])
{
  char *buf = NULL;
  size_t size;
  struct gif gif = { 0 };

  int test_mode        = 0;
  int retcode          = 0;
  const char *gif_path = NULL;

  if (argc >= 2)
    {
      for (int i = 1; i < argc; i++)
        {
          switch (argv[i][0])
            {
            case '\0':
              continue;
            case '-':
              {
                const char *s = argv[i] + 1;
                while (s[0])
                  {
                    if (s[0] == 't')
                      test_mode = 1;
                    ++s;
                  }
                break;
              }
            default:
              if (!gif_path)
                gif_path = argv[i];
              break;
            }
        }
    }

  if (!gif_path)
    {
      fprintf (stderr, "no GIF file provided\n");
      retcode = -1;
      goto exit;
    }

  switch (readall (gif_path, &buf, &size))
    {
    case 0:
      break;
    case READALL_NOMEM:
      fprintf (stderr, "out of memory\n");
      retcode = -1;
      goto exit;
    case READALL_NFILE:
      fprintf (stderr, "failed to open '%s'\n", gif_path);
      retcode = -1;
      goto exit;
    default:
      fprintf (stderr, "error occurred while reading '%s'\n", gif_path);
      retcode = -1;
      goto exit;
    }

  int result = gif_parse (&gif, size, buf);

  if (result != GIF_SUCCESS)
    {
      fprintf (stderr, "failed to parse gif: '%s'\n", gif_strerr (result));
      retcode = -1;
      goto exit;
    }

  if (test_mode)
    goto exit;

  SDL_Event event;
  int run = 1;
  Uint64 ticks;

  if ((retcode = sdl_init (&gif)))
    goto exit;

  if (!num_frames)
    {
      fprintf (stderr, "no frames to present\n");
      goto exit;
    }

  sdl_draw_background (&gif);
  sdl_draw_frame (&gif);
  SDL_RenderPresent (renderer);

  ticks = SDL_GetTicks ();

  while (run)
    {
      gu8 did_expose = 0;

      while (SDL_PollEvent (&event) != 0)
        {
          if (event.type == SDL_EVENT_QUIT)
            run = 0;
          else if (event.type == SDL_EVENT_WINDOW_EXPOSED)
            {
              did_expose = 1;
              sdl_draw_frame (&gif);
              SDL_RenderPresent (renderer);
            }
        }

      if (did_expose)
        continue;

      Uint64 ticks_now = SDL_GetTicks ();

      if (ticks_now - ticks
          >= frames[current_frame].image->frame.delay_time * 10)
        {
          ticks         = ticks_now;
          current_frame = (current_frame + 1) % num_frames;
        }

      sdl_draw_frame (&gif);
      SDL_RenderPresent (renderer);
    }

exit:
  sdl_cleanup ();
  gif_free (&gif);
  free (buf);

  return retcode;
}
