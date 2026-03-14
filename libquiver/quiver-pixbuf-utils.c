#include <config.h>
#include <gtk/gtk.h>
#include "quiver-pixbuf-utils.h"

void pixbuf_set_alpha(GdkPixbuf *src, guchar alpha)
{
	g_return_if_fail (GDK_IS_PIXBUF (src));
	  
	int i, j, t;
	int width, height, has_alpha, src_rowstride, bytes_per_pixel,n_channels;
	guchar *src_line;
	guchar *src_pixel;

	n_channels = gdk_pixbuf_get_n_channels (src);

	g_assert (gdk_pixbuf_get_colorspace (src) == GDK_COLORSPACE_RGB);
	g_assert (gdk_pixbuf_get_bits_per_sample (src) == 8);
	g_assert (gdk_pixbuf_get_has_alpha (src));
	g_assert (n_channels == 4);

	has_alpha = gdk_pixbuf_get_has_alpha (src);
	if (!has_alpha)
	{
		GdkPixbuf *new_src = gdk_pixbuf_add_alpha(src,FALSE,0,0,0);
		g_object_unref(src);
		src = new_src;
	}

	width = gdk_pixbuf_get_width (src);
	height = gdk_pixbuf_get_height (src);
	src_rowstride = gdk_pixbuf_get_rowstride (src);

	src_line = gdk_pixbuf_get_pixels (src);

	g_assert(NULL != src_line);
	guchar new_alpha;

	for (i = 0 ; i < height ; i++)
	{
		src_pixel = src_line;
		src_line += src_rowstride;
		for (j = 0 ; j < width ; j++)
		{
			new_alpha = (guint)(src_pixel[3]/255. * alpha);

			src_pixel[3] = new_alpha;

			src_pixel += n_channels;
		}
	}
}

void pixbuf_set_grayscale(GdkPixbuf *src)
{
	/* NOTE that src and dest MAY be the same pixbuf! */

	g_return_if_fail (GDK_IS_PIXBUF (src));
	  
	int i, j, t;
	int width, height, has_alpha, src_rowstride, bytes_per_pixel;
	guchar *src_line;
	guchar *src_pixel;

	has_alpha = gdk_pixbuf_get_has_alpha (src);
	bytes_per_pixel = has_alpha ? 4 : 3;
	width = gdk_pixbuf_get_width (src);
	height = gdk_pixbuf_get_height (src);
	src_rowstride = gdk_pixbuf_get_rowstride (src);

	src_line = gdk_pixbuf_get_pixels (src);

	guchar gray_color;

	for (i = 0 ; i < height ; i++)
	{
		src_pixel = src_line;
		src_line += src_rowstride;
		for (j = 0 ; j < width ; j++)
		{
			gray_color = (guint) (.3*src_pixel[0] + .59 *src_pixel[1] + .11 * src_pixel[2]);
			src_pixel[0] = gray_color;
			src_pixel[1] = gray_color;
			src_pixel[2] = gray_color; 

			src_pixel += bytes_per_pixel;
		}
	}
}

void pixbuf_brighten(const GdkPixbuf *src, GdkPixbuf *dest, gint amount)
{
	/* NOTE that src and dest MAY be the same pixbuf! */

	g_return_if_fail (GDK_IS_PIXBUF (src));
	g_return_if_fail (GDK_IS_PIXBUF (dest));
	g_return_if_fail (gdk_pixbuf_get_height (src) == gdk_pixbuf_get_height (dest));
	g_return_if_fail (gdk_pixbuf_get_width (src) == gdk_pixbuf_get_width (dest));
	g_return_if_fail (gdk_pixbuf_get_has_alpha (src) == gdk_pixbuf_get_has_alpha (dest));
	g_return_if_fail (gdk_pixbuf_get_colorspace (src) == gdk_pixbuf_get_colorspace (dest));
	  
	int i, j, t;
	int width, height, has_alpha, src_rowstride, dest_rowstride, bytes_per_pixel;
	guchar *src_line = NULL;
	guchar *dest_line = NULL;
	guchar *src_pixel = NULL;
	guchar *dest_pixel = NULL;

	has_alpha = gdk_pixbuf_get_has_alpha (src);
	bytes_per_pixel = has_alpha ? 4 : 3;
	width = gdk_pixbuf_get_width (src);
	height = gdk_pixbuf_get_height (src);
	src_rowstride = gdk_pixbuf_get_rowstride (src);
	dest_rowstride = gdk_pixbuf_get_rowstride (dest);

	src_line = gdk_pixbuf_get_pixels (src);
	dest_line = gdk_pixbuf_get_pixels (dest);

	for (i = 0 ; i < height ; i++)
	{
		src_pixel = src_line;
		src_line += src_rowstride;
		dest_pixel = dest_line;
		dest_line += dest_rowstride;
#define CLAMP_UCHAR(v) (t = (v), CLAMP (t, 0, 255))
		for (j = 0 ; j < width ; j++)
		{
			dest_pixel[0] = CLAMP_UCHAR (src_pixel[0]+amount);
			dest_pixel[1] = CLAMP_UCHAR (src_pixel[1]+amount);
			dest_pixel[2] = CLAMP_UCHAR (src_pixel[2]+amount);

			if (has_alpha)
				dest_pixel[3] = src_pixel[3];

			src_pixel += bytes_per_pixel;
			dest_pixel += bytes_per_pixel;
		}
	}
}

void quiver_rect_get_bound_size(guint bound_width,guint bound_height,guint *width,guint *height,gboolean fill_if_smaller)
{
	guint new_width;
	guint w = *width;
	guint h = *height;

	if (!fill_if_smaller && 
			(w < bound_width && h < bound_height) )
	{
		return;
	}

	if (w != bound_width || h != bound_height)
	{
		gdouble ratio = (double)w/h;
		guint new_height;

		new_height = (guint)(bound_width/ratio +.5);
		if (new_height < bound_height)
		{
			*width = bound_width;
			*height = new_height;
		}
		else
		{
			new_width = (guint)(bound_height *ratio + .5); 
			*width = MIN(new_width, bound_width);
			*height = bound_height;
		}
	}
}	

