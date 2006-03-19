#include <stdio.h>
#include "ImageList.h"

int main(int argc,char* argv[])
{
	//init gnome-vfs
	if (!gnome_vfs_init ()) {
		printf ("Could not initialize GnomeVFS\n");
		return 1;
	}
	
	printf("hello world\n");
	ImageList l("/home/mike/Desktop/");
	
	printf("\n\n\n\n lets go through the list now:\n\n\n\n");
	while (l.HasNext())
	{
		printf("IMAGELIST file : %s \n",l.GetNext().c_str()); 
	}
	
	return 1;
}
