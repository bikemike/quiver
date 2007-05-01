#include <config.h>
#include <glib.h>

#ifndef QUIVER_MAEMO
#include <libgnomeui/gnome-icon-lookup.h>
#else
#include <osso-mime.h>
#endif

#include <libexif/exif-utils.h>
#include <glib/gstdio.h>

#include <gcrypt.h>

#include <string>

#include "QuiverFile.h"
#include "Timer.h"
#include "QuiverUtils.h"

#include <map>

#include <libquiver/quiver-pixbuf-utils.h>

// =================================================================================================
// Implementation
// =================================================================================================

typedef struct _ThumbnailSize
{
	int size;
	char* name;
} ThumbnailSize;

ThumbnailSize ThumbnailSizes[] =
{
	// these must be in order from smallest to largest
#ifdef QUIVER_MAEMO
		{80,"osso"},
#endif
		{128,"normal"},
		{256,"large"},
};


class ThumbnailCache
{
public:
	ThumbnailCache()
	{
		for (unsigned int i = 0; i < G_N_ELEMENTS(ThumbnailSizes); i++)
		{
			// 1mb = 1024 * 1024;
			int cache_size = 4 * 1024 * 1024; // 2mb
			
			int n_images = cache_size / (ThumbnailSizes[i].size * ThumbnailSizes[i].size * 4);
			m_mapThumbnailCache.insert(std::pair<int,ImageCache*>(ThumbnailSizes[i].size,new ImageCache(n_images)));
		}
		
	};
	
	~ThumbnailCache()
	{
		std::map<int,ImageCache*>::iterator itr;
		for (itr = m_mapThumbnailCache.begin(); itr != m_mapThumbnailCache.end(); ++itr)
		{
			delete itr->second;
		}
	};
	std::map<int,ImageCache*> m_mapThumbnailCache;
};

struct IptcData;
//struct GnomeVFSFileInfo;
struct DBData;

class QuiverFile::QuiverFileImpl {

public:
// Methods
	
	QuiverFileImpl(const gchar*  uri);
	QuiverFileImpl(const gchar* , GnomeVFSFileInfo *info);
	~QuiverFileImpl();

	void Init(const gchar *uri, GnomeVFSFileInfo *info);
	void Reload();
	
	const char* GetMimeType();
	GnomeVFSFileInfo* GetFileInfo();
	
	int GetWidth();
	int GetHeight();
	
	std::string GetFilePath() const;

	void LoadExifData();
	int GetOrientation();
	time_t GetTimeT();
	
	ExifData* GetExifData();
	bool SetExifData(ExifData* pExifData);	
	GdkPixbuf* GetExifThumbnail();
	
	bool HasThumbnail(int iSize) ;
	GdkPixbuf* GetThumbnail(int iSize /* = 0 */);
	
	bool Modified() const;
	
// variables	
	gchar* m_szURI;
	GnomeVFSFileInfo* m_vfsFileInfo;
 	
 	ExifData* m_pExifData;
 	ExifData* m_pExifDataOriginal;
 	
	IptcData* m_pIPTCData;
	DBData* m_pDBData;
	
	// if load has been tried, set the flags accordingly
	QuiverDataFlags m_fDataLoaded;
	
	// if the load of data is attempted, and the
	// data actually exists set this flag accordingly
	QuiverDataFlags m_fDataExists;

	// indicates if the data has been modified or not
	QuiverDataFlags m_fDataModified;
	
	int m_iWidth;
	int m_iHeight;
	int m_iOrientation;

	double m_dLoadTimeSeconds;
	
	static ThumbnailCache c_ThumbnailCache;
	std::map<int,bool> m_mapThumbnailExists;
};

static void GetImageDimensions(const gchar *uri, gint *width, gint *height);
static void pixbuf_loader_size_prepared (GdkPixbufLoader *loader, gint width,
                                            gint             height,
                                            gpointer         user_data);
gchar* quiver_thumbnail_path_for_uri(const char* uri, const char* szSize);

typedef struct _PixbufLoaderSizeInfoStruct
{
	gint width;
	gint height;
	
	gint size_request;
	
} PixbufLoaderSizeInfo;


ThumbnailCache QuiverFile::QuiverFileImpl::c_ThumbnailCache;

QuiverFile::QuiverFileImpl::QuiverFileImpl(const gchar * uri)
{
	// get the file info
	Init(uri,NULL);
}

QuiverFile::QuiverFileImpl::QuiverFileImpl(const gchar *uri, GnomeVFSFileInfo *info)
{
	Init(uri,info);
}

