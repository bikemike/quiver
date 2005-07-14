#ifndef SP_ART_RGBA_SVP_H
#define SP_ART_RGBA_SVP_H

#include <libart_lgpl/art_misc.h>
#include <libart_lgpl/art_alphagamma.h>
#include <libart_lgpl/art_affine.h>
#include <libart_lgpl/art_svp.h>
#include <libart_lgpl/art_uta.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void
art_rgba_rgba_composite (art_u8 *dst, const art_u8 *src, int n);

void
art_rgba_fill_run (art_u8 *buf, art_u8 r, art_u8 g, art_u8 b, int n);

void
art_rgba_fill_run_with_alpha (art_u8 *buf, art_u8 r, art_u8 g, art_u8 b, art_u8 a, int n);

void
art_rgba_run_alpha (art_u8 *buf, art_u8 r, art_u8 g, art_u8 b, int alpha, int n);



void
gnome_print_art_rgba_svp_alpha (const ArtSVP *svp,
				int x0, int y0, int x1, int y1,
				art_u32 rgba,
				art_u8 *buf, int rowstride,
				ArtAlphaGamma *alphagamma);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SP_ART_RGBA_SVP_H */
