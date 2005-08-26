#include <config.h>
#include <glib.h>
#include <libgnomeui/gnome-thumbnail.h>
#include "QuiverFile.h"
#include "Timer.h"
#include <exif-utils.h>
#include "Viewer.h"


// =================================================================================================
// Implementation
// =================================================================================================

struct IptcData;
//struct GnomeVFSFileInfo;
struct DBData;

class QuiverFile::QuiverFileImpl {
	// Associations
	// Attributes
public:

	// Operations
	
	QuiverFileImpl(const gchar*  uri);
	QuiverFileImpl(const gchar* , GnomeVFSFileInfo *info);
	~QuiverFileImpl();
	
	
	gchar* m_szURI;
	GnomeVFSFileInfo *file_info;
 	ExifData *exif_data;
	IptcData *iptc_data;
	DBData *db_data;
	
	// if load is successful, set the flags accordingly
	QuiverDataFlags data_loaded_flags;
	
	// if the load of data is attempted, set flag accordingly
	QuiverDataFlags data_exists_flags;
	
	int width;
	int height;
	double loadTimeSeconds;
	
	static ImageCache c_ThumbnailCache;
private:
	void Init(const gchar *uri, GnomeVFSFileInfo *info);
};


ImageCache QuiverFile::QuiverFileImpl::c_ThumbnailCache(100); // thumbnail cache of size 100!

QuiverFile::QuiverFileImpl::QuiverFileImpl(const gchar * uri)
{
	// get the file info
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,info,(GnomeVFSFileInfoOptions)
									  (GNOME_VFS_FILE_INFO_DEFAULT|
									  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
									  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
									  GNOME_VFS_FILE_INFO_FOLLOW_LINKS)
									  );
	Init(uri,info);
}

QuiverFile::QuiverFileImpl::QuiverFileImpl(const gchar *uri, GnomeVFSFileInfo *info)
{
	Init(uri,info);
}

void QuiverFile::QuiverFileImpl::Init(const gchar *uri, GnomeVFSFileInfo *info)
{
	//m_szURI = (gchar*)malloc (sizeof(gchar) * strlen(uri) + 1 );
	m_szURI = new gchar [ strlen(uri) + 1 ];
	g_stpcpy(m_szURI,uri);

	file_info = info;
	gnome_vfs_file_info_ref(file_info);

	data_loaded_flags = QUIVER_FILE_DATA_NONE;
	data_exists_flags = QUIVER_FILE_DATA_NONE;
	
	if (NULL != info)
	{
		data_exists_flags = (QuiverDataFlags)(data_exists_flags | QUIVER_FILE_DATA_INFO);
		data_loaded_flags = (QuiverDataFlags)(data_loaded_flags | QUIVER_FILE_DATA_INFO);
	}
	
	exif_data = NULL;
	iptc_data = NULL;	

	width = -1;
	height = -1;
	loadTimeSeconds = 0;
}

QuiverFile::QuiverFileImpl::~QuiverFileImpl()
{
	//free ( m_szURI );
	gnome_vfs_file_info_unref(file_info);
	
	delete [] m_szURI;

	if (NULL != exif_data)
	{
		exif_data_unref(exif_data);
	}
}




// =================================================================================================
// QuiverFile Wrapper Class
// =================================================================================================

QuiverFile::QuiverFile() : QuiverFilePtr( new QuiverFileImpl("") )
{
}
QuiverFile::QuiverFile(const gchar*  uri)  : QuiverFilePtr( new QuiverFileImpl(uri) )
{
	
}
QuiverFile::QuiverFile(const gchar* uri, GnomeVFSFileInfo *info) : QuiverFilePtr( new QuiverFileImpl(uri,info) )
{
	
}
QuiverFile::~QuiverFile()
{
	
}

GnomeVFSFileInfo * QuiverFile::GetFileInfo()
{
	gnome_vfs_file_info_ref(QuiverFilePtr->file_info);
	return QuiverFilePtr->file_info;
}

std::string QuiverFile::GetFilePath()
{
	std::string s;
	if (GNOME_VFS_FILE_INFO_LOCAL (QuiverFilePtr->file_info) )
	{
		char * uri = gnome_vfs_get_local_path_from_uri (QuiverFilePtr->m_szURI);
		s = uri;
		g_free(uri);
	}
	else
	{
		s = QuiverFilePtr->m_szURI;
	}
	return s;
}

const gchar* QuiverFile::GetURI()
{
	return QuiverFilePtr->m_szURI;
}


