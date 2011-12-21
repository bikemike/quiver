#include <config.h>

#include <glib.h>

#define GDK_PIXBUF_ENABLE_BACKEND
#include <gdk-pixbuf/gdk-pixbuf-io.h>


#ifdef QUIVER_MAEMO
#ifdef HAVE_HILDON_MIME
#include <hildon-mime.h>
#else
#include <osso-mime.h>
#endif
#endif

#include <libexif/exif-utils.h>
#include <glib/gstdio.h>

#include <string>
#include <string.h>

#include "QuiverFile.h"
#include "Timer.h"
#include "QuiverUtils.h"
#include "QuiverVideoOps.h"
#include "ImageCache.h"
#include "MD5.h"

#include <map>
#include <cmath>


#include <libquiver/quiver-pixbuf-utils.h>


#include <gst/gst.h>
#include <gst/video/video.h>

// =================================================================================================
// Implementation
// =================================================================================================
static void thread_save_thumbnail(gpointer data, gpointer user_data);

typedef struct _ThumbnailSize
{
	int size;
	const char* name;
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

	void Clear()
	{
		std::map<int,ImageCache*>::iterator itr;
		for (itr = m_mapThumbnailCache.begin(); itr != m_mapThumbnailCache.end(); ++itr)
		{
			itr->second->Clear();
		}
	}
	
	~ThumbnailCache()
	{
		std::map<int,ImageCache*>::iterator itr;
		for (itr = m_mapThumbnailCache.begin(); itr != m_mapThumbnailCache.end(); ++itr)
		{
			delete itr->second;
		}
		m_mapThumbnailCache.clear();
	};
	std::map<int,ImageCache*> m_mapThumbnailCache;
};

struct IptcData;
struct DBData;

class QuiverFile::QuiverFileImpl {

public:
// Methods
	
	QuiverFileImpl(const gchar*  uri);
	QuiverFileImpl(const gchar* , GFileInfo *info);
	~QuiverFileImpl();

	void Init(const gchar *uri, GFileInfo *info);
	void Reload();
	
	const char* GetMimeType();
	GFileInfo* GetFileInfo();
	
	int GetWidth();
	int GetHeight();
	
	void GetVideoDimensions(gint *width, gint *height);

	std::string GetFilePath() const;

	void LoadExifData();
	int GetOrientation();
	time_t GetTimeT(bool fromExif = true);
	
	ExifData* GetExifData();
	bool SetExifData(ExifData* pExifData);	
	GdkPixbuf* GetExifThumbnail();
	
	bool HasThumbnail(int iSize) ;
	GdkPixbuf* GetThumbnail(int iSize = 0);
	void SaveThumbnail(GdkPixbuf* pixbuf, const char* uri, const char* path, time_t mtime, int width, int height, int orientation);
	
	bool Modified() const;
	bool IsVideo();
	
// variables	
	gchar* m_szURI;
	gchar* m_szMimeType;
	GFileInfo* m_gFileInfo;
 	
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

	static boost::shared_ptr<GThreadPool> c_ThreadPoolPtr;
};

class ThreadPoolDestructor
{
public:
	void operator()(GThreadPool *thread_pool)
	{
		g_thread_pool_free(thread_pool, FALSE, TRUE);
	}
};

class ThumbnailSaveThreadData
{
public:
	ThumbnailSaveThreadData(GdkPixbuf* pixbuf, const char* uri, const char* path, time_t mtime, int width, int height, int orientation)
	{
		m_pPixbuf = gdk_pixbuf_copy(pixbuf);
		m_strURI = uri;
		m_strPath = path;
		m_mtime = mtime;
		m_iWidth = width;
		m_iHeight = height;
		m_iOrientation = orientation;
	}
	GdkPixbuf* m_pPixbuf;
	std::string m_strURI;
	std::string m_strPath;
	time_t m_mtime;
   	int m_iWidth;
   	int m_iHeight;
   	int m_iOrientation;
};

boost::shared_ptr<GThreadPool> QuiverFile::QuiverFileImpl::c_ThreadPoolPtr;

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
	Init(uri, NULL);
}

QuiverFile::QuiverFileImpl::QuiverFileImpl(const gchar *uri, GFileInfo *info)
{
	Init(uri, info);
}