void QuiverFile::QuiverFileImpl::Init(const gchar *uri, GnomeVFSFileInfo *info)
{
	//m_szURI = (gchar*)malloc (sizeof(gchar) * strlen(uri) + 1 );
	if (NULL != uri)
	{
		m_szURI = new gchar [ strlen(uri) + 1 ];
		g_stpcpy(m_szURI,uri);
	}
	else
	{
		m_szURI = NULL;
	}

	m_vfsFileInfo = info;

	m_fDataLoaded = QUIVER_FILE_DATA_NONE;
	m_fDataExists = QUIVER_FILE_DATA_NONE;
	
	if (NULL != info)
	{
		gnome_vfs_file_info_ref(m_vfsFileInfo);
		m_fDataExists = (QuiverDataFlags)(m_fDataExists | QUIVER_FILE_DATA_INFO);
		m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_INFO);
	}
	
	m_pExifData = NULL;
	m_pExifDataOriginal = NULL;
	
	m_pIPTCData = NULL;	

	m_iWidth = -1;
	m_iHeight = -1;
	
	m_iOrientation = 0;
	
	m_fDataModified = QUIVER_FILE_DATA_NONE;
	
	m_dLoadTimeSeconds = 0;

}

const char* QuiverFile::QuiverFileImpl::GetMimeType()
{
	char *pMimeType = NULL;
	GnomeVFSFileInfo *pInfo = GetFileInfo();
	
	if (NULL != pInfo && GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE & pInfo->valid_fields)
	{
		pMimeType = pInfo->mime_type;
	}
	return pMimeType;
}

GnomeVFSFileInfo* QuiverFile::QuiverFileImpl::GetFileInfo()
{
	GnomeVFSFileInfo* vfsFileInfo = NULL;

	if (m_fDataExists & QUIVER_FILE_DATA_INFO && m_vfsFileInfo)
	{
		vfsFileInfo = m_vfsFileInfo;
	}
	
	if (!(m_fDataLoaded & QUIVER_FILE_DATA_INFO) && NULL != m_szURI && NULL == m_vfsFileInfo)
	{
		GnomeVFSResult result;
		vfsFileInfo = gnome_vfs_file_info_new ();
		result = gnome_vfs_get_file_info (m_szURI,vfsFileInfo,(GnomeVFSFileInfoOptions)
										  (GNOME_VFS_FILE_INFO_DEFAULT|
										  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
										  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
										  GNOME_VFS_FILE_INFO_FOLLOW_LINKS|
										  GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS)
	 
										  );
		m_vfsFileInfo = vfsFileInfo;
		
		m_fDataExists = (QuiverDataFlags)(m_fDataExists | QUIVER_FILE_DATA_INFO);
		m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_INFO);
	}
	
	return vfsFileInfo;
}

QuiverFile::QuiverFileImpl::~QuiverFileImpl()
{
	if (m_vfsFileInfo != NULL)
	{
		gnome_vfs_file_info_unref(m_vfsFileInfo);
	}
	
	if (NULL != m_szURI)
	{
		delete [] m_szURI;
	}

	if (NULL != m_pExifData)
	{
		exif_data_unref(m_pExifData);
	}
	
	if (NULL != m_pExifDataOriginal)
	{
		exif_data_unref(m_pExifDataOriginal);
	}
}

GdkPixbuf * QuiverFile::QuiverFileImpl::GetExifThumbnail()
{
	//Timer t("QuiverFile::GetExifThumbnail()");
	GdkPixbuf *thumb_pixbuf = NULL;

	ExifData *pExifData = GetExifData();
	
	if (m_fDataExists & QUIVER_FILE_DATA_EXIF && pExifData->data)
	{
		GError *tmp_error;
		tmp_error = NULL;
		
		GdkPixbufLoader *pixbuf_loader;
		
		pixbuf_loader = NULL;
		pixbuf_loader = gdk_pixbuf_loader_new();
		if (NULL != pixbuf_loader)
		{
			gdk_pixbuf_loader_write (pixbuf_loader,(guchar*)pExifData->data, pExifData->size, &tmp_error);
	
			gdk_pixbuf_loader_close(pixbuf_loader, NULL);
			thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(pixbuf_loader);
			
			if (NULL != thumb_pixbuf)
			{
				g_object_ref(thumb_pixbuf);
			}				
			g_object_unref(pixbuf_loader);
		}		
	}
	
	return thumb_pixbuf;
}

bool QuiverFile::QuiverFileImpl::HasThumbnail(int iSize)
{
	bool bExists = false;
	ThumbnailSize* thumbSize = NULL;
	
	unsigned int n_elements = G_N_ELEMENTS(ThumbnailSizes);
	for (unsigned int i = 0 ; i < n_elements; i++)
	{
		if (iSize <= ThumbnailSizes[i].size || i == n_elements - 1 )
		{
			thumbSize = &ThumbnailSizes[i];
			break;
		}
	}
	
	if (m_mapThumbnailExists.end() == m_mapThumbnailExists.find(thumbSize->size))
	{
		gchar * thumb_path ;
		thumb_path = quiver_thumbnail_path_for_uri(m_szURI,thumbSize->name);
		
		struct stat s;
		if (0 == g_stat(thumb_path,&s))
		{
			bExists = true;
		}
		g_free(thumb_path);
	}
	else
	{
		bExists = m_mapThumbnailExists[thumbSize->size];
	}
	m_mapThumbnailExists[thumbSize->size] = bExists;
		
	return bExists;
}

