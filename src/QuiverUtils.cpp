#include <config.h>
#include "QuiverUtils.h"


namespace QuiverUtils
{
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation)
	{
		//printf("orientation is: %d\n",orientation);

		/*
		  1        2       3      4         5            6           7          8
		888888  888888      88  88      8888888888  88                  88  8888888888
		88          88      88  88      88  88      88  88          88  88      88  88
		8888      8888    8888  8888    88          8888888888  8888888888          88
		88          88      88  88
		88          88  888888  888888
		1 = no rotation
		2 = flip h
		3 = rotate 180
		4 = flip v
		5 = flip v, rotate 90
		6 = rotate 90
		7 = flip v, rotate 270
		8 = rotae 270 


		*/
		//get rotaiton
		
		GdkPixbuf * modified = NULL;

		switch (orientation)
		{
			case 1:
				//1 = no rotation
				break;
			case 2:
				//2 = flip h
				modified = gdk_pixbuf_flip(pixbuf,TRUE);
				break;
			case 3:
				//3 = rotate 180
				modified = gdk_pixbuf_rotate_simple(pixbuf,(GdkPixbufRotation)180);
				break;
			case 4:
				//4 = flip v
				modified = gdk_pixbuf_flip(pixbuf,FALSE);
				break;
			case 5:
				//5 = flip v, rotate 90
				{
					GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
					modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_CLOCKWISE);
					g_object_unref(tmp);
				}
				break;
			case 6:
				modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_CLOCKWISE);
				break;
			case 7:
				//7 = flip v, rotate 270
				{
					GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
					modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
					g_object_unref(tmp);
				}
				break;
			case 8:
				//8 = rotae 270 
				modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
			default:
				break;
		}
		return modified;
	}

}