void QuiverFile::QuiverFileImpl::Init(const gchar *uri, GFileInfo *info)
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

	m_gFileInfo = info;

	m_fDataLoaded = QUIVER_FILE_DATA_NONE;
	m_fDataExists = QUIVER_FILE_DATA_NONE;
	
	if (NULL != m_gFileInfo)
	{
		g_object_ref(m_gFileInfo);
		m_fDataExists = (QuiverDataFlags)(m_fDataExists | QUIVER_FILE_DATA_INFO);
		m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_INFO);
	}
	else
	{
		GetFileInfo();
	}
	
	m_szMimeType = NULL;
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
	if (NULL == m_szMimeType)
	{
		GFileInfo *pInfo = GetFileInfo();
		
		if (NULL != pInfo)
		{
			const char* pContentType = g_file_info_get_content_type(pInfo);
			m_szMimeType = g_content_type_get_mime_type(pContentType);
		}
	}

	return m_szMimeType;
}

GFileInfo* QuiverFile::QuiverFileImpl::GetFileInfo()
{
	GFileInfo* gFileInfo = NULL;

	if ((m_fDataExists & QUIVER_FILE_DATA_INFO) && m_gFileInfo)
	{
		gFileInfo = m_gFileInfo;
	}
	
	if (!(m_fDataLoaded & QUIVER_FILE_DATA_INFO) && NULL != m_szURI && NULL == m_gFileInfo)
	{
		GFile* file = g_file_new_for_uri(m_szURI);
		gFileInfo = g_file_query_info(file,
			G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
				G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
				G_FILE_ATTRIBUTE_STANDARD_SIZE ","
				G_FILE_ATTRIBUTE_STANDARD_ICON ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH ","
				G_FILE_ATTRIBUTE_TIME_MODIFIED ","
				G_FILE_ATTRIBUTE_TIME_CREATED,
			G_FILE_QUERY_INFO_NONE,
			NULL,
			NULL);
		g_object_unref(file);

		if (NULL != gFileInfo)
		{
			m_gFileInfo = gFileInfo;
			
			m_fDataExists = (QuiverDataFlags)(m_fDataExists | QUIVER_FILE_DATA_INFO);
			m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_INFO);
		}
	}
	
	return gFileInfo;
}