void QuiverFile::SetLoadTimeInSeconds(double seconds)
{
	QuiverFilePtr->loadTimeSeconds = seconds;
}
double QuiverFile::GetLoadTimeInSeconds()
{
	return QuiverFilePtr->loadTimeSeconds;
}

void QuiverFile::SetWidth(int w)
{
	QuiverFilePtr->width = w;
}
void QuiverFile::SetHeight(int h)
{
	QuiverFilePtr->height = h;
}
int QuiverFile::GetWidth()
{
	/*
	if (-1 == QuiverFilePtr->width)
	{
		
	}
	*/
	return QuiverFilePtr->width;
}
int QuiverFile::GetHeight()
{
	/*
	if (-1 == QuiverFilePtr->height)
	{
			
	}
	*/
	return QuiverFilePtr->height;
}
void QuiverFile::LoadExifData()
{
	//Timer t("QuiverFile::LoadExifData()");
	if (! ( QuiverFilePtr->data_loaded_flags & QUIVER_FILE_DATA_EXIF ) )
	{
		ExifLoader *loader;
		//	unsigned char data[1024];
	
		loader = exif_loader_new ();
	
		unsigned char buffer[2048];
		
		GnomeVFSHandle   *handle;
		GnomeVFSResult    result;
		GnomeVFSFileSize  bytes_read;
		
		result = gnome_vfs_open (&handle, QuiverFilePtr->m_szURI, GNOME_VFS_OPEN_READ);
		if (GNOME_VFS_OK == result)
		{
			while (GNOME_VFS_OK == result)
			{
				result = gnome_vfs_read (handle, buffer, sizeof(buffer), &bytes_read);
				
				if (!exif_loader_write (loader, buffer, bytes_read))
				{
					break;
				}
			}
			gnome_vfs_close(handle);
		}
		
		QuiverFilePtr->exif_data = exif_loader_get_data (loader);
		exif_loader_unref (loader);
		
		if (NULL == QuiverFilePtr->exif_data)
		{
			//printf("exif data is null\n");
		}
		else
		{
			bool no_exif = true;
			
			// go through exif fields to see if any are set (if not, no exif)
			for (int i = 0; i < EXIF_IFD_COUNT && no_exif; i++) {
				if (QuiverFilePtr->exif_data->ifd[i] && QuiverFilePtr->exif_data->ifd[i]->count) {
					no_exif = false;
				}
			}
			if (QuiverFilePtr->exif_data->data)
			{
				no_exif = false;
			}
			
			if (!no_exif)
			{
				QuiverFilePtr->data_exists_flags = (QuiverDataFlags)(QuiverFilePtr->data_exists_flags | QUIVER_FILE_DATA_EXIF);
				//printf("there is exif!!\n");
			}
			else
			{
				//printf("no data in the exif!\n");
				// dont keep structure around, it has nothing in it
				exif_data_unref(QuiverFilePtr->exif_data);
				QuiverFilePtr->exif_data = NULL;
			}
	
		}
		QuiverFilePtr->data_loaded_flags = (QuiverDataFlags)(QuiverFilePtr->data_loaded_flags | QUIVER_FILE_DATA_EXIF);
	}
}

GdkPixbuf * QuiverFile::GetExifThumbnail()
{
	//Timer t("QuiverFile::GetExifThumbnail()");
	GdkPixbuf *thumb_pixbuf = NULL;

	LoadExifData();
	//exif_data_dump(exif_data);
	if (QuiverFilePtr->data_exists_flags & QUIVER_FILE_DATA_EXIF && QuiverFilePtr->exif_data->data)
	{
		GError *tmp_error;
		tmp_error = NULL;
		
		GdkPixbufLoader *pixbuf_loader;
		
		pixbuf_loader = gdk_pixbuf_loader_new();
		gdk_pixbuf_loader_write (pixbuf_loader,(guchar*)QuiverFilePtr->exif_data->data, QuiverFilePtr->exif_data->size, &tmp_error);

		gdk_pixbuf_loader_close(pixbuf_loader,&tmp_error);
		thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(pixbuf_loader);
		
		if (NULL != thumb_pixbuf)
		{
			
			g_object_ref(thumb_pixbuf);
			/*
			int w = gdk_pixbuf_get_width(thumb_pixbuf);
			int h = gdk_pixbuf_get_height(thumb_pixbuf);
			*/
			//printf ("thumbnail size: %d x %d\n",w,h);
			// to keep using this thumbnail
			// we must ref it
		}				
		g_object_unref(pixbuf_loader);
		
	}
		//thumb data
	return thumb_pixbuf;
}

