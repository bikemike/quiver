#include <config.h>
#include <glib.h>
#include <libgnomeui/gnome-thumbnail.h>
#include <libgnomeui/gnome-icon-lookup.h>
#include <exif-utils.h>
#include <glib/gstdio.h>

#include "QuiverFile.h"
#include "Timer.h"
#include "QuiverUtils.h"




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
	
	static ImageCache c_ThumbnailCacheNormal;
	static ImageCache c_ThumbnailCacheLarge;
private:
	void Init(const gchar *uri, GnomeVFSFileInfo *info);
};


ImageCache QuiverFile::QuiverFileImpl::c_ThumbnailCacheNormal(100),QuiverFile::QuiverFileImpl::c_ThumbnailCacheLarge(50); // thumbnail cache of size 1000!

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
									  GNOME_VFS_FILE_INFO_FOLLOW_LINKS|
									  GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS)
 
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

QuiverFile::QuiverFile() : m_QuiverFilePtr( new QuiverFileImpl("") )
{
}
QuiverFile::QuiverFile(const gchar*  uri)  : m_QuiverFilePtr( new QuiverFileImpl(uri) )
{
	
}
QuiverFile::QuiverFile(const gchar* uri, GnomeVFSFileInfo *info) : m_QuiverFilePtr( new QuiverFileImpl(uri,info) )
{
	
}
QuiverFile::~QuiverFile()
{
	
}

GnomeVFSFileInfo * QuiverFile::GetFileInfo()
{
	gnome_vfs_file_info_ref(m_QuiverFilePtr->file_info);
	return m_QuiverFilePtr->file_info;
}

unsigned long long QuiverFile::GetFileSize()
{
	return m_QuiverFilePtr->file_info->size;
}

std::string QuiverFile::GetFilePath()
{
	std::string s;
	if (GNOME_VFS_FILE_INFO_LOCAL (m_QuiverFilePtr->file_info) )
	{
		char * uri = gnome_vfs_get_local_path_from_uri (m_QuiverFilePtr->m_szURI);
		s = uri;
		g_free(uri);
	}
	else
	{
		s = m_QuiverFilePtr->m_szURI;
	}
	return s;
}

const gchar* QuiverFile::GetURI()
{
	return m_QuiverFilePtr->m_szURI;
}


void QuiverFile::SetLoadTimeInSeconds(double seconds)
{
	m_QuiverFilePtr->loadTimeSeconds = seconds;
}
double QuiverFile::GetLoadTimeInSeconds()
{
	return m_QuiverFilePtr->loadTimeSeconds;
}

void QuiverFile::SetWidth(int w)
{
	m_QuiverFilePtr->width = w;
}
void QuiverFile::SetHeight(int h)
{
	m_QuiverFilePtr->height = h;
}
int QuiverFile::GetWidth()
{
	/*
	if (-1 == m_QuiverFilePtr->width)
	{
		
	}
	*/
	return m_QuiverFilePtr->width;
}
int QuiverFile::GetHeight()
{
	/*
	if (-1 == m_QuiverFilePtr->height)
	{
			
	}
	*/
	return m_QuiverFilePtr->height;
}

bool QuiverFile::IsWriteable()
{
	bool rval = false;
	if (GNOME_VFS_FILE_INFO_FIELDS_ACCESS & m_QuiverFilePtr->file_info->valid_fields)
	{
		if (GNOME_VFS_PERM_ACCESS_WRITABLE & m_QuiverFilePtr->file_info->permissions)
		{
			rval = true;
		}
	}

	return rval;
}

