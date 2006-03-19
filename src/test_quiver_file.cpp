#include <stdio.h>
#include "QuiverFile.h"

int main(int argc,char* argv[])
{
	//init gnome-vfs
	if (!gnome_vfs_init ()) {
		printf ("Could not initialize GnomeVFS\n");
		return 1;
	}
	
	QuiverFile f("file:///home/mike/Desktop/stump_small.jpg");
	f.LoadExifData();
	QuiverFile b("file:///home/mike/Desktop/pictures/IMG_0007.JPG");
	b.LoadExifData();
	QuiverFile d("file:///home/mike/Desktop/pictures/Screenshot.png");
	d.LoadExifData();
	GdkPixbuf * nail = d.GetThumbnail();
	
	if (NULL != nail)
	{
		printf("unref'ing\n");
		g_object_unref(nail);
		printf("unref'ed\n");
	}
	
	return 0;
}