GdkPixbuf * QuiverFile::QuiverFileImpl::GetThumbnail(int iSize /* = 0 */)
{
	//Timer t("QuiverFileImpl::GetThumbnail");
	GnomeVFSFileInfo* vfsFileInfo = GetFileInfo();
	
	gboolean save_thumbnail_to_cache = TRUE;
	
	GdkPixbuf * thumb_pixbuf = NULL;

	ThumbnailSize* thumbSize = NULL;
	
	unsigned int n_elements = G_N_ELEMENTS(ThumbnailSizes);
	for (unsigned int i = 0 ; i < n_elements; i++)
	{
		if (iSize <= ThumbnailSizes[i].size || i == n_elements - 1 )
		{
			thumbSize = &ThumbnailSizes[i];
			break;
		}
	}
	thumb_pixbuf = c_ThumbnailCache.m_mapThumbnailCache[thumbSize->size]->GetPixbuf(m_szURI);

	if (NULL != thumb_pixbuf)
	{
		return thumb_pixbuf;
	}
	
	gchar* thumb_path ;
	thumb_path = quiver_thumbnail_path_for_uri(m_szURI,thumbSize->name);
	
	// try to load the thumb from thumb_path
	GError *tmp_error;
	gchar buffer[65536];
	GnomeVFSHandle   *handle;
	GnomeVFSResult    result;
	GnomeVFSFileSize  bytes_read;
	
	tmp_error = NULL;
	result = gnome_vfs_open (&handle, thumb_path, GNOME_VFS_OPEN_READ);
	if (GNOME_VFS_OK == result)
	{
		GdkPixbufLoader* loader = NULL;
		loader = gdk_pixbuf_loader_new ();
		
		if (NULL != loader)
		{

			while (GNOME_VFS_OK == result)
			{
				result = gnome_vfs_read (handle, buffer, 
										 sizeof(buffer), &bytes_read);
				if (!gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error) )
				{
					break;
				}
			}
			gdk_pixbuf_loader_close(loader,NULL);
			
			thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
			
			if (NULL != thumb_pixbuf)
				g_object_ref(thumb_pixbuf);
			
			g_object_unref(loader);
		}
		
		gnome_vfs_close(handle);

		if (NULL != thumb_pixbuf)
		{
			const gchar* thumb_mtime_str = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::MTime");
			const gchar* str_orientation = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::Image::Orientation");

			const gchar* str_thumb_width = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::Image::Width");
			const gchar* str_thumb_height = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::Image::Height");
			const gchar *str_thumb_software = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Software");
			
			if (NULL != str_orientation)
			{
				//printf("we got orientation: %s\n",str_orientation);
				m_iOrientation = atoi(str_orientation);
			}
			
			if (NULL != thumb_mtime_str)
			{
				time_t mtime;
				mtime = atol (thumb_mtime_str);
				
				if (vfsFileInfo && vfsFileInfo->mtime != mtime)
				{
					// they dont match.. we should load a new version
					//printf("m-times do not match! %lu  != %lu\n",vfsFileInfo->mtime ,mtime);
					g_object_unref(thumb_pixbuf);
					thumb_pixbuf = NULL;
				}
				else
				{
					//printf("m-times do match! %lu  != %lu\n",vfsFileInfo->mtime ,mtime);
					save_thumbnail_to_cache = FALSE;
				}
			}
			
			// if we didn't get the width and height we should resave thumbnail
			// with this information 
			if (NULL == str_thumb_width || NULL == str_thumb_height)
			{
				save_thumbnail_to_cache = TRUE;
			}
			else 
			{
				int new_width  = atol(str_thumb_width);
				int new_height = atol(str_thumb_height);
				
				if (NULL != str_thumb_software && 0 == strcmp("GNOME::ThumbnailFactory",str_thumb_software))
				{
					// this is to work around a bug with the gnome thumbnail factory
					// not saving the correct width/height to the thumbnails
					save_thumbnail_to_cache = TRUE;
				}
				else if (-1 == m_iWidth || -1 == m_iHeight)
				{
					if (0 < new_width && 0 < new_height)
					{
						m_iWidth =  new_width;
						m_iHeight = new_height;
					}
					else
					{
						save_thumbnail_to_cache = TRUE;	
					}
				}
			}
		}
	}
	
	if (NULL == thumb_pixbuf && 128 <= thumbSize->size) // no thumbnail in the ~/.thumbnails/ cache
	{ 
		// unable to load from file, next try exif
		thumb_pixbuf = GetExifThumbnail();
		if (NULL != thumb_pixbuf)
		{
			guint size = thumbSize->size;
			guint pixbuf_width = gdk_pixbuf_get_width(thumb_pixbuf);
			guint pixbuf_height = gdk_pixbuf_get_height(thumb_pixbuf);
			// resize it to the proper size
			// check the ratio of the exif thumbnail
			double thumb_ratio = pixbuf_width / (double)pixbuf_height;
			double actual_ratio = GetWidth() / (double)GetHeight();

			if (thumb_ratio == actual_ratio && (pixbuf_width >= size || pixbuf_height >= size))
			{
				quiver_rect_get_bound_size(size,size, &pixbuf_width,&pixbuf_height,FALSE);

				GdkPixbuf* newpixbuf = gdk_pixbuf_scale_simple (
									thumb_pixbuf,
									pixbuf_width,
									pixbuf_height,
									GDK_INTERP_BILINEAR);
									//GDK_INTERP_NEAREST);
				g_object_unref(thumb_pixbuf);
				thumb_pixbuf = newpixbuf;
			}
			else
			{
				g_object_unref(thumb_pixbuf);
				thumb_pixbuf = NULL;
			}
		}
	}

	// we couldn't get it from the cache, or the exif, so we'll have to load it straight from the file
	if (NULL == thumb_pixbuf)
	{
		int size = thumbSize->size;

		result = gnome_vfs_open (&handle, m_szURI, GNOME_VFS_OPEN_READ);
		if (GNOME_VFS_OK == result)
		{
			PixbufLoaderSizeInfo size_info = {0};
			size_info.size_request = size;			

			GdkPixbufLoader* loader = NULL;
			loader = gdk_pixbuf_loader_new ();	

			if (NULL != loader)
			{
				g_signal_connect (loader,"size-prepared",G_CALLBACK (pixbuf_loader_size_prepared), &size_info);	
	
				while (GNOME_VFS_OK == result) 
				{
					result = gnome_vfs_read (handle, buffer, sizeof(buffer), &bytes_read);
					tmp_error = NULL;
					
					if (!gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error))
					{
						//printf("error: %s\n",tmp_error->message);
						break;
					}
				}

				gdk_pixbuf_loader_close(loader, NULL);
								
				thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
				
				if (NULL != thumb_pixbuf)
					g_object_ref(thumb_pixbuf);
				
				g_object_unref(loader);		
			}
			
			gnome_vfs_close(handle);

		
			if (NULL != thumb_pixbuf)
			{
				// this is just in case we are browsing through the .thumbnail folders
				// this way it will show the thumbnail at the correct orientation
				//printf("checking for orientation\n");
				const gchar* str_orientation = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::Image::Orientation");
			
				if (NULL != str_orientation)
				{
					//printf("we got orientation: %s\n",str_orientation);
					m_iOrientation = atoi(str_orientation);
					
				}
				//printf("got thumb from file\n");
			}
			
			if (-1 == m_iWidth || -1 == m_iHeight)
			{
				m_iWidth = size_info.width;
				m_iHeight = size_info.height;
			}
			
			if (size_info.width <= size_info.size_request && size_info.height <= size_info.size_request )
			{
				// size of image is smaller than size requested so
				// we do not need to cache it
				save_thumbnail_to_cache = FALSE;
			}			
		}

	}
	
	gchar *base_thumb_dir = g_build_filename( g_get_home_dir(), ".thumbnails",NULL);
	
	// user may be browsing the thumbnail directory. 
	// if so, we don't want "normal" thumbnails to be saved
	// for images in the large directoryTimer
	if (save_thumbnail_to_cache)
	{
		std::string filepath = GetFilePath();
		
		if (NULL != strstr(filepath.c_str(),base_thumb_dir) )
		{
			save_thumbnail_to_cache = FALSE; 
		}
	}

	g_free(base_thumb_dir);
		
	// FIXME: maybe this should be in a low priority thread?
	// save the thumbnail to the cache directory (~/.thumbnails)
	if (NULL != thumb_pixbuf && save_thumbnail_to_cache && vfsFileInfo)
	{
		// make the directory if it does not already exist:
		gchar *thumb_dir = g_path_get_dirname(thumb_path);
		g_mkdir_with_parents(thumb_dir,S_IRUSR|S_IWUSR|S_IXUSR);
		g_free(thumb_dir);
		
		gchar *temp_file_name = g_strconcat (thumb_path, ".XXXXXX", NULL);
		gint fhandle = g_mkstemp (temp_file_name);
		
		if (-1 != fhandle )
		{
			close (fhandle);
			gchar str_mtime[32];
			gchar str_width[32];
			gchar str_height[32];
			gchar str_orientation[2];
			//printf("temp_file_name = %s -> %s\n",temp_file_name,m_szURI);
			g_snprintf (str_mtime, 30, "%lu",  vfsFileInfo->mtime);

			g_snprintf (str_width, 32, "%d",  GetWidth());
			g_snprintf (str_height, 32, "%d",  GetHeight());
			
			g_snprintf (str_orientation, 2, "%d",  GetOrientation());


			//printf("orientation to save: %s\n",str_orientation);
			
			gboolean saved = gdk_pixbuf_save (thumb_pixbuf,
					   temp_file_name,
					   "png", NULL, 
					   "tEXt::Thumb::URI", m_szURI,
					   "tEXt::Thumb::MTime", str_mtime,
					   "tEXt::Thumb::Image::Orientation", str_orientation,
					   "tEXt::Thumb::Image::Width", str_width,
					   "tEXt::Thumb::Image::Height", str_height,
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
	
	if (NULL != thumb_pixbuf  && 1 < GetOrientation()) 
	{
		GdkPixbuf * new_pixbuf = QuiverUtils::GdkPixbufExifReorientate(thumb_pixbuf, GetOrientation());
		if (NULL != new_pixbuf)
		{
			g_object_unref(thumb_pixbuf);
			thumb_pixbuf = new_pixbuf;
		}
	}




	if (NULL != thumb_pixbuf)
	{
		c_ThumbnailCache.m_mapThumbnailCache[thumbSize->size]->AddPixbuf(m_szURI,thumb_pixbuf);
		m_mapThumbnailExists[thumbSize->size] = true;
	}

	return thumb_pixbuf;
}


void QuiverFile::QuiverFileImpl::Reload()
{
	std::string strURI = m_szURI;
	
	gnome_vfs_file_info_unref(m_vfsFileInfo);
	
	delete [] m_szURI;
	m_szURI = NULL;

	if (NULL != m_pExifData)
	{
		exif_data_unref(m_pExifData);
		m_pExifData = NULL;
	}
	
	unsigned int n_elements = G_N_ELEMENTS(ThumbnailSizes);
	for (unsigned int i = 0 ; i < n_elements; i++)
	{
		c_ThumbnailCache.m_mapThumbnailCache[ThumbnailSizes[i].size]->RemovePixbuf(strURI);
			
	}

	// get the file info
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (strURI.c_str(),info,(GnomeVFSFileInfoOptions)
									  (GNOME_VFS_FILE_INFO_DEFAULT|
									  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
									  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
									  GNOME_VFS_FILE_INFO_FOLLOW_LINKS|
									  GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS)
 
									  );
	Init(strURI.c_str(),info);
	gnome_vfs_file_info_unref(info);
	
}


void QuiverFile::QuiverFileImpl::LoadExifData()
{
	//Timer t("QuiverFile::LoadExifData()");
	if (NULL != m_szURI && !( m_fDataLoaded & QUIVER_FILE_DATA_EXIF ) )
	{
		ExifLoader *loader;
		//	unsigned char data[1024];
	
		loader = exif_loader_new ();
	
		unsigned char buffer[2048];
		
		GnomeVFSHandle   *handle;
		GnomeVFSResult    result;
		GnomeVFSFileSize  bytes_read;
		
		result = gnome_vfs_open (&handle, m_szURI, GNOME_VFS_OPEN_READ);
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
		
		m_pExifData = exif_loader_get_data (loader);
		exif_loader_unref (loader);
		
		if (NULL == m_pExifData)
		{
			//printf("exif data is null\n");
		}
		else
		{
			bool no_exif = true;
			
			// go through exif fields to see if any are set (if not, no exif)
			for (int i = 0; i < EXIF_IFD_COUNT && no_exif; i++)
			{
				if (m_pExifData->ifd[i] && m_pExifData->ifd[i]->count)
				{
					no_exif = false;
				}
			}
			
			if (m_pExifData->data)
			{
				no_exif = false;
			}
			
			if (!no_exif)
			{
				m_fDataExists = (QuiverDataFlags)(m_fDataExists | QUIVER_FILE_DATA_EXIF);
				//printf("there is exif!!\n");
			}
			else
			{
				//printf("no data in the exif!\n");
				// dont keep structure around, it has nothing in it
				exif_data_unref(m_pExifData);
				m_pExifData = NULL;
			}
	
		}
		
		if ( NULL != m_pExifData)
		{
			m_pExifDataOriginal = m_pExifData;
			exif_data_ref(m_pExifDataOriginal);
		}
		
		m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_EXIF);
	}
}

ExifData* QuiverFile::QuiverFileImpl::GetExifData()
{
	LoadExifData();
	return m_pExifData;
}

time_t QuiverFile::QuiverFileImpl::GetTimeT()
{
	time_t date = 0;
	ExifData* pExifData = GetExifData();
	if (NULL != pExifData)
	{
		// use date_time_original
		ExifEntry* pEntry;
		pEntry = exif_data_get_entry(pExifData,EXIF_TAG_DATE_TIME_ORIGINAL);
		if (NULL != pEntry)
		{
			char szDate[20];
			exif_entry_get_value(pEntry,szDate,20);

			tm tm_exif_time;
			int num_substs = sscanf(szDate,"%04d:%02d:%02d %02d:%02d:%02d",
				&tm_exif_time.tm_year,
				&tm_exif_time.tm_mon,
				&tm_exif_time.tm_mday,
				&tm_exif_time.tm_hour,
				&tm_exif_time.tm_min,
				&tm_exif_time.tm_sec);
			tm_exif_time.tm_year -= 1900;
			tm_exif_time.tm_mon -= 1;
			if (6 == num_substs)
			{
				// successfully parsed date
				date = timelocal(&tm_exif_time);
			}
			
		}
	}
	

	if (0 == date) // unable to get exif date	
	{
		// use ctime or mtime
		GnomeVFSFileInfo *pInfo = GetFileInfo();
		
		if (NULL != pInfo)
		{
			if (GNOME_VFS_FILE_INFO_FIELDS_CTIME & pInfo->valid_fields)
			{
				date = pInfo->ctime;
			}
			else if (GNOME_VFS_FILE_INFO_FIELDS_MTIME & pInfo->valid_fields)
			{
				date = pInfo->mtime;
			}
		}
	}
	
	return date;
}	



bool QuiverFile::QuiverFileImpl::SetExifData(ExifData* pExifData)
{
	bool bSet = false;
	bool bModified = false;
	
	LoadExifData();
	
	if (NULL != pExifData && NULL != m_pExifDataOriginal)
	{
		unsigned char *data1 = NULL;
		unsigned char *data2 = NULL;
		unsigned int  size1, size2;

		exif_data_save_data(pExifData,&data1,&size1);
		exif_data_save_data(m_pExifDataOriginal,&data2,&size2);

		if (size1 != size2)
		{
			bModified = true;
		}
		else if (0 != memcmp(data1,data2, size1))
		{
			bModified = true;
		}
		
		int iOldOrientation = GetOrientation();
		
		if (bModified)
		{
			ExifData *pExifDataCopy = exif_data_new_from_data(data1,size1);
			
			exif_data_unref(m_pExifData);
			
			m_pExifData = pExifDataCopy;
			
			// set the modified flag
			m_fDataModified = (QuiverDataFlags)(m_fDataModified|QUIVER_FILE_DATA_EXIF);
		}
		else
		{
			exif_data_unref(m_pExifData);
			
			m_pExifData = m_pExifDataOriginal;
			
			exif_data_ref(m_pExifData);
			
			// unset the modified flag
			if (QUIVER_FILE_DATA_EXIF & m_fDataModified)
			{
				m_fDataModified = (QuiverDataFlags)(m_fDataModified ^ QUIVER_FILE_DATA_EXIF );
			}
		}
		
		if (GetOrientation() != iOldOrientation)
		{
			// remove cached thumbnails because they are cached
			// at a certain orientation and that has now changed
			unsigned int n_elements = G_N_ELEMENTS(ThumbnailSizes);
			for (unsigned int i = 0 ; i < n_elements; i++)
			{
				c_ThumbnailCache.m_mapThumbnailCache[ThumbnailSizes[i].size]->RemovePixbuf(m_szURI);
					
			}
		}
		
		
		free(data1);
		free(data2);
		
		
		bSet = true;
	}
	return bSet;
}

bool QuiverFile::QuiverFileImpl::Modified() const
{
	return (0 != m_fDataModified);
}

std::string QuiverFile::QuiverFileImpl::GetFilePath() const
{
	std::string s;
	if (GNOME_VFS_FILE_INFO_LOCAL (m_vfsFileInfo) )
	{
		char * uri = gnome_vfs_get_local_path_from_uri (m_szURI);
		s = uri;
		g_free(uri);
	}
	else
	{
		s = m_szURI;
	}
	return s;
}


int QuiverFile::QuiverFileImpl::GetWidth()
{
	if (-1 == m_iWidth)
	{
		GetImageDimensions(m_szURI, &m_iWidth, &m_iHeight);
	}
	return m_iWidth;
}

int QuiverFile::QuiverFileImpl::GetHeight()
{
	if (-1 == m_iHeight)
	{
		GetImageDimensions(m_szURI, &m_iWidth, &m_iHeight);
	}
	return m_iHeight;
}


int QuiverFile::QuiverFileImpl::GetOrientation()
{
	LoadExifData();

	// if we have loaded the exif data or the orientation flag is set,
	// check the exif data for the orientation value.
	// otherwise just return the cached value
	if (0 == m_iOrientation || (m_fDataLoaded & QUIVER_FILE_DATA_EXIF))
	{
		if (m_fDataExists & QUIVER_FILE_DATA_EXIF)
		{
			ExifData *pExifData = GetExifData();
			int orientation = 1;
			
			ExifEntry *entry;
			entry = exif_data_get_entry(pExifData,EXIF_TAG_ORIENTATION);
			if (NULL != entry)
			{
				// the orientation field is set
				//exif_entry_dump(entry,2);
				ExifByteOrder o;
				o = exif_data_get_byte_order (entry->parent->parent);
				orientation = exif_get_short (entry->data, o);
			}

			m_iOrientation = orientation;
		}
	}
	
	if (0 == m_iOrientation)
	{
		m_iOrientation = 1;
	}
	
	return m_iOrientation;
}

// =================================================================================================
// QuiverFile Wrapper Class
// =================================================================================================

QuiverFile::QuiverFile() : m_QuiverFilePtr( new QuiverFileImpl(NULL) )
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

bool QuiverFile::Modified() const
{
	return m_QuiverFilePtr->Modified();
}

bool QuiverFile::operator== (const QuiverFile &other) const
{
	bool bMatch = false;
	if (NULL != GetURI() && NULL != other.GetURI())
	{
		bMatch = gnome_vfs_uris_match(GetURI(),other.GetURI());
	}
	else if (NULL == GetURI() && NULL == other.GetURI())
	{
		bMatch = true;
	}
	return bMatch;
}

bool QuiverFile::operator!= (const QuiverFile &other) const
{
	return !operator==(other);
}

const char* QuiverFile::GetMimeType()
{
	return m_QuiverFilePtr->GetMimeType();
}

GnomeVFSFileInfo* QuiverFile::GetFileInfo()
{
	GnomeVFSFileInfo* vfsFileInfo = NULL;
	vfsFileInfo = m_QuiverFilePtr->GetFileInfo();
	if (NULL != vfsFileInfo)
	{
		gnome_vfs_file_info_ref(vfsFileInfo);
	}
	return vfsFileInfo;
}

unsigned long long QuiverFile::GetFileSize()
{
	long long size = 0;
	GnomeVFSFileInfo *info = GetFileInfo();
	if (NULL != info)
	{
		size = info->size;
		gnome_vfs_file_info_unref(info);
	}
	return size;
}

std::string QuiverFile::GetFilePath() const
{
	return m_QuiverFilePtr->GetFilePath();
}

const gchar* QuiverFile::GetURI() const
{
	return m_QuiverFilePtr->m_szURI;
}


void QuiverFile::SetLoadTimeInSeconds(double seconds)
{
	m_QuiverFilePtr->m_dLoadTimeSeconds = seconds;
}

double QuiverFile::GetLoadTimeInSeconds() const
{
	return m_QuiverFilePtr->m_dLoadTimeSeconds;
}

void QuiverFile::SetWidth(int w)
{
	m_QuiverFilePtr->m_iWidth = w;
}
void QuiverFile::SetHeight(int h)
{
	m_QuiverFilePtr->m_iHeight = h;
}

void QuiverFile::Reload()
{
	m_QuiverFilePtr->Reload();
}

static void pixbuf_loader_size_prepared (GdkPixbufLoader *loader, gint width,
                                            gint             height,
                                            gpointer         user_data)
{
	PixbufLoaderSizeInfo* loader_size = (PixbufLoaderSizeInfo*)user_data;
	
	int size = loader_size->size_request;
	if (size != 0)
	{
		guint new_width,new_height;
		new_width = width;
		new_height = height;

		quiver_rect_get_bound_size(size, size, &new_width, &new_height, FALSE);

		if ((guint)width != new_height && (guint)height != new_height)
		{
			gdk_pixbuf_loader_set_size(loader,new_width,new_height);
		}
	}
	loader_size->width = width;
	loader_size->height = height;
	
}


static void GetImageDimensions(const gchar *uri, gint *width, gint *height)
{
	GError *tmp_error;
	tmp_error = NULL;
	
	gchar buffer[512];
	GnomeVFSHandle   *handle;
	GnomeVFSResult    result;
	GnomeVFSFileSize  bytes_read;
	result = gnome_vfs_open (&handle, uri, GNOME_VFS_OPEN_READ);
	if (GNOME_VFS_OK == result)
	{
		PixbufLoaderSizeInfo size_info = {0};
		
		GdkPixbufLoader* loader = NULL;
		loader = gdk_pixbuf_loader_new ();	
		
		if (NULL != loader)
		{
			g_signal_connect (loader,"size-prepared",G_CALLBACK (pixbuf_loader_size_prepared), &size_info);	
			
			while (GNOME_VFS_OK == result)
			{
				result = gnome_vfs_read (handle, buffer, 
										 sizeof(buffer), &bytes_read);
				gboolean success;
				success = gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);
				if (!success)
				{
					break;
				}
				if (0 != size_info.width && 0 != size_info.height)
				{
					break;
				}
			}
			
			gdk_pixbuf_loader_close(loader, NULL);

			if (0 != size_info.width && 0 != size_info.height)
			{
				*width = size_info.width; 
				*height = size_info.height;
			}
			
			g_object_unref(loader);
		}
		gnome_vfs_close(handle);
	}
}
int QuiverFile::GetWidth()
{
	return m_QuiverFilePtr->GetWidth();
}