void QuiverFile::LoadExifData()
{
	//Timer t("QuiverFile::LoadExifData()");
	if (! ( m_QuiverFilePtr->data_loaded_flags & QUIVER_FILE_DATA_EXIF ) )
	{
		ExifLoader *loader;
		//	unsigned char data[1024];
	
		loader = exif_loader_new ();
	
		unsigned char buffer[2048];
		
		GnomeVFSHandle   *handle;
		GnomeVFSResult    result;
		GnomeVFSFileSize  bytes_read;
		
		result = gnome_vfs_open (&handle, m_QuiverFilePtr->m_szURI, GNOME_VFS_OPEN_READ);
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
		
		m_QuiverFilePtr->exif_data = exif_loader_get_data (loader);
		exif_loader_unref (loader);
		
		if (NULL == m_QuiverFilePtr->exif_data)
		{
			//printf("exif data is null\n");
		}
		else
		{
			bool no_exif = true;
			
			// go through exif fields to see if any are set (if not, no exif)
			for (int i = 0; i < EXIF_IFD_COUNT && no_exif; i++) {
				if (m_QuiverFilePtr->exif_data->ifd[i] && m_QuiverFilePtr->exif_data->ifd[i]->count) {
					no_exif = false;
				}
			}
			if (m_QuiverFilePtr->exif_data->data)
			{
				no_exif = false;
			}
			
			if (!no_exif)
			{
				m_QuiverFilePtr->data_exists_flags = (QuiverDataFlags)(m_QuiverFilePtr->data_exists_flags | QUIVER_FILE_DATA_EXIF);
				//printf("there is exif!!\n");
			}
			else
			{
				//printf("no data in the exif!\n");
				// dont keep structure around, it has nothing in it
				exif_data_unref(m_QuiverFilePtr->exif_data);
				m_QuiverFilePtr->exif_data = NULL;
			}
	
		}
		m_QuiverFilePtr->data_loaded_flags = (QuiverDataFlags)(m_QuiverFilePtr->data_loaded_flags | QUIVER_FILE_DATA_EXIF);
	}
}

GdkPixbuf * QuiverFile::GetExifThumbnail()
{
	//Timer t("QuiverFile::GetExifThumbnail()");
	GdkPixbuf *thumb_pixbuf = NULL;

	ExifData *exif_data = GetExifData();
	//exif_data_dump(exif_data);
	if (m_QuiverFilePtr->data_exists_flags & QUIVER_FILE_DATA_EXIF && exif_data->data)
	{
		GError *tmp_error;
		tmp_error = NULL;
		
		GdkPixbufLoader *pixbuf_loader;
		
		pixbuf_loader = gdk_pixbuf_loader_new();
		gdk_pixbuf_loader_write (pixbuf_loader,(guchar*)exif_data->data, exif_data->size, &tmp_error);

		gdk_pixbuf_loader_close(pixbuf_loader,&tmp_error);
		thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(pixbuf_loader);
		
		if (NULL != thumb_pixbuf)
		{
			
			g_object_ref(thumb_pixbuf);
			//int w = gdk_pixbuf_get_width(thumb_pixbuf);
			//int h = gdk_pixbuf_get_height(thumb_pixbuf);
			//printf ("thumbnail size: %d x %d\n",w,h);
			// to keep using this thumbnail
			// we must ref it
		}				
		g_object_unref(pixbuf_loader);
		
	}
		//thumb data
	return thumb_pixbuf;
}

static void pixbuf_loader_size_prepared (GdkPixbufLoader *loader, gint width,
                                            gint             height,
                                            gpointer         user_data)
{
	int size = *(int*)user_data;

	if (size < width || size < height)
	{
		//printf("resize thumbnail to size: %d from %dx%d\n",size,width,height);
		gint new_width,new_height;
		if (size < width && height < width)
		{
			new_width = size;
			new_height = (gint)(size*(double)height/(double)width);
		}
		else
		{
			new_height = size;
			new_width = (gint)(size* (double)width/(double)height);
		}
		//printf("resize thumbnail to size: %dx%d\n",new_width,new_height);
		gdk_pixbuf_loader_set_size(loader,new_width,new_height);
	}
	else
	{
		// we set the size to -1 to inform the 
		// loader that the original size was smaller than
		// the size requested
		//printf("we are not resizing: %dx%d\n",width,height);
		size = -1;
	}
	
}

GdkPixbuf * QuiverFile::GetThumbnail()
{
	return GetThumbnail(false);
}

