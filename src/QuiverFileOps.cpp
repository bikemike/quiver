#include "QuiverFileOps.h"
#include <libgnomevfs/gnome-vfs.h>
#include <iostream>

using namespace std;

namespace QuiverFileOps
{

	bool MoveToTrash(QuiverFile quiverFile)
	{
		bool bResult = false;
		//trash the file
		//locate trash folder
		GnomeVFSURI * trash_vfs_uri = NULL;

		GnomeVFSURI * near_vfs_uri = gnome_vfs_uri_new ( quiverFile.GetURI() );
		GnomeVFSResult result = gnome_vfs_find_directory (near_vfs_uri, GNOME_VFS_DIRECTORY_KIND_TRASH,&trash_vfs_uri, TRUE, TRUE, 0777);
		if (GNOME_VFS_OK != result && NULL == trash_vfs_uri)
		{
			result = gnome_vfs_find_directory (near_vfs_uri, GNOME_VFS_DIRECTORY_KIND_TRASH,&trash_vfs_uri, TRUE, FALSE, 0777);
		}
		
		if (trash_vfs_uri != NULL) 
		{
			// we have trash
			gchar * short_name = gnome_vfs_uri_extract_short_name (near_vfs_uri);
			

			
			GnomeVFSURI *trash_file_vfs_uri = gnome_vfs_uri_append_file_name (trash_vfs_uri,short_name);
			g_free(short_name);
			gnome_vfs_uri_unref(trash_vfs_uri);
			gchar *trash_uri = gnome_vfs_uri_to_string (trash_file_vfs_uri,GNOME_VFS_URI_HIDE_NONE);
			cout << "trash uri: " << trash_uri << endl;
			g_free (trash_uri);
			
			GnomeVFSResult result = gnome_vfs_move_uri(near_vfs_uri,trash_file_vfs_uri,FALSE);
			if (GNOME_VFS_OK == result)
			{
				// delete the thumbnails now
				quiverFile.RemoveCachedThumbnail(false);
				quiverFile.RemoveCachedThumbnail(true);
				
				bResult = true;
			}
			else
			{
				cout << "Error trashing file: " << quiverFile.GetURI() << " - " << gnome_vfs_result_to_string(result) << endl;
			}
		}
		else
		{
			printf("Error locating trash: %s\n",gnome_vfs_result_to_string(result));
		}
		
		gnome_vfs_uri_unref(near_vfs_uri);
		return bResult;
	}

}