int QuiverFile::GetHeight()
{
	return m_QuiverFilePtr->GetHeight();
}	

bool QuiverFile::IsWriteable()
{
	bool rval = false;

	GnomeVFSFileInfo* vfsFileInfo = GetFileInfo();
	if (NULL != vfsFileInfo)
	{
		if (GNOME_VFS_FILE_INFO_FIELDS_ACCESS & vfsFileInfo->valid_fields)
		{
			if (GNOME_VFS_PERM_ACCESS_WRITABLE & vfsFileInfo->permissions)
			{
				rval = true;
			}
		}

		gnome_vfs_file_info_unref(vfsFileInfo);
	}
	return rval;
}

time_t QuiverFile::GetTimeT() const
{
	return m_QuiverFilePtr->GetTimeT();
}

GdkPixbuf * QuiverFile::GetExifThumbnail()
{
	return m_QuiverFilePtr->GetExifThumbnail();
}

bool QuiverFile::HasThumbnail(int iSize )
{
	return m_QuiverFilePtr->HasThumbnail(iSize);
}

GdkPixbuf * QuiverFile::GetThumbnail(int iSize /* = 0 */)
{
	return m_QuiverFilePtr->GetThumbnail(iSize);
}

gchar* QuiverFile::GetIconName()
{
	gchar *icon_name = NULL;


	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

	GnomeVFSFileInfo *file_info = GetFileInfo();
	
	if (NULL == file_info)
		return NULL;
	
#ifdef QUIVER_MAEMO
	gchar** icon_names = osso_mime_get_icon_names(file_info->mime_type, file_info);
	
	gint i = 0;
	for (i = 0; NULL != icon_names[i]; i++)
	{
		if (gtk_icon_theme_has_icon(icon_theme, icon_names[i]))
		{
			break;
		}
	}
	icon_name = g_strdup(icon_names[i]);
	g_strfreev(icon_names);

#else
	GnomeIconLookupResultFlags result;
	icon_name = gnome_icon_lookup (icon_theme,
										 NULL,
										 GetURI(),
										 NULL,
										 file_info,
										 file_info->mime_type,
										 GNOME_ICON_LOOKUP_FLAGS_NONE,
										 &result);
#endif
	gnome_vfs_file_info_unref(file_info);
	return icon_name;
}