GdkPixbuf * QuiverFile::GetThumbnail()
{

	//Timer t("QuiverFile::GetThumbnail()");
	GdkPixbuf * thumb_pixbuf = NULL;
	//GnomeThumbnailFactory* thumb_factory;
	//thumb_factory =	gnome_thumbnail_factory_new(GNOME_THUMBNAIL_SIZE_NORMAL);

	gchar * thumb_path ;
	thumb_path = gnome_thumbnail_path_for_uri(QuiverFilePtr->m_szURI,GNOME_THUMBNAIL_SIZE_NORMAL);
	//printf("thumb path %s: %s\n",QuiverFilePtr->m_szURI,thumb_path);

	//const gchar* gdk_pixbuf_get_option(GdkPixbuf *pixbuf,const gchar *key);
	
	// try to load the thumb from thumb_path
	//gdk_threads_enter ();
	GdkPixbufLoader* loader = gdk_pixbuf_loader_new ();	

	GError *tmp_error;
	gchar buffer[8192];
	GnomeVFSHandle   *handle;
	GnomeVFSResult    result;
	GnomeVFSFileSize  bytes_read;
	
	tmp_error = NULL;
	result = gnome_vfs_open (&handle, thumb_path, GNOME_VFS_OPEN_READ);
	if (GNOME_VFS_OK == result)
	{
		while (GNOME_VFS_OK == result) {
			result = gnome_vfs_read (handle, buffer, 
									 sizeof(buffer), &bytes_read);
			gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);
		}
		gdk_pixbuf_loader_close(loader,&tmp_error);
		gnome_vfs_close(handle);
		
		thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
	
		// check mtime (if they do not match, we dont want to use this thumbnail)
		const char *thumb_uri, *thumb_mtime_str;
		thumb_uri = gdk_pixbuf_get_option (thumb_pixbuf,"tEXt::Thumb::URI");
  		thumb_mtime_str = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::MTime");
		
		time_t mtime;
		mtime = atol (thumb_mtime_str);
		
		if (QuiverFilePtr->file_info->mtime == mtime)
		{
			//mtimes match, it's good!
			g_object_ref(thumb_pixbuf);
		}
		else
		{
			// they dont match.. we should load a new version
			thumb_pixbuf = NULL;
		}
		g_object_unref(loader);
	}
	//gdk_threads_leave ();
	
	if (NULL == thumb_pixbuf) // no thumbnail in the ~/.thumbnails/normal/ cache
	{ 
		// unable to load from file, next try exif
		thumb_pixbuf = GetExifThumbnail();
		if (NULL != thumb_pixbuf)
		{	
			// FIXME:
			// we should store this into .thumbnails (see commented out code at bottom of
			// file
			// we should also  return a thumbnail that is 128x128 pixels
		}
	}

	g_free (thumb_path);

	//FIXME: need to get an autorotate option
	
	int orientation = GetExifOrientation();
	if (NULL != thumb_pixbuf  && 1 < orientation) 
	{
		// FIXME: we shouldnt have GdkPixbufExifReorientate in the viewer
		// it should be in a utility class
		GdkPixbuf * new_pixbuf = Viewer::GdkPixbufExifReorientate(thumb_pixbuf, orientation);
		if (NULL != new_pixbuf)
		{
			g_object_unref(thumb_pixbuf);
			thumb_pixbuf = new_pixbuf;
		}
	}
	

	return thumb_pixbuf;
}

ExifData* QuiverFile::GetExifData()
{
	exif_data_ref(QuiverFilePtr->exif_data);
	return QuiverFilePtr->exif_data;
}
	
int QuiverFile::GetExifOrientation()
{
	LoadExifData();
	int orientation = 0;
	
	if (QuiverFilePtr->data_exists_flags & QUIVER_FILE_DATA_EXIF)
	{
		ExifEntry *entry;
		entry = exif_data_get_entry(QuiverFilePtr->exif_data,EXIF_TAG_ORIENTATION);
		if (NULL != entry)
		{
			// the orientation field is set
			//exif_entry_dump(entry,2);
			ExifByteOrder o;
			o = exif_data_get_byte_order (entry->parent->parent);
			orientation = exif_get_short (entry->data, o);
		}
	}
	return orientation;
}