QuiverFile::QuiverFileImpl::~QuiverFileImpl()
{
	if (m_gFileInfo != NULL)
	{
		g_object_unref(m_gFileInfo);
	}
	
	if (NULL != m_szURI)
	{
		delete [] m_szURI;
	}

	if (NULL != m_szMimeType)
	{
		g_free(m_szMimeType);
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
		GdkPixbufLoader *pixbuf_loader;
		
		pixbuf_loader = NULL;
		pixbuf_loader = gdk_pixbuf_loader_new();
		if (NULL != pixbuf_loader)
		{
			gdk_pixbuf_loader_write (pixbuf_loader,(guchar*)pExifData->data, pExifData->size, NULL);
	
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

static void get_thumbnail_embedded_size(GdkPixbuf* pixbuf, gint *width, gint *height)
{
	*width = -1;
	*height = -1;

	const gchar* str_thumb_width = gdk_pixbuf_get_option (pixbuf, "tEXt::Thumb::Image::Width");
	const gchar* str_thumb_height = gdk_pixbuf_get_option (pixbuf, "tEXt::Thumb::Image::Height");
	
	// if we didn't get the width and height we should resave thumbnail
	// with this information 
	if (NULL != str_thumb_width && NULL != str_thumb_height)
	{
		*width  = atol(str_thumb_width);
		*height = atol(str_thumb_height);
	}

}

GdkPixbuf * QuiverFile::QuiverFileImpl::GetThumbnail(int iSize /* = 0 */)
{
	//Timer t("QuiverFileImpl::GetThumbnail");
	GFileInfo* gFileInfo = GetFileInfo();
	
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
	int buffsize = 65536;
	guchar buffer[buffsize];
	gssize bytes_read;
	GError *tmp_error = NULL;

	GFile* gfile = g_file_new_for_path(thumb_path);
	GInputStream* inStream = G_INPUT_STREAM(g_file_read(gfile, NULL,NULL));

	if (NULL != inStream)
	{
		GdkPixbufLoader* loader = NULL;
		loader = gdk_pixbuf_loader_new ();
		
		if (NULL != loader)
		{

			while (0 < (bytes_read = g_input_stream_read(inStream, buffer, buffsize, NULL, NULL)))
			{
				gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);
				if (NULL != tmp_error)
				{
					g_error_free(tmp_error);
					tmp_error = NULL;
					break;
				}
			}
			gdk_pixbuf_loader_close(loader,NULL);
			
			thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
			
			if (NULL != thumb_pixbuf)
				g_object_ref(thumb_pixbuf);
			
			g_object_unref(loader);
		}
		
		g_object_unref(inStream);

		if (NULL != thumb_pixbuf)
		{
			const gchar* thumb_mtime_str = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::MTime");
			const gchar* str_orientation = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::Image::Orientation");

			const gchar* str_thumb_width = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::Image::Width");
			const gchar* str_thumb_height = gdk_pixbuf_get_option (thumb_pixbuf, "tEXt::Thumb::Image::Height");
			
			if (NULL != str_orientation)
			{
				//printf("we got orientation: %s\n",str_orientation);
				m_iOrientation = atoi(str_orientation);
			}
			
			if (NULL != thumb_mtime_str)
			{
				time_t mtime;
				mtime = atol (thumb_mtime_str);
				
				GTimeVal tv;
				g_file_info_get_modification_time(gFileInfo, &tv);

				if (tv.tv_sec != mtime)
				{
					// they dont match.. we should load a new version
					//printf("m-times do not match! %lu  != %lu\n", tv.tv_sec, mtime);
					g_object_unref(thumb_pixbuf);
					thumb_pixbuf = NULL;
				}
				else
				{
					//printf("m-times do match! %lu  == %lu\n", tv.tv_sec, mtime);
					save_thumbnail_to_cache = FALSE;
				}
			}

			if (NULL != thumb_pixbuf)
			{
				// if we didn't get the width and height we should resave thumbnail
				// with this information 
				if ((NULL == str_thumb_width || '\0' == str_thumb_width) || 
					( NULL == str_thumb_height || '\0' == str_thumb_height))
				{
					if (IsVideo())
					{
						// video thumbnail doens't have width/height
						// so create a new thumbnail
						g_object_unref(thumb_pixbuf);
						thumb_pixbuf = NULL;
					}
					save_thumbnail_to_cache = TRUE;
				}
				else 
				{
					int img_width  = atol(str_thumb_width);
					int img_height = atol(str_thumb_height);

					guint thumb_width  = gdk_pixbuf_get_width(thumb_pixbuf);
					guint thumb_height = gdk_pixbuf_get_height(thumb_pixbuf);
					// resize it to the proper size
					// check the ratio of the exif thumbnail
					double thumb_ratio = thumb_width / (double)thumb_height;
					double actual_ratio = img_width / (double)img_height;

					if (0.01 <  std::abs(actual_ratio - thumb_ratio))
					{
						//printf("wrong orientation\n");
						// looks like the thumbnail is not in 
						// the correct orientation so regenerate it
						g_object_unref(thumb_pixbuf);
						thumb_pixbuf = NULL;
					}
					else
					{
						if (-1 == m_iWidth || -1 == m_iHeight)
						{
							if (0 < img_width && 0 < img_height)
							{
								m_iWidth =  img_width;
								m_iHeight = img_height;
							}
							else
							{
								// thumb has an invalid size. resave it
								if (IsVideo())
								{
									// video thumbnail has wrong dimensions
									// so create a new thumbnail
									g_object_unref(thumb_pixbuf);
									thumb_pixbuf = NULL;
								}
								save_thumbnail_to_cache = TRUE;	
							}
						}
					}
				}
			}
		}
	}

	g_object_unref(gfile);
	
	if (NULL == thumb_pixbuf && 128 >= thumbSize->size) // no thumbnail in the ~/.thumbnails/ cache
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

		if (IsVideo())
		{
			gint n=1, d=1;
			GdkPixbuf* video_pixbuf = QuiverVideoOps::LoadPixbuf(m_szURI, &n, &d);
			if (NULL != video_pixbuf)
			{
				guint pixbuf_width  = gdk_pixbuf_get_width(video_pixbuf);
				guint pixbuf_height = gdk_pixbuf_get_height(video_pixbuf);

				if (n > d)
					pixbuf_width = (guint)((pixbuf_width * n) / float(d) + .5);
				else
					pixbuf_height = (guint)((pixbuf_height * d) / float(n) + .5);

				m_iWidth = pixbuf_width;
				m_iHeight = pixbuf_height;

				if (pixbuf_width > size || pixbuf_height > size)
				{
					quiver_rect_get_bound_size(size,size, &pixbuf_width,&pixbuf_height,FALSE);
					thumb_pixbuf = gdk_pixbuf_scale_simple (
						video_pixbuf,
						pixbuf_width,
						pixbuf_height,
						GDK_INTERP_BILINEAR);

					g_object_unref(video_pixbuf);

					// add filmstrip to sides of thumbnail
					GdkPixbuf* filmholes = NULL;
					if (size < 256)
					{
						// FIXME: shouldn't load this every time
						filmholes = gdk_pixbuf_new_from_file(QUIVER_DATADIR "/filmholes.png", NULL);
					}
					else
					{
						// FIXME: shouldn't load this every time
						filmholes = gdk_pixbuf_new_from_file(QUIVER_DATADIR "/filmholes-big.png", NULL);
					}

					if (NULL != filmholes)
					{
						int w = gdk_pixbuf_get_width(filmholes);
						int h = gdk_pixbuf_get_height(filmholes);

						int dest_y = 0;

						//left
						while (dest_y < pixbuf_height)
						{
							if (dest_y + h > pixbuf_height)
								h = pixbuf_height - dest_y;

							gdk_pixbuf_composite(
								filmholes,
								thumb_pixbuf,
								0, // dest x
								dest_y, // dest y
								w, // dest w
								h, // dest h
								0., // offset x
								dest_y, // offset y
								1., // scale x
								1., // scale y
								GDK_INTERP_NEAREST,
								128);//alpha
							dest_y += h;
						}

						//right
						dest_y = 0;
						h = gdk_pixbuf_get_height(filmholes);
						while (dest_y < pixbuf_height)
						{
							if (dest_y + h > pixbuf_height)
								h = pixbuf_height - dest_y;

							gdk_pixbuf_composite(
								filmholes,
								thumb_pixbuf,
								pixbuf_width - w, // dest x
								dest_y, // dest y
								w, // dest w
								h, // dest h
								pixbuf_width - w, // offset x
								dest_y, // offset y
								1., // scale x
								1., // scale y
								GDK_INTERP_NEAREST,
								128);//alpha
							dest_y += h;
						}

						g_object_unref(filmholes);
					}
				}
				else
				{
					// size of image is smaller than size requested so
					// we do not need to cache it
					save_thumbnail_to_cache = FALSE;
					thumb_pixbuf = video_pixbuf;
				}
			}
		}
		else
		{
			gfile = g_file_new_for_uri(m_szURI);

			GInputStream* inStream = G_INPUT_STREAM(g_file_read(gfile, NULL,NULL));
			if (NULL != inStream)
			{
				PixbufLoaderSizeInfo size_info = {0};
				size_info.size_request = size;			

				GdkPixbufLoader* loader = NULL;
				loader = gdk_pixbuf_loader_new ();	

				if (NULL != loader)
				{
					g_signal_connect (loader,"size-prepared",G_CALLBACK (pixbuf_loader_size_prepared), &size_info);	
		
					while (0 < (bytes_read = g_input_stream_read(inStream, buffer, buffsize, NULL, NULL)))
					{
						tmp_error = NULL;
						
						gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);
						if (NULL != tmp_error)
						{
							printf("error: %s\n",tmp_error->message);
							g_error_free(tmp_error);
							break;
						}
					}

					gdk_pixbuf_loader_close(loader, NULL);
									
					thumb_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
					
					if (NULL != thumb_pixbuf)
						g_object_ref(thumb_pixbuf);
					
					g_object_unref(loader);		
				}
				
				g_object_unref(inStream);
		
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
			g_object_unref(gfile);
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
		
	// save the thumbnail to the cache directory (~/.thumbnails)
	if (NULL != thumb_pixbuf && save_thumbnail_to_cache && gFileInfo)
	{
		GTimeVal tv;
		g_file_info_get_modification_time(gFileInfo, &tv);
		gchar text_buff[20];
		g_sprintf(text_buff, "%d", GetWidth());
		gdk_pixbuf_set_option (thumb_pixbuf, "tEXt::Thumb::Image::Width", text_buff);
		g_sprintf(text_buff, "%d", GetHeight());
		gdk_pixbuf_set_option (thumb_pixbuf, "tEXt::Thumb::Image::Height", text_buff);
		SaveThumbnail(thumb_pixbuf, m_szURI, thumb_path, tv.tv_sec, GetWidth(), GetHeight(), GetOrientation());
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

void QuiverFile::QuiverFileImpl::SaveThumbnail(GdkPixbuf* pixbuf, const char* uri, const char* path, time_t mtime, int width, int height, int orientation)
{
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
	g_static_mutex_lock (&mutex);
	if (NULL == c_ThreadPoolPtr.get())
	{
		c_ThreadPoolPtr = boost::shared_ptr<GThreadPool> (
				g_thread_pool_new(thread_save_thumbnail, NULL, 1, FALSE, NULL),
				ThreadPoolDestructor()
				);
	}

	ThumbnailSaveThreadData *thread_data = new ThumbnailSaveThreadData(pixbuf, uri, path, mtime, width, height, orientation);

	g_thread_pool_push(c_ThreadPoolPtr.get(), thread_data, NULL);
	g_static_mutex_unlock (&mutex);
}

void QuiverFile::QuiverFileImpl::Reload()
{
	std::string strURI = m_szURI;
	
	if (NULL != m_gFileInfo)
	{
		g_object_unref(m_gFileInfo);
		m_gFileInfo = NULL;
	}
	
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

	Init(strURI.c_str(), NULL);
}


void QuiverFile::QuiverFileImpl::LoadExifData()
{
	if (NULL != m_szURI && !( m_fDataLoaded & QUIVER_FILE_DATA_EXIF ) )
	{
		ExifLoader *loader;
		//	unsigned char data[1024];
	
		loader = exif_loader_new ();
	
		int buffsize = 2048;
		unsigned char buffer[buffsize];
		gssize  bytes_read = 0;
		
		GFile* gfile = g_file_new_for_uri(m_szURI);

		GInputStream* inStream = G_INPUT_STREAM(g_file_read(gfile, NULL, NULL));

		if (NULL != inStream)
		{
			while (0 < (bytes_read = g_input_stream_read(inStream, buffer, buffsize, NULL, NULL)))
			{
				if (!exif_loader_write (loader, buffer, bytes_read))
				{
					break;
				}
			}
			g_object_unref(inStream);
		}
		
		g_object_unref(gfile);

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

time_t QuiverFile::QuiverFileImpl::GetTimeT(bool fromExif /* = true */)
{
	time_t date = 0;
	if (fromExif && !IsVideo())
	{
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

				struct tm tm_exif_time = {0};

				int num_substs = sscanf(szDate,"%04d:%02d:%02d %02d:%02d:%02d",
					&tm_exif_time.tm_year,
					&tm_exif_time.tm_mon,
					&tm_exif_time.tm_mday,
					&tm_exif_time.tm_hour,
					&tm_exif_time.tm_min,
					&tm_exif_time.tm_sec);

				tm_exif_time.tm_year -= 1900;
				tm_exif_time.tm_mon -= 1;
				tm_exif_time.tm_isdst = -1;

				if (6 == num_substs)
				{
					// successfully parsed date
					date = mktime(&tm_exif_time);
				}
				
			}
		}
	}

	if (0 == date) // unable to get exif date	
	{
		// use ctime or mtime
		GFileInfo *pInfo = GetFileInfo();
		
		if (NULL != pInfo)
		{
			GTimeVal tv;
			g_file_info_get_modification_time(pInfo, &tv);
			date = tv.tv_sec;
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

bool QuiverFile::QuiverFileImpl::IsVideo()
{
	return (g_strstr_len(GetMimeType(), 5, "video") == GetMimeType());
}

std::string QuiverFile::QuiverFileImpl::GetFilePath() const
{
	std::string s;

	GFile* file = g_file_new_for_uri(m_szURI);
	char* path = g_file_get_path(file);
	if (NULL != path)
	{
		s = path;
		g_free(path);
	}
	else
	{
		s = m_szURI;
	}
	g_object_unref(file);
	return s;
}


int QuiverFile::QuiverFileImpl::GetWidth()
{
	if (-1 == m_iWidth)
	{
		if (IsVideo())
			GetVideoDimensions(&m_iWidth, &m_iHeight);
		else
			GetImageDimensions(m_szURI, &m_iWidth, &m_iHeight);
	}
	return m_iWidth;
}

int QuiverFile::QuiverFileImpl::GetHeight()
{
	if (-1 == m_iHeight)
	{
		if (IsVideo())
			GetVideoDimensions(&m_iWidth, &m_iHeight);
		else
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

QuiverFile::QuiverFile(const gchar* uri, GFileInfo *info) : m_QuiverFilePtr( new QuiverFileImpl(uri,info) )
{
	
}
QuiverFile::~QuiverFile()
{
	
}

void QuiverFile::ClearThumbnailCache()
{
	QuiverFileImpl::c_ThumbnailCache.Clear();
}

bool QuiverFile::Modified() const
{
	return m_QuiverFilePtr->Modified();
}

bool QuiverFile::IsVideo()
{
	return m_QuiverFilePtr->IsVideo();
}

bool QuiverFile::operator== (const QuiverFile &other) const
{
	bool bMatch = false;
	if (NULL != GetURI() && NULL != other.GetURI())
	{
		GFile* file1 = g_file_new_for_uri(GetURI());
		GFile* file2 = g_file_new_for_uri(other.GetURI());

		bMatch = g_file_equal(file1, file2);

		g_object_unref(file1);
		g_object_unref(file2);
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

GFileInfo* QuiverFile::GetFileInfo()
{
	GFileInfo* fileInfo = NULL;
	fileInfo = m_QuiverFilePtr->GetFileInfo();
	if (NULL != fileInfo)
	{
		g_object_ref(fileInfo);
	}
	return fileInfo;
}

unsigned long long QuiverFile::GetFileSize()
{
	long long size = 0;
	GFileInfo *info = GetFileInfo();
	if (NULL != info)
	{
		size = g_file_info_get_size(info);
		g_object_unref(info);
	}
	return size;
}

std::string QuiverFile::GetFileName() const
{
	std::string s;
	GFile* gfile = g_file_new_for_uri(m_QuiverFilePtr->m_szURI);
	char* shortname = g_file_get_basename(gfile);
	s = shortname;
	g_free(shortname);
	g_object_unref(gfile);
	return s;
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


void QuiverFile::QuiverFileImpl::GetVideoDimensions(gint *width, gint *height)
{
	GdkPixbuf* pixbuf = GetThumbnail();
	if (NULL != pixbuf)
	{	
		get_thumbnail_embedded_size(pixbuf, width, height);
		g_object_unref(pixbuf);
	}
	else
	{
		gint n=1, d=1;
		GdkPixbuf* pixbuf = QuiverVideoOps::LoadPixbuf(m_szURI, &n, &d);
		if (NULL != pixbuf)
		{
			*width  = gdk_pixbuf_get_width(pixbuf);
			*height = gdk_pixbuf_get_height(pixbuf);

			if (n > d)
				*width = (guint)((*width * n) / float(d) + .5);
			else
				*height = (guint)((*height * d) / float(n) + .5);
		}

		g_object_unref(pixbuf);
	}
}

static void GetImageDimensions(const gchar *uri, gint *width, gint *height)
{
	GError *tmp_error = NULL;

	int buffsize = 512;
	gchar buffer[buffsize];

	gssize  bytes_read;

	GFile* gfile = g_file_new_for_uri(uri);
	GInputStream* inStream = G_INPUT_STREAM(g_file_read(gfile, NULL, NULL));
	
	if (NULL != inStream)
	{
		PixbufLoaderSizeInfo size_info = {0};
		
		GdkPixbufLoader* loader = NULL;
		loader = gdk_pixbuf_loader_new ();	
		
		if (NULL != loader)
		{
			g_signal_connect (loader,"size-prepared",G_CALLBACK (pixbuf_loader_size_prepared), &size_info);	
			
			while (0 < (bytes_read = g_input_stream_read(inStream, buffer, buffsize, NULL, NULL)))
			{
				gboolean success
					= gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);

				if (NULL != tmp_error)
				{
					g_error_free(tmp_error);
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
		g_object_unref(inStream);
	}
	g_object_unref(gfile);
}


bool QuiverFile::IsWidthHeightSet() const
{
	return (-1 != m_QuiverFilePtr->m_iWidth && -1 != m_QuiverFilePtr->m_iHeight);
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

	GFileInfo* gFileInfo = GetFileInfo();
	if (NULL != gFileInfo)
	{
		rval = g_file_info_get_attribute_boolean(gFileInfo, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE);
		g_object_unref(gFileInfo);
	}
	return rval;
}

time_t QuiverFile::GetTimeT(bool fromExif /* = true */) const
{
	return m_QuiverFilePtr->GetTimeT(fromExif);
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

	GFileInfo *file_info = GetFileInfo();
	
	if (NULL == file_info)
		return NULL;
	
#ifdef QUIVER_MAEMO
#ifdef HAVE_HILDON_MIME
	gchar** icon_names = hildon_mime_get_icon_names(file_info->mime_type, file_info);
#else
	gchar** icon_names = osso_mime_get_icon_names(file_info->mime_type, file_info);
#endif
	
	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

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
	const char* content_type = g_file_info_get_content_type(file_info);
	GIcon* icon = g_content_type_get_icon(content_type);
	if (NULL != icon)
	{
		icon_name = g_icon_to_string(icon);
		g_object_unref(icon);
	}
#endif
	g_object_unref(file_info);
	return icon_name;
}

GdkPixbuf* QuiverFile::GetIcon(int width_desired,int height_desired)
{
	GdkPixbuf* pixbuf = NULL;

	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

	gint size_wanted = MIN(width_desired,height_desired);
		
	GFileInfo* file_info = GetFileInfo();
	const char* content_type = g_file_info_get_content_type(file_info);
	GIcon* icon = g_content_type_get_icon(content_type);

	if (NULL != icon)
	{
		GtkIconInfo* icon_info = gtk_icon_theme_lookup_by_gicon (
			icon_theme,
			icon,
			size_wanted,
			GTK_ICON_LOOKUP_USE_BUILTIN);

		if (NULL != icon_info)
		{
			pixbuf = gtk_icon_info_load_icon(icon_info, NULL);
			gtk_icon_info_free(icon_info);
		}
		g_object_unref(icon);
	}

	g_object_unref(file_info);

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
	unsigned char* digest = NULL;
	MD5 md5((unsigned char*)uri);	
	digest = md5.raw_digest();
	
	char szMD5Hash[37];
	g_snprintf(szMD5Hash,37,"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x.png",
	           digest[0], digest[1], digest[2], digest[3], digest[4],
	           digest[5], digest[6], digest[7], digest[8], digest[9],
    	       digest[10], digest[11], digest[12], digest[13],
        	   digest[14], digest[15]);
	delete [] digest;

	return g_build_filename( g_get_home_dir(), ".thumbnails", szSize, szMD5Hash, NULL);
}


static void thread_save_thumbnail(gpointer data, gpointer user_data)
{
	ThumbnailSaveThreadData* thumb_data = (ThumbnailSaveThreadData*)data;
	// make the directory if it does not already exist:
	gchar *thumb_dir = g_path_get_dirname(thumb_data->m_strPath.c_str());
	g_mkdir_with_parents(thumb_dir,S_IRUSR|S_IWUSR|S_IXUSR);
	g_free(thumb_dir);
	
	gchar *temp_file_name = g_strconcat (thumb_data->m_strPath.c_str(), ".XXXXXX", NULL);
	gint fhandle = g_mkstemp (temp_file_name);
	
	if (-1 != fhandle )
	{
		close (fhandle);
		gchar str_mtime[32];
		gchar str_width[32];
		gchar str_height[32];
		gchar str_orientation[2];
		//printf("temp_file_name = %s -> %s\n",temp_file_name,m_szURI);
		g_snprintf (str_mtime, 30, "%lu",  thumb_data->m_mtime);

		g_snprintf (str_width, 32, "%d",  thumb_data->m_iWidth);
		g_snprintf (str_height, 32, "%d",  thumb_data->m_iHeight);
		
		g_snprintf (str_orientation, 2, "%d",  thumb_data->m_iOrientation);


		//printf("orientation to save: %s\n",str_orientation);
		
		gboolean saved = gdk_pixbuf_save (thumb_data->m_pPixbuf,
				   temp_file_name,
				   "png", NULL, 
				   "tEXt::Thumb::URI", thumb_data->m_strURI.c_str(),
				   "tEXt::Thumb::MTime", str_mtime,
				   "tEXt::Thumb::Image::Orientation", str_orientation,
				   "tEXt::Thumb::Image::Width", str_width,
				   "tEXt::Thumb::Image::Height", str_height,
				   "tEXt::Software", PACKAGE_STRING,
				   NULL);
		if (saved)
		{
			//printf("move: %s => %s \n",temp_file_name,thumb_data->m_strPath.c_str());
			g_chmod (temp_file_name, 0600);
			g_rename(temp_file_name, thumb_data->m_strPath.c_str());
		}
		else
		{
			g_remove(temp_file_name);
		}
	}
	g_free(temp_file_name);
	g_object_unref(thumb_data->m_pPixbuf);
	delete thumb_data;
	
}