GdkPixbuf* QuiverFile::GetIcon(int width_desired,int height_desired)
{
	GdkPixbuf* pixbuf = NULL;
	GError *error = NULL;
	gchar *icon_name = NULL;


	icon_name = GetIconName();
	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

	if (NULL != icon_name)
	{
		gint* sizes_orig = gtk_icon_theme_get_icon_sizes   (icon_theme, icon_name);
		gint* sizes = sizes_orig;

		gint size_wanted = MIN(width_desired,height_desired);
		
		gint size;
		gint size_to_get = 0;
		gint min_size = G_MAXINT;
		while (0 != (size = *sizes))
		{
			min_size = MIN(min_size, size);
			if (size <= size_wanted)
			{
				size_to_get = MAX(size_to_get,size);
			}
			sizes++;
		}
		g_free(sizes_orig);

		if (0 == size_to_get)
		{
			size_to_get = min_size;
		}

		pixbuf = gtk_icon_theme_load_icon (icon_theme,
												 icon_name,
												 size_to_get,
												 GTK_ICON_LOOKUP_USE_BUILTIN,
												 &error);
		g_free(icon_name);
	}
	return pixbuf;
}


ExifData* QuiverFile::GetExifData()
{
	// return a copy of the exif data
	ExifData* pExifData = m_QuiverFilePtr->GetExifData();
	ExifData* pExifDataCopy = NULL;
	if (pExifData)
	{
		unsigned char *data = NULL;
		unsigned int  size;

		exif_data_save_data(pExifData,&data,&size);
		pExifDataCopy = exif_data_new_from_data(data,size);
		free(data);
	}
	return pExifDataCopy;
}

