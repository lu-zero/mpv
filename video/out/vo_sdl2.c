/*
 * video output driver for SDL2
 *
 * by divVerent <divVerent@xonotic.org>
 *
 * Some functions/codes/ideas are from x11 and aalib vo
 *
 * TODO: support draw_alpha?
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#include <SDL.h>

#include "config.h"
#include "vo.h"
#include "sub/sub.h"
#include "video/mp_image.h"
#include "video/vfcap.h"
#include "aspect.h"
#include "bitmap_packer.h"

#include "core/input/keycodes.h"
#include "core/input/input.h"
#include "core/mp_msg.h"
#include "core/mp_fifo.h"
#include "core/options.h"

struct formatmap_entry {
    Uint32 sdl;
    unsigned int mpv;
    bool is_rgba;
};
const struct formatmap_entry formats[] = {
    {SDL_PIXELFORMAT_YV12, IMGFMT_YV12, 0},
    {SDL_PIXELFORMAT_IYUV, IMGFMT_IYUV, 0},
    {SDL_PIXELFORMAT_YUY2, IMGFMT_YUY2, 0},
    {SDL_PIXELFORMAT_UYVY, IMGFMT_UYVY, 0},
    {SDL_PIXELFORMAT_YVYU, IMGFMT_YVYU, 0},
#if BYTE_ORDER == BIG_ENDIAN
    {SDL_PIXELFORMAT_RGBX8888, IMGFMT_RGBA, 1},
    {SDL_PIXELFORMAT_BGRX8888, IMGFMT_BGRA, 1},
    {SDL_PIXELFORMAT_ARGB8888, IMGFMT_ARGB, 1},
    {SDL_PIXELFORMAT_RGBA8888, IMGFMT_RGBA, 1},
    {SDL_PIXELFORMAT_ABGR8888, IMGFMT_ABGR, 1},
    {SDL_PIXELFORMAT_BGRA8888, IMGFMT_BGRA, 1},
    {SDL_PIXELFORMAT_RGB24, IMGFMT_RGB24, 0},
    {SDL_PIXELFORMAT_BGR24, IMGFMT_BGR24, 0},
    {SDL_PIXELFORMAT_RGB888, IMGFMT_RGB24, 0},
    {SDL_PIXELFORMAT_BGR888, IMGFMT_BGR24, 0},
    {SDL_PIXELFORMAT_RGB565, IMGFMT_RGB16, 0},
    {SDL_PIXELFORMAT_BGR565, IMGFMT_BGR16, 0},
    {SDL_PIXELFORMAT_RGB555, IMGFMT_RGB15, 0},
    {SDL_PIXELFORMAT_BGR555, IMGFMT_BGR15, 0},
    {SDL_PIXELFORMAT_RGB444, IMGFMT_RGB12, 0},
    {SDL_PIXELFORMAT_RGB332, IMGFMT_RGB8, 0}
#else
    {SDL_PIXELFORMAT_RGBX8888, IMGFMT_ABGR, 1},
    {SDL_PIXELFORMAT_BGRX8888, IMGFMT_ARGB, 1},
    {SDL_PIXELFORMAT_ARGB8888, IMGFMT_BGRA, 1},
    {SDL_PIXELFORMAT_RGBA8888, IMGFMT_ABGR, 1},
    {SDL_PIXELFORMAT_ABGR8888, IMGFMT_RGBA, 1},
    {SDL_PIXELFORMAT_BGRA8888, IMGFMT_ARGB, 1},
    {SDL_PIXELFORMAT_RGB24, IMGFMT_RGB24, 0},
    {SDL_PIXELFORMAT_BGR24, IMGFMT_BGR24, 0},
    {SDL_PIXELFORMAT_RGB888, IMGFMT_BGR24, 0},
    {SDL_PIXELFORMAT_BGR888, IMGFMT_RGB24, 0},
    {SDL_PIXELFORMAT_RGB565, IMGFMT_BGR16, 0},
    {SDL_PIXELFORMAT_BGR565, IMGFMT_RGB16, 0},
    {SDL_PIXELFORMAT_RGB555, IMGFMT_BGR15, 0},
    {SDL_PIXELFORMAT_BGR555, IMGFMT_RGB15, 0},
    {SDL_PIXELFORMAT_RGB444, IMGFMT_BGR12, 0},
    {SDL_PIXELFORMAT_RGB332, IMGFMT_RGB8, 0}
#endif
};

struct keymap_entry {
    SDL_Keycode sdl;
    int mpv;
};
const struct keymap_entry keys[] = {
    {SDLK_RETURN, KEY_ENTER},
    {SDLK_ESCAPE, KEY_ESC},
    {SDLK_BACKSPACE, KEY_BACKSPACE},
    {SDLK_TAB, KEY_TAB},
    {SDLK_PRINTSCREEN, KEY_PRINT},
    {SDLK_PAUSE, KEY_PAUSE},
    {SDLK_INSERT, KEY_INSERT},
    {SDLK_HOME, KEY_HOME},
    {SDLK_PAGEUP, KEY_PAGE_UP},
    {SDLK_DELETE, KEY_DELETE},
    {SDLK_END, KEY_END},
    {SDLK_PAGEDOWN, KEY_PAGE_DOWN},
    {SDLK_RIGHT, KEY_RIGHT},
    {SDLK_LEFT, KEY_LEFT},
    {SDLK_DOWN, KEY_DOWN},
    {SDLK_UP, KEY_UP},
    {SDLK_KP_ENTER, KEY_KPENTER},
    {SDLK_KP_1, KEY_KP1},
    {SDLK_KP_2, KEY_KP2},
    {SDLK_KP_3, KEY_KP3},
    {SDLK_KP_4, KEY_KP4},
    {SDLK_KP_5, KEY_KP5},
    {SDLK_KP_6, KEY_KP6},
    {SDLK_KP_7, KEY_KP7},
    {SDLK_KP_8, KEY_KP8},
    {SDLK_KP_9, KEY_KP9},
    {SDLK_KP_0, KEY_KP0},
    {SDLK_KP_PERIOD, KEY_KPDEC},
    {SDLK_POWER, KEY_POWER},
    {SDLK_MENU, KEY_MENU},
    {SDLK_STOP, KEY_STOP},
    {SDLK_MUTE, KEY_MUTE},
    {SDLK_VOLUMEUP, KEY_VOLUME_UP},
    {SDLK_VOLUMEDOWN, KEY_VOLUME_DOWN},
    {SDLK_KP_COMMA, KEY_KPDEC},
    {SDLK_AUDIONEXT, KEY_NEXT},
    {SDLK_AUDIOPREV, KEY_PREV},
    {SDLK_AUDIOSTOP, KEY_STOP},
    {SDLK_AUDIOPLAY, KEY_PLAY},
    {SDLK_AUDIOMUTE, KEY_MUTE},
    {SDLK_F1, KEY_F + 1},
    {SDLK_F2, KEY_F + 2},
    {SDLK_F3, KEY_F + 3},
    {SDLK_F4, KEY_F + 4},
    {SDLK_F5, KEY_F + 5},
    {SDLK_F6, KEY_F + 6},
    {SDLK_F7, KEY_F + 7},
    {SDLK_F8, KEY_F + 8},
    {SDLK_F9, KEY_F + 9},
    {SDLK_F10, KEY_F + 10},
    {SDLK_F11, KEY_F + 11},
    {SDLK_F12, KEY_F + 12},
    {SDLK_F13, KEY_F + 13},
    {SDLK_F14, KEY_F + 14},
    {SDLK_F15, KEY_F + 15},
    {SDLK_F16, KEY_F + 16},
    {SDLK_F17, KEY_F + 17},
    {SDLK_F18, KEY_F + 18},
    {SDLK_F19, KEY_F + 19},
    {SDLK_F20, KEY_F + 20},
    {SDLK_F21, KEY_F + 21},
    {SDLK_F22, KEY_F + 22},
    {SDLK_F23, KEY_F + 23},
    {SDLK_F24, KEY_F + 24}
};

struct priv {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_RendererInfo renderer_info;
    SDL_Texture *tex;
    mp_image_t texmpi;
    mp_image_t *ssmpi;
    struct mp_rect src_rect;
    struct mp_rect dst_rect;
    struct mp_osd_res osd_res;
    int int_pause;
    struct formatmap_entry osd_format;
    struct osd_bitmap_surface {
        int bitmap_id;
        int bitmap_pos_id;
        SDL_Texture *tex;
        struct osd_target {
            SDL_Rect source;
            SDL_Rect dest;
            Uint8 color[3];
            Uint8 alpha;
        } *targets;
        int targets_size;
        int render_count;
        struct bitmap_packer *packer;
    } osd_surfaces[MAX_OSD_PARTS];

    // options
    const char *opt_driver;
    bool opt_shaders;
    int opt_scale;
};

static void resize(struct vo *vo, int w, int h)
{
    struct priv *vc = vo->priv;
    vo->dwidth = w;
    vo->dheight = h;
    vo_get_src_dst_rects(vo, &vc->src_rect, &vc->dst_rect,
                         &vc->osd_res);
    SDL_RenderSetLogicalSize(vc->renderer, w, h);
    vo->want_redraw = true;
}

static void force_resize(struct vo *vo)
{
    struct priv *vc = vo->priv;
    int w, h;
    SDL_GetWindowSize(vc->window, &w, &h);
    resize(vo, w, h);
}

static void check_resize(struct vo *vo)
{
    struct priv *vc = vo->priv;
    int w, h;
    SDL_GetWindowSize(vc->window, &w, &h);
    if (vo->dwidth != w || vo->dheight != h)
        resize(vo, w, h);
}

static int config(struct vo *vo, uint32_t width, uint32_t height,
                  uint32_t d_width, uint32_t d_height, uint32_t flags,
                  uint32_t format)
{
    struct priv *vc = vo->priv;
    SDL_SetWindowSize(vc->window, d_width, d_height);
    if (vc->tex)
        SDL_DestroyTexture(vc->tex);
    Uint32 texfmt = SDL_PIXELFORMAT_UNKNOWN;
    int i, j;
    for (i = 0; i < vc->renderer_info.num_texture_formats; ++i)
        for (j = 0; j < sizeof(formats) / sizeof(formats[0]); ++j)
            if (vc->renderer_info.texture_formats[i] == formats[j].sdl)
                if (format == formats[j].mpv)
                    texfmt = formats[j].sdl;
    if (texfmt == SDL_PIXELFORMAT_UNKNOWN) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] Invalid pixel format\n");
        return -1;
    }

    vc->tex = SDL_CreateTexture(vc->renderer, texfmt,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!vc->tex) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] Could not create a texture\n");
        return -1;
    }

    mp_image_t *texmpi = &vc->texmpi;
    texmpi->width = texmpi->w = width;
    texmpi->height = texmpi->h = height;
    mp_image_setfmt(texmpi, format);
    switch (texmpi->num_planes) {
    case 1:
    case 3:
        break;
    default:
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] Invalid plane count\n");
        SDL_DestroyTexture(vc->tex);
        vc->tex = NULL;
        return -1;
    }

    vc->ssmpi = alloc_mpi(width, height, format);

    resize(vo, d_width, d_height);

    if (flags & VOFLAG_FULLSCREEN) {
        if (SDL_SetWindowFullscreen(vc->window, 1))
            mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_SetWindowFullscreen failed\n");
        else
            vo_fs = 1;
    }

    SDL_ShowWindow(vc->window);

    check_resize(vo);

    return 0;
}

static void toggle_fullscreen(struct vo *vo)
{
    struct priv *vc = vo->priv;
    vo_fs = !vo_fs;
    if (SDL_SetWindowFullscreen(vc->window, vo_fs)) {
        vo_fs = !vo_fs;
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_SetWindowFullscreen failed\n");
    }
    force_resize(vo);
}

static void flip_page(struct vo *vo)
{
    struct priv *vc = vo->priv;
    SDL_RenderPresent(vc->renderer);
}

static void check_events(struct vo *vo)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_WINDOWEVENT:
            switch (ev.window.event) {
            case SDL_WINDOWEVENT_EXPOSED:
                vo->want_redraw = true;
                break;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                check_resize(vo);
                break;
            case SDL_WINDOWEVENT_CLOSE:
                mplayer_put_key(vo->key_fifo, KEY_CLOSE_WIN);
                break;
            }
            break;
        case SDL_KEYDOWN:
        {
            int keycode = 0;
            int i;
            if (ev.key.keysym.sym >= ' ' && ev.key.keysym.sym <= '~')
                keycode = ev.key.keysym.sym;
            else {
                for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
                    if (keys[i].sdl == ev.key.keysym.sym) {
                        keycode = keys[i].mpv;
                        break;
                    }
            }
            if (keycode) {
                if (ev.key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT))
                    keycode |= KEY_MODIFIER_SHIFT;
                if (ev.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                    keycode |= KEY_MODIFIER_CTRL;
                if (ev.key.keysym.mod & (KMOD_LALT | KMOD_RALT))
                    keycode |= KEY_MODIFIER_ALT;
                if (ev.key.keysym.mod & (KMOD_LGUI | KMOD_RGUI))
                    keycode |= KEY_MODIFIER_META;
                mplayer_put_key(vo->key_fifo, keycode);
            }
        }
        break;
        case SDL_MOUSEMOTION:
            // TODO opts->cursor_autohide_delay
            vo_mouse_movement(vo, ev.motion.x, ev.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
            // TODO opts->cursor_autohide_delay
            mplayer_put_key(vo->key_fifo,
                            (MOUSE_BTN0 + ev.button.button - 1) | MP_KEY_DOWN);
            break;
        case SDL_MOUSEBUTTONUP:
            // TODO opts->cursor_autohide_delay
            mplayer_put_key(vo->key_fifo,
                            (MOUSE_BTN0 + ev.button.button - 1));
            break;
        case SDL_MOUSEWHEEL:
            break;
        }
    }
}

static void uninit(struct vo *vo)
{
    struct priv *vc = vo->priv;
    free_mp_image(vc->ssmpi);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    talloc_free(vc);
}

static struct bitmap_packer *make_packer(struct vo *vo)
{
    struct bitmap_packer *packer = talloc_zero(vo, struct bitmap_packer);
    packer->w_max = 4096; // FIXME is there a maximum size?
    packer->h_max = 4096; // FIXME is there a maximum size?
    return packer;
}

static void generate_osd_part(struct vo *vo, struct sub_bitmaps *imgs)
{
    struct priv *vc = vo->priv;
    struct osd_bitmap_surface *sfc = &vc->osd_surfaces[imgs->render_index];

    if (imgs->bitmap_pos_id == sfc->bitmap_pos_id)
        return;  // Nothing changed and we still have the old data

    sfc->render_count = 0;

    if (imgs->format == SUBBITMAP_EMPTY || imgs->num_parts == 0)
        return;

    unsigned char *surfpixels = NULL;
    int surfpitch = 0;

    if (imgs->bitmap_id != sfc->bitmap_id) {
        if (sfc->tex)
            SDL_DestroyTexture(sfc->tex);
        sfc->tex = NULL;

        if (!sfc->packer)
            sfc->packer = make_packer(vo);
        sfc->packer->padding = 0;
        int r = packer_pack_from_subbitmaps(sfc->packer, imgs);
        if (r < 0) {
            mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] OSD bitmaps do not fit on "
                   "a surface with the maximum supported size\n");
            return;
        } else {
            mp_msg(MSGT_VO, MSGL_V, "[sdl2] Allocating a %dx%d surface for "
                   "OSD bitmaps.\n", sfc->packer->w, sfc->packer->h);
            surfpixels = talloc_size(vc, sfc->packer->w * sfc->packer->h * 4);
            surfpitch = sfc->packer->w * 4;
            if (!surfpixels) {
                mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] Could not create surface\n");
                return;
            }
        }

        if (sfc->packer->count > sfc->targets_size) {
            talloc_free(sfc->targets);
            sfc->targets_size = sfc->packer->count;
            sfc->targets = talloc_size(vc, sfc->targets_size *
                                       sizeof(*sfc->targets));
        }
    }

    for (int i = 0; i < sfc->packer->count; i++) {
        struct sub_bitmap *b = &imgs->parts[i];
        struct osd_target *target = sfc->targets + sfc->render_count;
        int x = sfc->packer->result[i].x;
        int y = sfc->packer->result[i].y;
        target->source = (SDL_Rect){
            x, y, b->w, b->h
        };
        target->dest = (SDL_Rect){
            b->x, b->y, b->dw, b->dh
        };
        switch (imgs->format) {
        case SUBBITMAP_LIBASS:
            target->alpha = 255 - ((b->libass.color >> 0) & 0xff);
            target->color[0] = ((b->libass.color >>  8) & 0xff);
            target->color[1] = ((b->libass.color >> 16) & 0xff);
            target->color[2] = ((b->libass.color >> 24) & 0xff);
            if (surfpixels) {
                // damn SDL has no support for 8bit gray...
                // idea: could instead hand craft a SDL_Surface with palette
                size_t n = b->h * b->stride, i;
                uint32_t *bmp = talloc_size(vc, n * 4);
                for (i = 0; i < n; ++i)
                    bmp[i] = 0x00FFFFFF |
                             ((((uint8_t *) b->bitmap)[i]) << 24);
                SDL_ConvertPixels(
                    b->w, b->h, SDL_PIXELFORMAT_ARGB8888,
                        bmp, b->stride * 4,
                    vc->osd_format.sdl,
                        surfpixels + x * 4 + y * surfpitch, surfpitch);
                talloc_free(bmp);
            }
            break;
        case SUBBITMAP_RGBA:
            target->alpha = 255;
            target->color[0] = 255;
            target->color[1] = 255;
            target->color[2] = 255;
            if (surfpixels) {
                SDL_ConvertPixels(
                    b->w, b->h, SDL_PIXELFORMAT_ARGB8888,
                        b->bitmap, b->stride,
                    vc->osd_format.sdl,
                        surfpixels + x * 4 + y * surfpitch, surfpitch);
            }
            break;
        }
        sfc->render_count++;
    }

    if (surfpixels) {
        sfc->tex = SDL_CreateTexture(vc->renderer, vc->osd_format.sdl,
                                     SDL_TEXTUREACCESS_STATIC, sfc->packer->w,
                                     sfc->packer->h);
        if (!surfpixels) {
            mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] Could not create texture\n");
            return;
        }
        SDL_UpdateTexture(sfc->tex, NULL, surfpixels, surfpitch);
        talloc_free(surfpixels);
        SDL_SetTextureBlendMode(sfc->tex, SDL_BLENDMODE_BLEND);
    }

    sfc->bitmap_id = imgs->bitmap_id;
    sfc->bitmap_pos_id = imgs->bitmap_pos_id;
}

static void draw_osd_part(struct vo *vo, int index)
{
    struct priv *vc = vo->priv;
    struct osd_bitmap_surface *sfc = &vc->osd_surfaces[index];
    int i;

    for (i = 0; i < sfc->render_count; i++) {
        struct osd_target *target = sfc->targets + i;
        SDL_SetTextureAlphaMod(sfc->tex, sfc->targets[i].alpha);
        SDL_SetTextureColorMod(sfc->tex, sfc->targets[i].color[0],
                sfc->targets[i].color[1],
                sfc->targets[i].color[2]);
        SDL_RenderCopy(vc->renderer, sfc->tex, &target->source, &target->dest);
    }
}

static void draw_osd_cb(void *ctx, struct sub_bitmaps *imgs)
{
    struct vo *vo = ctx;
    generate_osd_part(vo, imgs);
    draw_osd_part(vo, imgs->render_index);
}

static void draw_osd(struct vo *vo, struct osd_state *osd)
{
    struct priv *vc = vo->priv;

    static const bool formats[SUBBITMAP_COUNT] = {
        [SUBBITMAP_LIBASS] = true,
        [SUBBITMAP_RGBA] = true,
    };

    osd_draw(osd, vc->osd_res, osd->vo_pts, 0, formats, draw_osd_cb, vo);
}

static bool MP_SDL_IsGoodRenderer(int n, const char *driver_name_wanted)
{
    SDL_RendererInfo ri;
    if (SDL_GetRenderDriverInfo(n, &ri))
        return false;

    if (driver_name_wanted && driver_name_wanted[0])
        if (strcmp(driver_name_wanted, ri.name))
            return false;

    int i, j;
    for (i = 0; i < ri.num_texture_formats; ++i)
        for (j = 0; j < sizeof(formats) / sizeof(formats[0]); ++j)
            if (ri.texture_formats[i] == formats[j].sdl && formats[j].is_rgba)
                return true;
    return false;
}

static int preinit(struct vo *vo, const char *arg)
{
    struct priv *vc = vo->priv;

    int i, j;

    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] already initialized\n");
        return -1;
    }
    if (SDL_Init(SDL_INIT_VIDEO)) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_Init failed\n");
        return -1;
    }

    // predefine SDL defaults (SDL env vars shall override)
    SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "1",
            SDL_HINT_DEFAULT);

    // predefine MPV options (SDL env vars shall be overridden)
    if (vc->opt_driver && *vc->opt_driver)
        SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, vc->opt_driver,
                SDL_HINT_OVERRIDE);

    if (vc->opt_shaders)
        SDL_SetHintWithPriority(SDL_HINT_RENDER_OPENGL_SHADERS, "1",
                SDL_HINT_OVERRIDE);
    else
        SDL_SetHintWithPriority(SDL_HINT_RENDER_OPENGL_SHADERS, "0",
                SDL_HINT_OVERRIDE);

    if (vc->opt_scale == 0)
        SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "0",
                SDL_HINT_OVERRIDE);
    else if (vc->opt_scale == 1)
        SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "1",
                SDL_HINT_OVERRIDE);
    else if (vc->opt_scale == 2)
        SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "2",
                SDL_HINT_OVERRIDE);

    if (vo_vsync)
        SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, "1",
                SDL_HINT_OVERRIDE);
    else
        SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, "0",
                SDL_HINT_OVERRIDE);

    vc->window = SDL_CreateWindow("MPV",
                                  SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED,
                                  640, 480,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!vc->window) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_CreateWindow failedd\n");
        return -1;
    }

    int n = SDL_GetNumRenderDrivers();
    for (i = 0; i < n; ++i)
        if (MP_SDL_IsGoodRenderer(i, SDL_GetHint(SDL_HINT_RENDER_DRIVER)))
            break;
    if (i >= n)
        for (i = 0; i < n; ++i)
            if (MP_SDL_IsGoodRenderer(i, NULL))
                break;
    if (i >= n) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] No supported renderer\n");
        SDL_DestroyWindow(vc->window);
        vc->window = NULL;
        return -1;
    }

    vc->renderer = SDL_CreateRenderer(vc->window, i, 0);
    if (!vc->renderer) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_CreateRenderer failed\n");
        SDL_DestroyWindow(vc->window);
        vc->window = NULL;
        return -1;
    }

    if (SDL_GetRendererInfo(vc->renderer, &vc->renderer_info)) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_GetRendererInfo failed\n");
        SDL_DestroyWindow(vc->window);
        vc->window = NULL;
        return 0;
    }

    mp_msg(MSGT_VO, MSGL_INFO, "[sdl2] Using %s\n", vc->renderer_info.name);

    for (i = 0; i < vc->renderer_info.num_texture_formats; ++i)
        for (j = 0; j < sizeof(formats) / sizeof(formats[0]); ++j)
            if (vc->renderer_info.texture_formats[i] == formats[j].sdl)
                if (formats[j].is_rgba)
                    vc->osd_format = formats[j];

    // we don't have proper event handling
    vo->wakeup_period = 0.02;

    return 0;
}

static int query_format(struct vo *vo, uint32_t format)
{
    struct priv *vc = vo->priv;
    int i, j;
    int cap = VFCAP_CSP_SUPPORTED | VFCAP_FLIP | VFCAP_ACCEPT_STRIDE |
              VFCAP_OSD;
    for (i = 0; i < vc->renderer_info.num_texture_formats; ++i)
        for (j = 0; j < sizeof(formats) / sizeof(formats[0]); ++j)
            if (vc->renderer_info.texture_formats[i] == formats[j].sdl)
                if (format == formats[j].mpv)
                    return cap;
    return 0;
}

static void draw_image(struct vo *vo, mp_image_t *mpi, double pts)
{
    struct priv *vc = vo->priv;

    void *pixels;
    int pitch;

    // typically this runs in parallel with the following copy_mpi call
    SDL_RenderClear(vc->renderer);

    if (mpi) {
        if (SDL_LockTexture(vc->tex, NULL, &pixels, &pitch)) {
            mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_LockTexture failed\n");
            return;
        }

        mp_image_t *texmpi = &vc->texmpi;
        texmpi->planes[0] = pixels;
        texmpi->stride[0] = pitch;
        if (texmpi->num_planes == 3) {
            if (texmpi->imgfmt == IMGFMT_YV12) {
                texmpi->planes[2] =
                    ((Uint8 *) texmpi->planes[0] + texmpi->h * pitch);
                texmpi->stride[2] = pitch / 2;
                texmpi->planes[1] =
                    ((Uint8 *) texmpi->planes[2] + (texmpi->h * pitch) / 4);
                texmpi->stride[1] = pitch / 2;
            } else {
                texmpi->planes[1] =
                    ((Uint8 *) texmpi->planes[0] + texmpi->h * pitch);
                texmpi->stride[1] = pitch / 2;
                texmpi->planes[2] =
                    ((Uint8 *) texmpi->planes[1] + (texmpi->h * pitch) / 4);
                texmpi->stride[2] = pitch / 2;
            }
        }
        copy_mpi(texmpi, mpi);

        SDL_UnlockTexture(vc->tex);
    }

    SDL_Rect src, dst;
    src.x = vc->src_rect.x0;
    src.y = vc->src_rect.y0;
    src.w = vc->src_rect.x1 - vc->src_rect.x0;
    src.h = vc->src_rect.y1 - vc->src_rect.y0;
    dst.x = vc->dst_rect.x0;
    dst.y = vc->dst_rect.y0;
    dst.w = vc->dst_rect.x1 - vc->dst_rect.x0;
    dst.h = vc->dst_rect.y1 - vc->dst_rect.y0;

    // typically this runs in parallel with the following copy_mpi call
    SDL_RenderCopy(vc->renderer, vc->tex, &src, &dst);
    if (mpi)
        copy_mpi(vc->ssmpi, mpi);
}

static void update_screeninfo(struct vo *vo)
{
    struct priv *vc = vo->priv;
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplay(vc->window), &mode)) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_GetCurrentDisplayMode failed\n");
        return;
    }
    struct MPOpts *opts = vo->opts;
    opts->vo_screenwidth = mode.w;
    opts->vo_screenheight = mode.h;
    aspect_save_screenres(vo, opts->vo_screenwidth, opts->vo_screenheight);
}

static struct mp_image *get_screenshot(struct vo *vo)
{
    struct priv *vc = vo->priv;
    mp_image_t *image = alloc_mpi(vc->ssmpi->width, vc->ssmpi->height,
            vc->ssmpi->imgfmt);
    copy_mpi(image, vc->ssmpi);
    return image;
}

static struct mp_image *get_window_screenshot(struct vo *vo)
{
    struct priv *vc = vo->priv;
    mp_image_t *image = alloc_mpi(vo->dwidth, vo->dheight, vc->osd_format.mpv);
    if (SDL_RenderReadPixels(vc->renderer, NULL, vc->osd_format.sdl,
                image->planes[0], image->stride[0])) {
        mp_msg(MSGT_VO, MSGL_ERR, "[sdl2] SDL_RenderReadPixels failed\n");
        free_mp_image(image);
        return NULL;
    }
    return image;
}

static int control(struct vo *vo, uint32_t request, void *data)
{
    struct priv *vc = vo->priv;
    switch (request) {
    case VOCTRL_QUERY_FORMAT:
        return query_format(vo, *((uint32_t *)data));
    case VOCTRL_DRAW_IMAGE:
        draw_image(vo, (mp_image_t *)data, vo->next_pts);
        return 0;
    case VOCTRL_FULLSCREEN:
        toggle_fullscreen(vo);
        return 1;
    case VOCTRL_PAUSE:
        return vc->int_pause = 1;
    case VOCTRL_RESUME:
        return vc->int_pause = 0;
    case VOCTRL_REDRAW_FRAME:
        draw_image(vo, NULL, MP_NOPTS_VALUE);
        return 1;
    case VOCTRL_UPDATE_SCREENINFO:
        update_screeninfo(vo);
        return 1;
    case VOCTRL_GET_PANSCAN:
        return VO_TRUE;
    case VOCTRL_SET_PANSCAN:
        force_resize(vo);
        return VO_TRUE;
    case VOCTRL_SCREENSHOT: {
            struct voctrl_screenshot_args *args = data;
            if (args->full_window)
                args->out_image = get_window_screenshot(vo);
            else
                args->out_image = get_screenshot(vo);
            return true;
        }
    }
    return VO_NOTIMPL;
}

#undef OPT_BASE_STRUCT
#define OPT_BASE_STRUCT struct priv

const struct vo_driver video_out_sdl2 = {
    .is_new = true,
    .info = &(const vo_info_t) {
        "SDL2",
        "sdl2",
        "Rudolf Polzer <divVerent@xonotic.org>",
        ""
    },
    .priv_size = sizeof(struct priv),
    .priv_defaults = &(const struct priv) {
        .opt_driver = "",
        .opt_shaders = true,
        .opt_scale = 1,
    },
    .options = (const struct m_option[]) {
        OPT_STRING("driver", opt_driver, 0),
        OPT_MAKE_FLAGS("shaders", opt_shaders, 0),
        OPT_INTRANGE("scale", opt_scale, 0, -1, 2),
        {0},
    },
    .preinit = preinit,
    .config = config,
    .control = control,
    .uninit = uninit,
    .check_events = check_events,
    .draw_osd = draw_osd,
    .flip_page = flip_page,
};