GdkPixbuf * QuiverFile::GetThumbnail(bool bLargeThumb)
{
	GnomeThumbnailSize thumb_size = GNOME_THUMBNAIL_SIZE_NORMAL;
	if (bLargeThumb)
	{
		thumb_size = GNOME_THUMBNAIL_SIZE_LARGE;
	}
	
	gboolean save_thumbnail_to_cache = TRUE;
	
	//Timer t("QuiverFile::GetThumbnail()");
	GdkPixbuf * thumb_pixbuf = NULL;


	if (bLargeThumb)
	{
		thumb_pixbuf = m_QuiverFilePtr->c_ThumbnailCacheLarge.GetPixbuf(m_QuiverFilePtr->m_szURI);
		if (NULL != thumb_pixbuf)
		{
			return thumb_pixbuf;
		}
	}
	else
	{
		thumb_pixbuf = m_QuiverFilePtr->c_ThumbnailCacheNormal.GetPixbuf(m_QuiverFilePtr->m_szURI);
		if (NULL != thumb_pixbuf)
		{
			return thumb_pixbuf;
		}
	}
	//GnomeThumbnailFactory* thumb_factory;
	//thumb_factory =	gnome_thumbnail_factory_new(GNOME_THUMBNAIL_SIZE_NORMAL);

	gchar * thumb_path ;
	thumb_path = gnome_thumbnail_path_for_uri(m_QuiverFilePtr->m_szURI,thumb_size);
	gchar *large_thumb_file = gnome_thumbnail_path_for_uri(m_QuiverFilePtr->m_szURI,GNOME_THUMBNAIL_SIZE_LARGE);
	gchar *large_thumb_path = g_path_get_dirname(large_thumb_file);

	g_free(large_thumb_file);
	//printf("thumb path %s: %s\n",m_QuiverFilePtr->m_szURI,thumb_path);

	//const gchar* gdk_pixbuf_get_option(GdkPixbuf *pixbuf,const gchar *key);
	
	// try to load the thumb from thumb_path
	//gdk_threads_enter ();
	int orientation = 0;

	GError *tmp_error;
	gchar buffer[8192];
	GnomeVFSHandle   *handle;
	GnomeVFSResult    result;
	GnomeVFSFileSize  bytes_read;
	
	tmp_error = NULL;
	result = gnome_vfs_open (&handle, thumb_path, GNOME_VFS_OPEN_READ);
	if (GNOME_VFS_OK == result)
	{
		GdkPixbufLoader* loader = gdk_pixbuf_loader_new ();	

		while (GNOME_VFS_OK == result) {
			result = gnome_vfs_read (handle, buffer, 
									 sizeof(buffer), &bytes_read);
			gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);
		}
		gnome_vfs_close(handle);
		
		thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);

		if (NULL != thumb_pixbuf)
		{
			g_object_ref(thumb_pixbuf);
			//printf("got thumb from cache\n");
	
			// check mtime (if they do not match, we dont want to use this thumbnail)
			const char *thumb_uri, *thumb_mtime_str, *str_orientation;
			thumb_uri = gdk_pixbuf_get_option (thumb_pixbuf,"tEXt::Thumb::URI");
			thumb_mtime_str = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::MTime");
			str_orientation = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::Image::Orientation");
			if (NULL != str_orientation)
			{
				//printf("we got orientation: %s\n",str_orientation);
				orientation = atoi(str_orientation);
			}
			
			time_t mtime;
			mtime = atol (thumb_mtime_str);
			
			if (m_QuiverFilePtr->file_info->mtime != mtime)
			{
				// they dont match.. we should load a new version
				//printf("m-times do not match! %lu  != %lu\n",m_QuiverFilePtr->file_info->mtime ,mtime);
				g_object_unref(thumb_pixbuf);
				thumb_pixbuf = NULL;
			}
			else
			{
				//printf("m-times do match! %lu  != %lu\n",m_QuiverFilePtr->file_info->mtime ,mtime);
				save_thumbnail_to_cache = FALSE;
			}

		}
		gdk_pixbuf_loader_close(loader,&tmp_error);
		g_object_unref(loader);
	}
	//gdk_threads_leave ();
	
	if (NULL == thumb_pixbuf && !bLargeThumb) // no thumbnail in the ~/.thumbnails/ cache
	{ 
		// unable to load from file, next try exif
		thumb_pixbuf = GetExifThumbnail();
		if (NULL != thumb_pixbuf)
		{
			int size = 128;
			gint pixbuf_width = gdk_pixbuf_get_width(thumb_pixbuf);
			gint pixbuf_height = gdk_pixbuf_get_height(thumb_pixbuf);
			// resize it to the proper size
			if (pixbuf_width > size || pixbuf_height > size)
			{
				int nw,nh;
				if (size < pixbuf_width && pixbuf_width > pixbuf_height)
				{
					nw = size;
					nh = (int) (size * (double)pixbuf_height/(double)pixbuf_width);
				}
				else
				{
					nh = size;
					nw = (int)(size * (double)pixbuf_width / (double)pixbuf_height);
				}
				pixbuf_width = nw;
				pixbuf_height = nh;
				//printf("new size %d %d\n",nw,nh);
				GdkPixbuf* newpixbuf = gdk_pixbuf_scale_simple (
									thumb_pixbuf,
									nw,
									nh,
									GDK_INTERP_BILINEAR);
									//GDK_INTERP_NEAREST);
				g_object_unref(thumb_pixbuf);
				thumb_pixbuf = newpixbuf;
			}
			//printf("got thumb from exif\n");
		}
	}

	// we couldnt get it from the cache, or the exif, so we'll have to load it straight from the file
	if (NULL == thumb_pixbuf)
	{
		int size = 128;
		if (bLargeThumb)
		{
			size = 256;
		}

		result = gnome_vfs_open (&handle, m_QuiverFilePtr->m_szURI, GNOME_VFS_OPEN_READ);
		if (GNOME_VFS_OK == result)
		{
			GdkPixbufLoader* loader = gdk_pixbuf_loader_new ();	
			g_signal_connect (loader,"size-prepared",G_CALLBACK (pixbuf_loader_size_prepared), &size);	

			while (GNOME_VFS_OK == result) {
				result = gnome_vfs_read (handle, buffer, 
										 sizeof(buffer), &bytes_read);
				gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);
			}
			gnome_vfs_close(handle);

			gdk_pixbuf_loader_close(loader,&tmp_error);
			
			thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
		
			if (NULL != thumb_pixbuf)
			{
				g_object_ref(thumb_pixbuf);
				//printf("got thumb from file\n");
			}
			g_object_unref(loader);
		}
		if (-1 == size)
		{
			// size of image is smaller than 128x128 so
			// we do not need to cache it
			save_thumbnail_to_cache = FALSE;
		}
	}
	
	
	if (save_thumbnail_to_cache && !bLargeThumb)
	{
		std::string filepath = GetFilePath();
		
		if (NULL != strstr(filepath.c_str(),large_thumb_path) )
		{
			// if user is snooping in the .thumbnails/large
			// directory, we dont want to generate thumbnails
			// for these items
			save_thumbnail_to_cache = FALSE; 
		}
	}
	if (NULL != large_thumb_path)
	{
		g_free(large_thumb_path);
	}
	
	// FIXME: maybe this should be in a low priority thread?
	// save the thumbnail to the cache directory (~/.thumbnails)

	if (NULL != thumb_pixbuf && save_thumbnail_to_cache)
	{

		// FIXME: should make sure the directory exists first!
		gchar *temp_file_name = g_strconcat (thumb_path, ".XXXXXX", NULL);
		gint fhandle = g_mkstemp (temp_file_name);
		
		if (-1 != fhandle )
		{
			close (fhandle);
			gchar str_mtime[30];
			gchar str_orientation[2];
			//printf("temp_file_name = %s -> %s\n",temp_file_name,m_QuiverFilePtr->m_szURI);
			g_snprintf (str_mtime, 30, "%lu",  m_QuiverFilePtr->file_info->mtime);

			orientation = GetExifOrientation();
			g_snprintf (str_orientation, 2, "%d",  orientation);
			//printf("orientation to save: %s\n",str_orientation);
			
			gboolean saved = gdk_pixbuf_save (thumb_pixbuf,
					   temp_file_name,
					   "png", NULL, 
					   "tEXt::Thumb::URI", m_QuiverFilePtr->m_szURI,
					   "tEXt::Thumb::MTime", str_mtime,
					   "tEXt::Thumb::Image::Orientation", str_orientation,
					   "tEXt::Software", PACKAGE_STRING,
					   NULL);
			if (saved)
			{
				//printf("move: %s => %s \n",temp_file_name,thumb_path);
				g_chmod (temp_file_name, 0600);
				g_rename(temp_file_name, thumb_path);
			}
			else
			{
				g_remove(temp_file_name);
			}
		}
		g_free(temp_file_name);
	}
	
	g_free (thumb_path);

	//FIXME: need to get an autorotate option
	
	if (0 == orientation)
	{
		orientation = GetExifOrientation();
	}
	if (NULL != thumb_pixbuf  && 1 < orientation) 
	{
		GdkPixbuf * new_pixbuf = QuiverUtils::GdkPixbufExifReorientate(thumb_pixbuf, orientation);
		if (NULL != new_pixbuf)
		{
			g_object_unref(thumb_pixbuf);
			thumb_pixbuf = new_pixbuf;
		}
	}


	if (NULL != thumb_pixbuf)
	{
		if (bLargeThumb)
		{
			m_QuiverFilePtr->c_ThumbnailCacheLarge.AddPixbuf(m_QuiverFilePtr->m_szURI,thumb_pixbuf);
		}
		else
		{
			m_QuiverFilePtr->c_ThumbnailCacheNormal.AddPixbuf(m_QuiverFilePtr->m_szURI,thumb_pixbuf);
		}
	}

	return thumb_pixbuf;
}

