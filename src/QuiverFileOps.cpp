#include "QuiverFileOps.h"

#include <iostream>
#include <gio/gio.h>

using namespace std;

namespace QuiverFileOps
{

	bool Delete(QuiverFile quiverFile)
	{
		GFile* file = g_file_new_for_uri(quiverFile.GetURI());
		gboolean rval = g_file_delete(file, NULL, NULL);
		if (rval)
		{
			// delete the thumbnails now
			quiverFile.RemoveCachedThumbnail(false);
			quiverFile.RemoveCachedThumbnail(true);
		}
		g_object_unref(file);
		return rval;
	}

	bool MoveToTrash(QuiverFile quiverFile)
	{
		GFile* gfile = g_file_new_for_uri( quiverFile.GetURI() );
		return g_file_trash(gfile, NULL, NULL);
	}

}