/**
 * gnome_thumbnail_factory_save_thumbnail:
 * @factory: a #GnomeThumbnailFactory
 * @thumbnail: the thumbnail as a pixbuf 
 * @uri: the uri of a file
 * @original_mtime: the mime type of the file
 *
 * Saves @thumbnail at the right place. If the save fails a
 * failed thumbnail is written.
 *
 * Usage of this function is threadsafe.
 **/
 /*
void
gnome_thumbnail_factory_save_thumbnail (GnomeThumbnailFactory *factory,
					GdkPixbuf             *thumbnail,
					const char            *uri,
					time_t                 original_mtime)
{
  GnomeThumbnailFactoryPrivate *priv = factory->priv;
  unsigned char *digest;
  char *path, *md5, *file, *dir;
  char *tmp_path;
  int tmp_fd;
  char mtime_str[21];
  gboolean saved_ok;
  struct stat statbuf;
  struct ThumbnailInfo *info;
  
  g_mutex_lock (priv->lock);
  
  gnome_thumbnail_factory_ensure_uptodate (factory);

  g_mutex_unlock (priv->lock);
  
  digest = g_malloc (16);
  thumb_md5 (uri, digest);

  md5 = thumb_digest_to_ascii (digest);
  file = g_strconcat (md5, ".png", NULL);
  g_free (md5);
  
  dir = g_build_filename (g_get_home_dir (),
			  ".thumbnails",
			  (priv->size == GNOME_THUMBNAIL_SIZE_NORMAL)?"normal":"large",
			  NULL);
  
  path = g_build_filename (dir,
			   file,
			   NULL);
  g_free (file);

  tmp_path = g_strconcat (path, ".XXXXXX", NULL);

  tmp_fd = g_mkstemp (tmp_path);
  if (tmp_fd == -1 &&
      make_thumbnail_dirs (factory))
    {
      g_free (tmp_path);
      tmp_path = g_strconcat (path, ".XXXXXX", NULL);
      tmp_fd = g_mkstemp (tmp_path);
    }

  if (tmp_fd == -1)
    {
      gnome_thumbnail_factory_create_failed_thumbnail (factory, uri, original_mtime);
      g_free (dir);
      g_free (tmp_path);
      g_free (path);
      g_free (digest);
      return;
    }
  close (tmp_fd);
  
  g_snprintf (mtime_str, 21, "%lu",  original_mtime);
  saved_ok  = gdk_pixbuf_save (thumbnail,
			       tmp_path,
			       "png", NULL, 
			       "tEXt::Thumb::URI", uri,
			       "tEXt::Thumb::MTime", mtime_str,
			       "tEXt::Software", "GNOME::ThumbnailFactory",
			       NULL);
  if (saved_ok)
    {
      g_chmod (tmp_path, 0600);
      g_rename(tmp_path, path);

      info = g_new (struct ThumbnailInfo, 1);
      info->mtime = original_mtime;
      info->uri = g_strdup (uri);
      
      g_mutex_lock (priv->lock);
      
      g_hash_table_insert (factory->priv->existing_thumbs, digest, info);
*/
      /* Make sure we don't re-read the directory. We should be uptodate
       * with all previous changes du to the ensure_uptodate above.
       * There is still a small window here where we might miss exisiting
       * thumbnails, but that shouldn't matter. (we would just redo them or
       * catch them later).
       */
/*
	   if (g_stat(dir, &statbuf) == 0)
	factory->priv->read_existing_mtime = statbuf.st_mtime;
      
      g_mutex_unlock (priv->lock);
    }
  else
    {
      g_free (digest);
      gnome_thumbnail_factory_create_failed_thumbnail (factory, uri, original_mtime);
    }

  g_free (dir);
  g_free (path);
  g_free (tmp_path);
}
*/

/*

const gchar* QuiverFile::GetURI()
{
	return QuiverFilePtr->GetURI();
	
}
//static QuiverFile GetInstance(std::string path);

GdkPixbuf * QuiverFile::GetThumbnail()
{
	return QuiverFilePtr->GetThumbnail();	
}
GdkPixbuf * QuiverFile::GetExifThumbnail()
{
	return QuiverFilePtr->GetExifThumbnail();	
}
void QuiverFile::LoadExifData()
{
	QuiverFilePtr->LoadExifData();
}
ExifData *QuiverFile::GetExifData()
{
	return QuiverFilePtr->GetExifData();
}
int QuiverFile::GetExifOrientation()
{
	return QuiverFilePtr->GetExifOrientation();
}
GnomeVFSFileInfo * QuiverFile::GetFileInfo()
{
	return QuiverFilePtr->GetFileInfo();
}

*/

