GdkPixbuf* QuiverFile::GetIcon(int width_desired,int height_desired)
{
	
	GError *error = NULL;


	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

	GnomeIconLookupResultFlags result;
	/*
	GNOME_ICON_LOOKUP_RESULT_FLAGS_NONE
	GNOME_ICON_LOOKUP_FLAGS_NONE
	*/
	GnomeVFSFileInfo *file_info = GetFileInfo();
	
	char* icon_name = gnome_icon_lookup (icon_theme,
										 NULL,
										 GetURI(),
										 NULL,
										 file_info,
										 file_info->mime_type,
										 GNOME_ICON_LOOKUP_FLAGS_NONE,
										 &result);
	gint* sizes_orig = gtk_icon_theme_get_icon_sizes   (icon_theme, icon_name);
	gint* sizes = sizes_orig;

	gint size_wanted = MIN(width_desired,height_desired);
	
	gint size;
	gint size_to_get = 0;
	while (0 != (size = *sizes))
	{
		if (size <= size_wanted)
		{
			size_to_get = MAX(size_to_get,size);
		}
		sizes++;
	}
	g_free(sizes_orig);

	GdkPixbuf* pixbuf = gtk_icon_theme_load_icon (icon_theme,
                                             icon_name,
                                             size_to_get,
                                             GTK_ICON_LOOKUP_USE_BUILTIN,
                                             &error);
	gnome_vfs_file_info_unref(file_info);
	g_free(icon_name);

	return pixbuf;
}


ExifData* QuiverFile::GetExifData()
{
	LoadExifData();
	exif_data_ref(m_QuiverFilePtr->exif_data);
	return m_QuiverFilePtr->exif_data;
}
	
int QuiverFile::GetExifOrientation()
{
	ExifData *exif_data = GetExifData();
	int orientation = 0;
	
	if (m_QuiverFilePtr->data_exists_flags & QUIVER_FILE_DATA_EXIF)
	{
		ExifEntry *entry;
		entry = exif_data_get_entry(exif_data,EXIF_TAG_ORIENTATION);
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