bool QuiverFile::SetExifData(ExifData* pExifData)
{
	return m_QuiverFilePtr->SetExifData(pExifData);
}


int QuiverFile::GetOrientation()
{
	return m_QuiverFilePtr->GetOrientation();
}


void QuiverFile::RemoveCachedThumbnail(int iSize /* = 0*/)
{
	ThumbnailSize* thumbSize = NULL;
	
	unsigned int n_elements = G_N_ELEMENTS(ThumbnailSizes);
	for (unsigned int i = 0 ; i < n_elements; i++)
	{
		if (-1 == iSize)
		{
			// remove all
			m_QuiverFilePtr->c_ThumbnailCache.m_mapThumbnailCache[ThumbnailSizes[i].size]->RemovePixbuf(m_QuiverFilePtr->m_szURI);
			gchar * thumb_path ;
			thumb_path = quiver_thumbnail_path_for_uri(m_QuiverFilePtr->m_szURI,ThumbnailSizes[i].name);
			g_remove(thumb_path);
			g_free (thumb_path);
			
		}
		else if (iSize <= ThumbnailSizes[i].size || i == n_elements - 1 )
		{
			thumbSize = &ThumbnailSizes[i];
			break;
		}
	}
	
	if (-1 != iSize)
	{
		m_QuiverFilePtr->c_ThumbnailCache.m_mapThumbnailCache[thumbSize->size]->RemovePixbuf(m_QuiverFilePtr->m_szURI);
	
		gchar * thumb_path ;
		thumb_path = quiver_thumbnail_path_for_uri(m_QuiverFilePtr->m_szURI,thumbSize->name);
		g_remove(thumb_path);
		g_free (thumb_path);
	}
}

gchar* quiver_thumbnail_path_for_uri(const char* uri, const char* szSize)
{
	
	unsigned char digest[16];
	gcry_md_hash_buffer (GCRY_MD_MD5, digest, uri, strlen(uri));
	
	char szMD5Hash[37];
	g_snprintf(szMD5Hash,37,"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x.png",
	           digest[0], digest[1], digest[2], digest[3], digest[4],
	           digest[5], digest[6], digest[7], digest[8], digest[9],
    	       digest[10], digest[11], digest[12], digest[13],
        	   digest[14], digest[15]);
	

	return g_build_filename( g_get_home_dir(), ".thumbnails", szSize, szMD5Hash, NULL);
}


