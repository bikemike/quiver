#include <config.h>

#include <glib.h>

#if HAVE_GDK_PIXBUF
#define GDK_PIXBUF_ENABLE_BACKEND
#include <gdk-pixbuf/gdk-pixbuf-io.h>
#endif
#include <libquiver/quiver-pixbuf-utils.h>

#include <exiv2/exiv2.hpp>
#include <glib/gstdio.h>

#include <string>
#include <string.h>
#include <vector>

#include "QuiverFile.h"
#include "Timer.h"
#include "QuiverUtils.h"
#include "QuiverVideoOps.h"
#include "ImageCache.h"
#include "ImageDecoder.h"
#include "MD5.h"

#include <map>
#include <cmath>
#include <mutex>
#include <boost/algorithm/string.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

static bool read_png_metadata_and_dimensions(const char* filepath,
                                            std::map<std::string, std::string>& out_meta,
                                            int* out_w, int* out_h)
{
	if (out_w) *out_w = -1;
	if (out_h) *out_h = -1;
	if (!filepath) return false;

	FILE *f = fopen(filepath, "rb");
	if (!f) return false;

	uint8_t sig[8];
	if (fread(sig, 1, 8, f) != 8 || memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0)
	{
		fclose(f);
		return false;
	}

	while (true)
	{
		uint8_t len_buf[4];
		if (fread(len_buf, 1, 4, f) != 4) break;
		uint32_t chunk_len = ((uint32_t)len_buf[0] << 24) |
		                     ((uint32_t)len_buf[1] << 16) |
		                     ((uint32_t)len_buf[2] << 8)  |
		                     ((uint32_t)len_buf[3]);

		uint8_t type_buf[4];
		if (fread(type_buf, 1, 4, f) != 4) break;

		if (memcmp(type_buf, "IHDR", 4) == 0 && chunk_len >= 8)
		{
			uint8_t ihdr_data[8];
			if (fread(ihdr_data, 1, 8, f) == 8)
			{
				if (out_w)
					*out_w = (int)(((uint32_t)ihdr_data[0] << 24) | ((uint32_t)ihdr_data[1] << 16) | ((uint32_t)ihdr_data[2] << 8) | (uint32_t)ihdr_data[3]);
				if (out_h)
					*out_h = (int)(((uint32_t)ihdr_data[4] << 24) | ((uint32_t)ihdr_data[5] << 16) | ((uint32_t)ihdr_data[6] << 8) | (uint32_t)ihdr_data[7]);
				fseek(f, (long)(chunk_len - 8 + 4), SEEK_CUR); // skip rest of IHDR + 4 CRC
			}
			else break;
		}
		else if (memcmp(type_buf, "tEXt", 4) == 0 && chunk_len > 0 && chunk_len < 65536)
		{
			std::vector<char> data(chunk_len + 1, 0);
			if (fread(data.data(), 1, chunk_len, f) == chunk_len)
			{
				const char* key = data.data();
				size_t key_len = strlen(key);
				if (key_len < chunk_len)
				{
					const char* val = key + key_len + 1;
					out_meta[std::string(key)] = std::string(val);
				}
				fseek(f, 4, SEEK_CUR); // skip CRC
			}
			else break;
		}
		else if (memcmp(type_buf, "IDAT", 4) == 0 || memcmp(type_buf, "IEND", 4) == 0)
		{
			break;
		}
		else
		{
			if (fseek(f, (long)(chunk_len + 4), SEEK_CUR) != 0) break;
		}
	}

	fclose(f);
	return true;
}

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
		{128,"normal"},
		{256,"large"},
		{512,"x-large"},
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
	std::string GetFileName() const;
	
	int GetWidth();
	int GetHeight();
	
	void GetVideoDimensions(gint *width, gint *height);

	std::string GetFilePath() const;

	void LoadExifData();
	int GetOrientation();
	time_t GetTimeT(bool fromExif = true);
	
	std::shared_ptr<Exiv2::ExifData> GetExifData();
	bool SetExifData(std::shared_ptr<Exiv2::ExifData> pExifData);
	GdkTexture* GetExifThumbnailTexture();
	
	bool HasThumbnail(int iSize) ;
	GdkTexture* GetThumbnailTexture(int iSize = 0,
		QuiverVideoOps::VideoAbortFn abort_fn = NULL,
		gpointer abort_data = NULL);
	void SaveThumbnail(GdkTexture* texture, const char* uri, const char* path, time_t mtime, gint64 size, int width, int height, int orientation);

#if HAVE_GDK_PIXBUF
	GdkPixbuf* GetExifThumbnail();
	GdkPixbuf* GetThumbnail(int iSize = 0,
		QuiverVideoOps::VideoAbortFn abort_fn = NULL,
		gpointer abort_data = NULL);
	void SaveThumbnail(GdkPixbuf* pixbuf, const char* uri, const char* path, time_t mtime, gint64 size, int width, int height, int orientation);
#endif
	
	bool Modified() const;
	bool IsVideo();
	bool IsFolder() const;
	
// variables	
	gchar* m_szURI;
	gchar* m_szMimeType;
	GFileInfo* m_pGFileInfo;
 	
 	std::shared_ptr<Exiv2::ExifData> m_ExifData;
 	std::shared_ptr<Exiv2::ExifData> m_ExifDataOriginal;
 	
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

	bool m_bThumbloadFail;
	time_t m_cachedTimeT = 0;
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
	ThumbnailSaveThreadData(GdkTexture* texture, const char* uri, const char* path, time_t mtime, gint64 size, int width, int height, int orientation)
	{
		m_pTexture = texture ? (GdkTexture*)g_object_ref(texture) : NULL;
		m_strURI = uri;
		m_strPath = path;
		m_mtime = mtime;
		m_iSize = size;
		m_iWidth = width;
		m_iHeight = height;
		m_iOrientation = orientation;
	}
#if HAVE_GDK_PIXBUF
	ThumbnailSaveThreadData(GdkPixbuf* pixbuf, const char* uri, const char* path, time_t mtime, gint64 size, int width, int height, int orientation)
	{
		m_pTexture = QuiverUtils::PixbufToTexture(pixbuf);
		m_strURI = uri;
		m_strPath = path;
		m_mtime = mtime;
		m_iSize = size;
		m_iWidth = width;
		m_iHeight = height;
		m_iOrientation = orientation;
	}
#endif
	~ThumbnailSaveThreadData()
	{
		if (m_pTexture)
		{
			g_object_unref(m_pTexture);
			m_pTexture = NULL;
		}
	}
	GdkTexture* m_pTexture;
	std::string m_strURI;
	std::string m_strPath;
	time_t m_mtime;
	gint64 m_iSize;
   	int m_iWidth;
   	int m_iHeight;
   	int m_iOrientation;
};

boost::shared_ptr<GThreadPool> QuiverFile::QuiverFileImpl::c_ThreadPoolPtr;

static void GetImageDimensions(const gchar *uri, const gchar* mimetype, gint *width, gint *height);
gchar* quiver_thumbnail_path_for_uri(const char* uri, const char* szSize);
static gchar* quiver_thumbnail_path_for_uri_legacy(const char* uri, const char* szSize);
static gchar* quiver_thumbnail_fail_path_for_uri(const char* uri);
static void quiver_thumbnail_write_fail_marker(const char* uri, time_t mtime);
static gboolean quiver_thumbnail_fail_marker_valid(const char* uri, time_t mtime);


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
	m_bThumbloadFail = false;

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

	m_pGFileInfo = info;

	m_fDataLoaded = QUIVER_FILE_DATA_NONE;
	m_fDataExists = QUIVER_FILE_DATA_NONE;
	
	if (NULL != m_pGFileInfo)
	{
		g_object_ref(m_pGFileInfo);
		m_fDataExists = (QuiverDataFlags)(m_fDataExists | QUIVER_FILE_DATA_INFO);
		m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_INFO);
	}
	else
	{
		GetFileInfo();
	}
	
	m_szMimeType = NULL;

	m_pIPTCData = NULL;

	m_iWidth = -1;
	m_iHeight = -1;
	
	m_iOrientation = 0;
	
	m_fDataModified = QUIVER_FILE_DATA_NONE;
	
	m_dLoadTimeSeconds = -1;

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

std::string QuiverFile::QuiverFileImpl::GetFileName() const
{
	std::string s;
	GFile* gfile = g_file_new_for_uri(m_szURI);
	char* shortname = g_file_get_basename(gfile);
	s = shortname;
	g_free(shortname);
	g_object_unref(gfile);
	return s;
}

GFileInfo* QuiverFile::QuiverFileImpl::GetFileInfo()
{
	GFileInfo* gFileInfo = NULL;

	if ((m_fDataExists & QUIVER_FILE_DATA_INFO) && m_pGFileInfo)
	{
		gFileInfo = m_pGFileInfo;
	}
	
	if (!(m_fDataLoaded & QUIVER_FILE_DATA_INFO) && NULL != m_szURI && NULL == m_pGFileInfo)
	{
		GFile* file = g_file_new_for_uri(m_szURI);
		gFileInfo = g_file_query_info(file,
			G_FILE_ATTRIBUTE_STANDARD_TYPE ","
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
			m_pGFileInfo = gFileInfo;
			
			m_fDataExists = (QuiverDataFlags)(m_fDataExists | QUIVER_FILE_DATA_INFO);
			m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_INFO);
		}
	}
	
	return gFileInfo;
}

QuiverFile::QuiverFileImpl::~QuiverFileImpl()
{
	if (m_pGFileInfo != NULL)
	{
		g_object_unref(m_pGFileInfo);
	}
	
	if (NULL != m_szURI)
	{
		delete [] m_szURI;
	}

	if (NULL != m_szMimeType)
	{
		g_free(m_szMimeType);
	}
}

GdkTexture * QuiverFile::QuiverFileImpl::GetExifThumbnailTexture()
{
	GdkTexture *thumb_texture = NULL;

	if (IsVideo())
	{
		return NULL;
	}

	LoadExifData();

	if (m_fDataExists & QUIVER_FILE_DATA_EXIF && NULL != m_ExifData.get())
	{
		try
		{
			Exiv2::DataBuf buf = Exiv2::ExifThumbC(*m_ExifData).copy();

#if EXIV2_TEST_VERSION(0,28,0)
			if (0 < buf.size())
			{
				GBytes *bytes = g_bytes_new_static(buf.c_data(), buf.size());
				thumb_texture = ImageDecoder::DecodeBytesTexture(bytes);
				g_bytes_unref(bytes);
			}
#else
			if (0 < buf.size_)
			{
				GBytes *bytes = g_bytes_new_static(buf.pData_, buf.size_);
				thumb_texture = ImageDecoder::DecodeBytesTexture(bytes);
				g_bytes_unref(bytes);
			}
#endif
		}
		catch (...)
		{
			thumb_texture = NULL;
		}
	}

	return thumb_texture;
}

#if HAVE_GDK_PIXBUF
static void pixbuf_data_destroy(guint8 *pixels, gpointer data)
{
	(void)pixels;
	g_free(data);
}

static GdkPixbuf *texture_to_pixbuf(GdkTexture *tex)
{
	if (!tex) return NULL;
	gint w = gdk_texture_get_width(tex);
	gint h = gdk_texture_get_height(tex);
	gsize stride = (gsize)w * 4;
	guint8 *data = (guint8 *)g_malloc(stride * h);
	gdk_texture_download(tex, data, stride);
	return gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, w, h,
		stride, pixbuf_data_destroy, data);
}
#endif

#if HAVE_GDK_PIXBUF
GdkPixbuf * QuiverFile::QuiverFileImpl::GetExifThumbnail()
{
	GdkTexture *tex = GetExifThumbnailTexture();
	if (!tex) return NULL;
	GdkPixbuf *pb = texture_to_pixbuf(tex);
	g_object_unref(tex);
	return pb;
}
#endif

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
		
		struct stat s = {};
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

GdkTexture * QuiverFile::QuiverFileImpl::GetThumbnailTexture(int iSize /* = 0 */,
	QuiverVideoOps::VideoAbortFn abort_fn /* = NULL */,
	gpointer abort_data /* = NULL */)
{
	if (IsFolder() || m_bThumbloadFail)
		return NULL;

	GFileInfo* gFileInfo = GetFileInfo();
	gboolean save_thumbnail_to_cache = TRUE;
	GdkTexture * thumb_texture = NULL;

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
	thumb_texture = c_ThumbnailCache.m_mapThumbnailCache[thumbSize->size]->GetTexture(m_szURI);
	if (NULL != thumb_texture)
	{
		return thumb_texture;
	}

	gchar* thumb_path = quiver_thumbnail_path_for_uri(m_szURI, thumbSize->name);
	gchar* legacy_thumb_path = quiver_thumbnail_path_for_uri_legacy(m_szURI, thumbSize->name);
	const char* candidate_paths[2] = { thumb_path, legacy_thumb_path };
	gboolean loaded_from_legacy = FALSE;
	GError *tmp_error = NULL;

	for (int path_index = 0; path_index < 2 && NULL == thumb_texture; path_index++)
	{
		if (!g_file_test(candidate_paths[path_index], G_FILE_TEST_EXISTS))
			continue;

		std::map<std::string, std::string> png_meta;
		int thumb_w = -1, thumb_h = -1;
		if (!read_png_metadata_and_dimensions(candidate_paths[path_index], png_meta, &thumb_w, &thumb_h))
			continue;

		loaded_from_legacy = (0 < path_index);
		bool valid = true;

		auto ori_itr = png_meta.find("Thumb::Image::Orientation");
		if (ori_itr != png_meta.end())
		{
			m_iOrientation = atoi(ori_itr->second.c_str());
		}

		auto mtime_itr = png_meta.find("Thumb::MTime");
		if (mtime_itr != png_meta.end())
		{
			time_t mtime = (time_t)atol(mtime_itr->second.c_str());
			gint64 tv_sec = 0;
			if (gFileInfo)
			{
				GDateTime* datetime = g_file_info_get_modification_date_time(gFileInfo);
				if (datetime)
				{
					tv_sec = g_date_time_to_unix(datetime);
					g_date_time_unref(datetime);
				}
			}
			if (tv_sec != mtime)
			{
				valid = false;
			}
			else
			{
				save_thumbnail_to_cache = FALSE;
			}
		}

		if (valid)
		{
			auto size_itr = png_meta.find("Thumb::Size");
			if (size_itr != png_meta.end() && gFileInfo)
			{
				gint64 thumb_size = g_ascii_strtoll(size_itr->second.c_str(), NULL, 10);
				gint64 file_size = g_file_info_get_size(gFileInfo);
				if (file_size > 0 && thumb_size != file_size)
				{
					valid = false;
					save_thumbnail_to_cache = TRUE;
				}
			}
		}

		if (valid)
		{
			int img_width = -1;
			int img_height = -1;
			auto w_itr = png_meta.find("Thumb::Image::Width");
			auto h_itr = png_meta.find("Thumb::Image::Height");
			if (w_itr != png_meta.end() && h_itr != png_meta.end())
			{
				img_width = atoi(w_itr->second.c_str());
				img_height = atoi(h_itr->second.c_str());
			}

			if (-1 == img_width || -1 == img_height)
			{
				if (IsVideo())
				{
					valid = false;
				}
				save_thumbnail_to_cache = TRUE;
			}
			else
			{
				guint act_width = img_width;
				guint act_height = img_height;
				quiver_rect_get_bound_size(thumbSize->size, thumbSize->size, &act_width, &act_height, FALSE);

				if ((int)act_width != thumb_w || (int)act_height != thumb_h)
				{
					valid = false;
					save_thumbnail_to_cache = TRUE;
				}
				else
				{
					if (-1 == m_iWidth || -1 == m_iHeight)
					{
						if (img_width > 0 && img_height > 0)
						{
							m_iWidth = img_width;
							m_iHeight = img_height;
						}
					}
				}
			}
		}

		if (valid)
		{
			GFile *f = g_file_new_for_path(candidate_paths[path_index]);
			thumb_texture = gdk_texture_new_from_file(f, NULL);
			g_object_unref(f);
		}
	}

	g_free(legacy_thumb_path);

	if (NULL != thumb_texture && loaded_from_legacy)
	{
		save_thumbnail_to_cache = TRUE;
	}

	gint64 tv_sec = 0;
	if (NULL != gFileInfo)
	{
		GDateTime* datetime = g_file_info_get_modification_date_time(gFileInfo);
		if (NULL != datetime)
		{
			tv_sec = g_date_time_to_unix(datetime);
			g_date_time_unref(datetime);
		}
	}

	gboolean bSkipGeneration = (NULL == thumb_texture && 0 < tv_sec && quiver_thumbnail_fail_marker_valid(m_szURI, tv_sec));

	if (!bSkipGeneration)
	{
		if (NULL == thumb_texture && 128 >= thumbSize->size && !IsVideo())
		{
			thumb_texture = GetExifThumbnailTexture();
			if (NULL != thumb_texture)
			{
				guint size = thumbSize->size;
				guint tex_width = gdk_texture_get_width(thumb_texture);
				guint tex_height = gdk_texture_get_height(thumb_texture);
				double thumb_ratio = tex_width / (double)tex_height;
				double actual_ratio = GetWidth() / (double)GetHeight();

				if (thumb_ratio == actual_ratio && (tex_width >= size || tex_height >= size))
				{
					quiver_rect_get_bound_size(size, size, &tex_width, &tex_height, FALSE);
					GdkTexture *scaled = QuiverUtils::ScaleTexture(thumb_texture, tex_width, tex_height);
					g_object_unref(thumb_texture);
					thumb_texture = scaled;
				}
				else
				{
					g_object_unref(thumb_texture);
					thumb_texture = NULL;
				}
			}
		}

		if (NULL == thumb_texture)
		{
			int size = thumbSize->size;
			if (IsVideo())
			{
				gint n = 1, d = 1;
				GdkTexture *video_tex = QuiverVideoOps::LoadTexture(m_szURI, &n, &d, -1, size, size, abort_fn, abort_data);
				if (NULL != video_tex)
				{
					guint tex_width = gdk_texture_get_width(video_tex);
					guint tex_height = gdk_texture_get_height(video_tex);
					if (n > d)
						tex_width = (guint)((tex_width * n) / float(d) + .5);
					else
						tex_height = (guint)((tex_height * d) / float(n) + .5);

					m_iWidth = tex_width;
					m_iHeight = tex_height;

					if (tex_width > (guint)size || tex_height > (guint)size)
					{
						quiver_rect_get_bound_size(size, size, &tex_width, &tex_height, FALSE);
						thumb_texture = QuiverUtils::ScaleTexture(video_tex, tex_width, tex_height);
						g_object_unref(video_tex);
					}
					else
					{
						save_thumbnail_to_cache = FALSE;
						thumb_texture = video_tex;
					}
				}
			}
			else
			{
				GFile* gfile = g_file_new_for_uri(m_szURI);
				int orig_w = -1, orig_h = -1;
				if (-1 == m_iWidth || -1 == m_iHeight)
				{
					ImageDecoder::GetDimensions(gfile, GetMimeType(), &orig_w, &orig_h);
					if (orig_w > 0 && orig_h > 0)
					{
						m_iWidth = orig_w;
						m_iHeight = orig_h;
					}
				}
				else
				{
					orig_w = m_iWidth;
					orig_h = m_iHeight;
				}

				thumb_texture = ImageDecoder::DecodeFileTexture(gfile, GetMimeType(), NULL, &tmp_error);
				g_object_unref(gfile);

				if (NULL != thumb_texture)
				{
					guint tex_w = gdk_texture_get_width(thumb_texture);
					guint tex_h = gdk_texture_get_height(thumb_texture);
					if (tex_w > (guint)size || tex_h > (guint)size)
					{
						quiver_rect_get_bound_size(size, size, &tex_w, &tex_h, FALSE);
						GdkTexture *scaled = QuiverUtils::ScaleTexture(thumb_texture, tex_w, tex_h);
						g_object_unref(thumb_texture);
						thumb_texture = scaled;
					}
					else if (orig_w > 0 && orig_h > 0 && orig_w <= size && orig_h <= size)
					{
						save_thumbnail_to_cache = FALSE;
					}
				}
			}
		}
	}

	gchar *base_thumb_dir = g_build_filename(g_get_user_cache_dir(), "thumbnails", NULL);
	gchar *base_thumb_dir_legacy = g_build_filename(g_get_home_dir(), ".thumbnails", NULL);

	if (save_thumbnail_to_cache)
	{
		std::string filepath = GetFilePath();
		if ((NULL != base_thumb_dir && NULL != strstr(filepath.c_str(), base_thumb_dir)) ||
		    (NULL != base_thumb_dir_legacy && NULL != strstr(filepath.c_str(), base_thumb_dir_legacy)))
		{
			save_thumbnail_to_cache = FALSE;
		}
	}

	g_free(base_thumb_dir);
	g_free(base_thumb_dir_legacy);

	if (NULL != thumb_texture && save_thumbnail_to_cache && gFileInfo)
	{
		SaveThumbnail(thumb_texture, m_szURI, thumb_path, tv_sec, g_file_info_get_size(gFileInfo), GetWidth(), GetHeight(), GetOrientation());
	}

	g_free(thumb_path);

	if (NULL != thumb_texture && 1 < GetOrientation())
	{
		GdkTexture *new_tex = QuiverUtils::TextureExifReorientate(thumb_texture, GetOrientation());
		if (NULL != new_tex)
		{
			g_object_unref(thumb_texture);
			thumb_texture = new_tex;
		}
	}

	if (NULL != thumb_texture)
	{
		c_ThumbnailCache.m_mapThumbnailCache[thumbSize->size]->AddTexture(m_szURI, thumb_texture);
		m_mapThumbnailExists[thumbSize->size] = true;
	}
	else
	{
		m_bThumbloadFail = true;
		if (0 < tv_sec)
		{
			quiver_thumbnail_write_fail_marker(m_szURI, tv_sec);
		}
	}

	return thumb_texture;
}

void QuiverFile::QuiverFileImpl::SaveThumbnail(GdkTexture* texture, const char* uri, const char* path, time_t mtime, gint64 size, int width, int height, int orientation)
{
	static GMutex mutex = { 0 };
	g_mutex_lock (&mutex);
	if (NULL == c_ThreadPoolPtr.get())
	{
		c_ThreadPoolPtr = boost::shared_ptr<GThreadPool> (
				g_thread_pool_new(thread_save_thumbnail, NULL, 1, FALSE, NULL),
				ThreadPoolDestructor()
				);
	}

	ThumbnailSaveThreadData *thread_data = new ThumbnailSaveThreadData(texture, uri, path, mtime, size, width, height, orientation);

	g_thread_pool_push(c_ThreadPoolPtr.get(), thread_data, NULL);
	g_mutex_unlock (&mutex);
}

#if HAVE_GDK_PIXBUF
GdkPixbuf * QuiverFile::QuiverFileImpl::GetThumbnail(int iSize /* = 0 */,
	QuiverVideoOps::VideoAbortFn abort_fn /* = NULL */,
	gpointer abort_data /* = NULL */)
{
	GdkTexture *tex = GetThumbnailTexture(iSize, abort_fn, abort_data);
	if (!tex) return NULL;
	GdkPixbuf *pb = texture_to_pixbuf(tex);
	g_object_unref(tex);
	return pb;
}

void QuiverFile::QuiverFileImpl::SaveThumbnail(GdkPixbuf* pixbuf, const char* uri, const char* path, time_t mtime, gint64 size, int width, int height, int orientation)
{
	static GMutex mutex = { 0 };
	g_mutex_lock (&mutex);
	if (NULL == c_ThreadPoolPtr.get())
	{
		c_ThreadPoolPtr = boost::shared_ptr<GThreadPool> (
				g_thread_pool_new(thread_save_thumbnail, NULL, 1, FALSE, NULL),
				ThreadPoolDestructor()
				);
	}

	ThumbnailSaveThreadData *thread_data = new ThumbnailSaveThreadData(pixbuf, uri, path, mtime, size, width, height, orientation);

	g_thread_pool_push(c_ThreadPoolPtr.get(), thread_data, NULL);
	g_mutex_unlock (&mutex);
}
#endif

void QuiverFile::QuiverFileImpl::Reload()
{
	std::string strURI = m_szURI;
	
	if (NULL != m_pGFileInfo)
	{
		g_object_unref(m_pGFileInfo);
		m_pGFileInfo = NULL;
	}
	
	delete [] m_szURI;
	m_szURI = NULL;

	m_ExifData.reset();
	m_ExifDataOriginal.reset();
	
	unsigned int n_elements = G_N_ELEMENTS(ThumbnailSizes);
	for (unsigned int i = 0 ; i < n_elements; i++)
	{
		c_ThumbnailCache.m_mapThumbnailCache[ThumbnailSizes[i].size]->RemoveTexture(strURI);
			
	}

	Init(strURI.c_str(), NULL);
}


void QuiverFile::QuiverFileImpl::LoadExifData()
{
	static std::once_flag s_exiv2_init;
	std::call_once(s_exiv2_init, []() {
		Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);
	});

	if (NULL != m_szURI && !( m_fDataLoaded & QUIVER_FILE_DATA_EXIF ) )
	{
		if (IsVideo())
		{
			m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_EXIF);
			return;
		}

		gchar* szPath = g_filename_from_uri(m_szURI, NULL, NULL);
		if (NULL != szPath)
		{
			try
			{
				auto image = Exiv2::ImageFactory::open(szPath);
				image->readMetadata();

				Exiv2::ExifData exifData = image->exifData();
				if (!exifData.empty())
				{
					m_ExifData =
						std::make_shared<Exiv2::ExifData>(std::move(exifData));
					m_fDataExists =
						(QuiverDataFlags)(m_fDataExists | QUIVER_FILE_DATA_EXIF);

					m_ExifDataOriginal =
						std::make_shared<Exiv2::ExifData>(*m_ExifData);
				}
			}
			catch (...)
			{
				// unreadable metadata counts as no-exif
			}
			g_free(szPath);
		}

		m_fDataLoaded = (QuiverDataFlags)(m_fDataLoaded | QUIVER_FILE_DATA_EXIF);
	}
}

std::shared_ptr<Exiv2::ExifData> QuiverFile::QuiverFileImpl::GetExifData()
{
	LoadExifData();
	return m_ExifData;
}

// Parses a container date string ("creation_time"/"date" metadata).
// ISO-8601 strings with an explicit zone ('+' offset or 'Z' suffix) are
// flagged as zoned; exiftool-style "YYYY-MM-DD HH:MM:SS" strings come
// back unzoned.
static GDateTime* ParseContainerDateString(const gchar* szValue, bool* pbExplicitTZ)
{
	*pbExplicitTZ = false;
	if (NULL == szValue || '\0' == szValue[0])
		return NULL;

	GDateTime* pDate = g_date_time_new_from_iso8601(szValue, NULL);
	if (NULL != pDate)
	{
		size_t len = strlen(szValue);
		char cLast = (0 < len) ? szValue[len - 1] : '\0';
		*pbExplicitTZ = (NULL != strchr(szValue, '+')) ||
			'Z' == cLast || 'z' == cLast;
		return pDate;
	}

	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
	if (6 == sscanf(szValue, "%04d-%02d-%02d %02d:%02d:%02d",
			&y, &mo, &d, &h, &mi, &s) &&
		y >= 1970 && y < 2100)
	{
		return g_date_time_new_local(y, mo, d, h, mi, s);
	}
	return NULL;
}

time_t QuiverFile::QuiverFileImpl::GetTimeT(bool fromExif /* = true */)
{
	if (m_cachedTimeT != 0)
		return m_cachedTimeT;

	if (fromExif && !IsVideo())
	{
		std::shared_ptr<Exiv2::ExifData> pExifData = GetExifData();
		if (NULL != pExifData.get())
		{
			// use date_time_original
			auto it = pExifData->findKey(
				Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
			if (pExifData->end() != it)
			{
				char szDate[20];
				g_strlcpy(szDate, it->toString().c_str(), sizeof(szDate));

				struct tm tm_exif_time = {};
				int num_substs = sscanf(szDate,"%04d:%02d:%02d %02d:%02d:%02d",
					&tm_exif_time.tm_year,
					&tm_exif_time.tm_mon,
					&tm_exif_time.tm_mday,
					&tm_exif_time.tm_hour,
					&tm_exif_time.tm_min,
					&tm_exif_time.tm_sec);

				if (6 == num_substs && tm_exif_time.tm_year >= 1970 && tm_exif_time.tm_year < 2100)
				{
					tm_exif_time.tm_year -= 1900;
					tm_exif_time.tm_mon -= 1;
					tm_exif_time.tm_isdst = -1;

					// successfully parsed date
					m_cachedTimeT = mktime(&tm_exif_time);
				}

			}
		}
	}
	else if (fromExif)
	{
		// read the container's creation date in-process via libavformat
		// (header-only open, no stream probing); fall through to mtime
		// when unavailable.  avformat is not documented thread-safe:
		// GetTimeT runs on the GUI thread and on worker threads.
		static std::mutex s_avformatMutex;
		static std::once_flag s_avformatInitFlag;
		std::lock_guard<std::mutex> lock(s_avformatMutex);
		// keep ffmpeg's chatter off stderr; quiver keeps logs clean
		std::call_once(s_avformatInitFlag,
			[](){ av_log_set_level(AV_LOG_ERROR); });

		gchar* szPath = g_filename_from_uri(m_szURI, NULL, NULL);
		if (NULL != szPath)
		{
			AVFormatContext* pFmt = NULL;
			if (avformat_open_input(&pFmt, szPath, NULL, NULL) >= 0 &&
				NULL != pFmt)
			{
				AVDictionaryEntry* e =
					av_dict_get(pFmt->metadata, "creation_time", NULL, 0);
				if (NULL == e) // some muxers store a plain "date" tag
					e = av_dict_get(pFmt->metadata, "date", NULL, 0);
				for (unsigned i = 0; NULL == e && i < pFmt->nb_streams; i++)
				{
					// some muxers only tag the streams
					e = av_dict_get(pFmt->streams[i]->metadata,
						"creation_time", NULL, 0);
				}

				bool bExplicitTZ = false;
				GDateTime* pGDate =
					ParseContainerDateString(e ? e->value : NULL, &bExplicitTZ);
				if (NULL != pGDate)
				{
					// container times (mvhd etc.) carry no usable zone;
					// match the legacy exiftool behaviour by reading the
					// fields as local wall clock.  Explicitly zoned tags
					// are trusted as-is, and camera files whose names
					// contain "pxl" store true UTC (as before).
					if (bExplicitTZ)
					{
						m_cachedTimeT = g_date_time_to_unix(pGDate);
					}
					else
					{
						const char* base = strrchr(m_szURI, '/');
						std::string low = g_ascii_strdown(
							(NULL != base) ? base + 1 : m_szURI, -1);
						if (std::string::npos != low.find("pxl"))
						{
							m_cachedTimeT = g_date_time_to_unix(pGDate);
						}
						else
						{
							GDateTime* pLocal = g_date_time_new_local(
								g_date_time_get_year(pGDate),
								g_date_time_get_month(pGDate),
								g_date_time_get_day_of_month(pGDate),
								g_date_time_get_hour(pGDate),
								g_date_time_get_minute(pGDate),
								g_date_time_get_seconds(pGDate));
							if (NULL != pLocal)
							{
								m_cachedTimeT =
									g_date_time_to_unix(pLocal);
								g_date_time_unref(pLocal);
							}
						}
					}
					g_date_time_unref(pGDate);
				}
				avformat_close_input(&pFmt);
			}
			g_free(szPath);
		}
	}


	if (0 == m_cachedTimeT) // unable to get exif date	
	{
		// use ctime or mtime
		GFileInfo *pInfo = GetFileInfo();
		
		if (NULL != pInfo)
		{
			GDateTime* datetime = g_file_info_get_modification_date_time(pInfo);
			if (NULL != datetime)
			{
				m_cachedTimeT = g_date_time_to_unix(datetime);
				g_date_time_unref(datetime);
			}
		}
	}

	return m_cachedTimeT;
}	



// structural comparison for change detection (ignores byte-order and
// format normalization differences that raw serialization would flag)
static bool ExifDataEqual(const Exiv2::ExifData& a, const Exiv2::ExifData& b)
{
	if (a.count() != b.count())
	{
		return false;
	}

	auto ia = a.begin();
	auto ib = b.begin();
	for (; ia != a.end() && ib != b.end(); ++ia, ++ib)
	{
		if (ia->key() != ib->key() || ia->value().toString() != ib->value().toString())
		{
			return false;
		}
	}
	return true;
}

bool QuiverFile::QuiverFileImpl::SetExifData(std::shared_ptr<Exiv2::ExifData> pExifData)
{
	bool bSet = false;
	bool bModified = false;

	LoadExifData();

	if (NULL != pExifData.get() && NULL != m_ExifDataOriginal.get())
	{
		bModified = !ExifDataEqual(*pExifData, *m_ExifDataOriginal);

		int iOldOrientation = GetOrientation();

		if (bModified)
		{
			m_ExifData = std::make_shared<Exiv2::ExifData>(*pExifData);

			// set the modified flag
			m_fDataModified = (QuiverDataFlags)(m_fDataModified|QUIVER_FILE_DATA_EXIF);
		}
		else
		{
			m_ExifData = m_ExifDataOriginal;

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
				c_ThumbnailCache.m_mapThumbnailCache[ThumbnailSizes[i].size]->RemoveTexture(m_szURI);

			}
		}

		bSet = true;
	}
	else if (NULL != pExifData.get() && NULL == m_ExifDataOriginal.get())
	{
		m_ExifData = std::make_shared<Exiv2::ExifData>(*pExifData);
		m_fDataModified = (QuiverDataFlags)(m_fDataModified|QUIVER_FILE_DATA_EXIF);
		bSet = true;
	}
	return bSet;
}

bool QuiverFile::QuiverFileImpl::Modified() const
{
	return (0 != m_fDataModified);
}

bool QuiverFile::QuiverFileImpl::IsFolder() const
{
	bool isDir = false;
	if (NULL != m_pGFileInfo)
	{
		GFileType type = g_file_info_get_file_type(m_pGFileInfo);
		isDir = (G_FILE_TYPE_DIRECTORY == type);
	}
	return isDir;
}

bool QuiverFile::QuiverFileImpl::IsVideo()
{
	const char* mime = GetMimeType();
	if (NULL != mime)
	{
		if (g_str_has_prefix(mime, "video/"))
			return true;
		if (0 == strcmp(mime, "application/x-matroska") ||
		    0 == strcmp(mime, "application/ogg") ||
		    0 == strcmp(mime, "application/vnd.rn-realmedia") ||
		    0 == strcmp(mime, "application/vnd.ms-asf") ||
		    0 == strcmp(mime, "application/x-ms-wmv"))
		{
			return true;
		}
	}

	if (NULL != m_szURI)
	{
		const char* dot = strrchr(m_szURI, '.');
		if (NULL != dot)
		{
			std::string ext = dot;
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
			if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov" ||
			    ext == ".wmv" || ext == ".flv" || ext == ".webm" || ext == ".ts" ||
			    ext == ".m2ts" || ext == ".mts" || ext == ".vob" || ext == ".ogv" ||
			    ext == ".3gp" || ext == ".rm" || ext == ".rmvb" || ext == ".asf" ||
			    ext == ".divx")
			{
				return true;
			}
		}
	}

	return false;
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
			GetImageDimensions(m_szURI, GetMimeType(), &m_iWidth, &m_iHeight);
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
			GetImageDimensions(m_szURI, GetMimeType(), &m_iWidth, &m_iHeight);
	}
	return m_iHeight;
}


int QuiverFile::QuiverFileImpl::GetOrientation()
{
	if (IsVideo())
	{
		return 1;
	}

	LoadExifData();

	// if we have loaded the exif data or the orientation flag is set,
	// check the exif data for the orientation value.
	// otherwise just return the cached value
	if (0 == m_iOrientation || (m_fDataLoaded & QUIVER_FILE_DATA_EXIF))
	{
		if (m_fDataExists & QUIVER_FILE_DATA_EXIF)
		{
			std::shared_ptr<Exiv2::ExifData> pExifData = GetExifData();
			int orientation = 1;

			if (NULL != pExifData.get())
			{
				try
				{
					auto it = pExifData->findKey(
						Exiv2::ExifKey("Exif.Image.Orientation"));
					if (pExifData->end() != it)
					{
#if EXIV2_TEST_VERSION(0,28,0)
						orientation = it->toInt64();
#else
						orientation = it->toLong();
#endif
					}
				}
				catch (...)
				{
					orientation = 1;
				}
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

bool QuiverFile::IsFolder() const
{
	return m_QuiverFilePtr->IsFolder();
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
	return m_QuiverFilePtr->GetFileName();
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


void QuiverFile::QuiverFileImpl::GetVideoDimensions(gint *width, gint *height)
{
	/* Prefer the container metadata: it gives the coded frame size plus the
	 * pixel aspect ratio, without decoding a frame (fast). */
	gint n=1, d=1;
	gint w=0, h=0;

	if (QuiverVideoOps::Probe(m_szURI, NULL, &w, &h, &n, &d))
	{
		if (n > d)
			w = (gint)((w * n) / double(d) + .5);
		else
			h = (gint)((h * d) / double(n) + .5);
		*width = w;
		*height = h;
		return;
	}

	GdkTexture* tex = GetThumbnailTexture();
	if (NULL != tex)
	{
		*width = gdk_texture_get_width(tex);
		*height = gdk_texture_get_height(tex);
		g_object_unref(tex);
	}
}

static void GetImageDimensions(const gchar *uri, const gchar* mimetype, gint *width, gint *height)
{
	GFile* gfile = g_file_new_for_uri(uri);
	if (gfile)
	{
		ImageDecoder::GetDimensions(gfile, mimetype, width, height);
		g_object_unref(gfile);
	}
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

GdkTexture * QuiverFile::GetThumbnailTexture(int iSize /* = 0 */,
	QuiverVideoOps::VideoAbortFn abort_fn /* = NULL */,
	gpointer abort_data /* = NULL */)
{
	return m_QuiverFilePtr->GetThumbnailTexture(iSize, abort_fn, abort_data);
}

GdkTexture * QuiverFile::GetExifThumbnailTexture()
{
	return m_QuiverFilePtr->GetExifThumbnailTexture();
}

#if HAVE_GDK_PIXBUF
GdkPixbuf * QuiverFile::GetThumbnail(int iSize /* = 0 */,
	QuiverVideoOps::VideoAbortFn abort_fn /* = NULL */,
	gpointer abort_data /* = NULL */)
{
	return m_QuiverFilePtr->GetThumbnail(iSize, abort_fn, abort_data);
}

GdkPixbuf * QuiverFile::GetExifThumbnail()
{
	return m_QuiverFilePtr->GetExifThumbnail();
}
#endif

bool QuiverFile::HasThumbnail(int iSize )
{
	return m_QuiverFilePtr->HasThumbnail(iSize);
}

gchar* QuiverFile::GetIconName()
{
	if (IsFolder())
	{
		const char* special_name = QuiverUtils::GetSpecialFolderIconName(GetURI());
		if (special_name)
		{
			return g_strdup(special_name);
		}
	}

	gchar *icon_name = NULL;

	GFileInfo *file_info = GetFileInfo();
	
	if (NULL == file_info)
		return NULL;
	
	const char* content_type = g_file_info_get_content_type(file_info);
	GIcon* icon = g_content_type_get_icon(content_type);
	if (NULL != icon)
	{
		icon_name = g_icon_to_string(icon);
		g_object_unref(icon);
	}
	g_object_unref(file_info);
	return icon_name;
}

GdkTexture* QuiverFile::GetIconTexture(int width_desired, int height_desired)
{
	GtkIconTheme* icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	gint size_wanted = MIN(width_desired, height_desired);
	GFileInfo* file_info = GetFileInfo();
	GdkTexture* texture = NULL;

	if (IsFolder())
	{
		if (file_info)
		{
			const char* custom_icon = g_file_info_get_attribute_string(file_info, "metadata::custom-icon");
			if (custom_icon && custom_icon[0])
			{
				GFile* cf = g_file_new_for_commandline_arg(custom_icon);
				texture = gdk_texture_new_from_file(cf, NULL);
				g_object_unref(cf);
				if (texture)
				{
					g_object_unref(file_info);
					return texture;
				}
			}
		}

		const char* special_name = QuiverUtils::GetSpecialFolderIconName(GetURI());
		if (special_name && gtk_icon_theme_has_icon(icon_theme, special_name))
		{
			GtkIconPaintable* paintable = gtk_icon_theme_lookup_icon(
				icon_theme, special_name, NULL, size_wanted, 1,
				GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_FORCE_REGULAR);
			if (paintable)
			{
				GFile* icon_file = gtk_icon_paintable_get_file(paintable);
				if (icon_file)
				{
					texture = gdk_texture_new_from_file(icon_file, NULL);
					g_object_unref(icon_file);
				}
				g_object_unref(paintable);
				if (texture)
				{
					if (file_info) g_object_unref(file_info);
					return texture;
				}
			}
		}
	}

	const char* content_type = file_info ? g_file_info_get_content_type(file_info) : NULL;
	GIcon* icon = content_type ? g_content_type_get_icon(content_type) : NULL;

	if (NULL != icon)
	{
		if (G_IS_THEMED_ICON(icon))
		{
			const char* const* names = g_themed_icon_get_names(G_THEMED_ICON(icon));
			for (int i = 0; names && names[i]; ++i)
			{
				if (!gtk_icon_theme_has_icon(icon_theme, names[i]))
					continue;

				GtkIconPaintable* paintable = gtk_icon_theme_lookup_icon(
					icon_theme, names[i], NULL, size_wanted, 1,
					GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_FORCE_REGULAR);
				if (NULL == paintable)
					continue;

				GFile* icon_file = gtk_icon_paintable_get_file(paintable);
				if (NULL != icon_file)
				{
					texture = gdk_texture_new_from_file(icon_file, NULL);
					g_object_unref(icon_file);
				}
				g_object_unref(paintable);
				if (NULL != texture)
					break;
			}
		}
		else if (G_IS_FILE_ICON(icon))
		{
			GFile* icon_file = g_file_icon_get_file(G_FILE_ICON(icon));
			if (NULL != icon_file)
			{
				texture = gdk_texture_new_from_file(icon_file, NULL);
			}
		}
		g_object_unref(icon);
	}

	if (NULL == texture)
	{
		GtkIconPaintable* paintable = gtk_icon_theme_lookup_icon(
			icon_theme, "image-x-generic", NULL, size_wanted, 1,
			GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_FORCE_REGULAR);
		if (paintable)
		{
			GFile* icon_file = gtk_icon_paintable_get_file(paintable);
			if (icon_file)
			{
				texture = gdk_texture_new_from_file(icon_file, NULL);
				g_object_unref(icon_file);
			}
			g_object_unref(paintable);
		}
	}

	if (file_info)
		g_object_unref(file_info);

	return texture;
}

#if HAVE_GDK_PIXBUF
GdkPixbuf* QuiverFile::GetIcon(int width_desired,int height_desired)
{
	GdkTexture *tex = GetIconTexture(width_desired, height_desired);
	if (!tex) return NULL;
	GdkPixbuf *pb = texture_to_pixbuf(tex);
	g_object_unref(tex);
	return pb;
}
#endif


std::shared_ptr<Exiv2::ExifData> QuiverFile::GetExifData()
{
	// return a copy of the exif data
	std::shared_ptr<Exiv2::ExifData> pExifDataCopy;
	std::shared_ptr<Exiv2::ExifData> pExifData = m_QuiverFilePtr->GetExifData();
	if (pExifData)
	{
		pExifDataCopy = std::make_shared<Exiv2::ExifData>(*pExifData);
	}
	return pExifDataCopy;
}

bool QuiverFile::SetExifData(std::shared_ptr<Exiv2::ExifData> pExifData)
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
			m_QuiverFilePtr->c_ThumbnailCache.m_mapThumbnailCache[ThumbnailSizes[i].size]->RemoveTexture(m_QuiverFilePtr->m_szURI);
			gchar * thumb_path ;
			thumb_path = quiver_thumbnail_path_for_uri(m_QuiverFilePtr->m_szURI,ThumbnailSizes[i].name);
			g_remove(thumb_path);
			g_free (thumb_path);
			gchar * legacy_thumb_path = quiver_thumbnail_path_for_uri_legacy(m_QuiverFilePtr->m_szURI,ThumbnailSizes[i].name);
			g_remove(legacy_thumb_path);
			g_free (legacy_thumb_path);
			
		}
		else if (iSize <= ThumbnailSizes[i].size || i == n_elements - 1 )
		{
			thumbSize = &ThumbnailSizes[i];
			break;
		}
	}
	
	if (-1 != iSize && thumbSize)
	{
		m_QuiverFilePtr->c_ThumbnailCache.m_mapThumbnailCache[thumbSize->size]->RemoveTexture(m_QuiverFilePtr->m_szURI);
	
		gchar * thumb_path ;
		thumb_path = quiver_thumbnail_path_for_uri(m_QuiverFilePtr->m_szURI,thumbSize->name);
		g_remove(thumb_path);
		g_free (thumb_path);
		gchar * legacy_thumb_path = quiver_thumbnail_path_for_uri_legacy(m_QuiverFilePtr->m_szURI,thumbSize->name);
		g_remove(legacy_thumb_path);
		g_free (legacy_thumb_path);
	}

	gchar* fail_path = quiver_thumbnail_fail_path_for_uri(m_QuiverFilePtr->m_szURI);
	g_remove(fail_path);
	g_free(fail_path);
}

time_t QuiverFile::GetTimeT(bool fromExif /* = true */) const
{
	return m_QuiverFilePtr->GetTimeT(fromExif);
}

static gchar* quiver_thumbnail_hash_filename(const char* uri)
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

	return g_strdup(szMD5Hash);
}

gchar* quiver_thumbnail_path_for_uri(const char* uri, const char* szSize)
{
	// freedesktop.org Thumbnail Managing Standard 0.9.0:
	// $XDG_CACHE_HOME/thumbnails/{normal,large,x-large,xx-large}/<md5>.png
	gchar* hash = quiver_thumbnail_hash_filename(uri);
	gchar* path = g_build_filename(g_get_user_cache_dir(), "thumbnails", szSize, hash, NULL);
	g_free(hash);
	return path;
}

static gchar* quiver_thumbnail_path_for_uri_legacy(const char* uri, const char* szSize)
{
	// old pre-0.9 location (~/.thumbnails), kept as a read fallback so
	// thumbnails generated by older Quiver builds are still reused.
	gchar* hash = quiver_thumbnail_hash_filename(uri);
	gchar* path = g_build_filename(g_get_home_dir(), ".thumbnails", szSize, hash, NULL);
	g_free(hash);
	return path;
}

static gchar* quiver_thumbnail_fail_path_for_uri(const char* uri)
{
	gchar* hash = quiver_thumbnail_hash_filename(uri);
	gchar* path = g_build_filename(g_get_user_cache_dir(), "thumbnails", "fail",
		"quiver-" PACKAGE_VERSION, hash, NULL);
	g_free(hash);
	return path;
}

static void quiver_thumbnail_write_fail_marker(const char* uri, time_t mtime)
{
	gchar* fail_path = quiver_thumbnail_fail_path_for_uri(uri);
	gchar* fail_dir = g_path_get_dirname(fail_path);
	g_mkdir_with_parents(fail_dir, S_IRUSR | S_IWUSR | S_IXUSR);
	g_free(fail_dir);

	gchar *temp_file_name = g_strconcat(fail_path, ".XXXXXX", NULL);
	gint fhandle = g_mkstemp(temp_file_name);
	if (-1 != fhandle)
	{
		close(fhandle);
		guint32 pixel = 0;
		GBytes *bytes = g_bytes_new_static(&pixel, 4);
		GdkTexture *marker = gdk_memory_texture_new(1, 1, GDK_MEMORY_DEFAULT, bytes, 4);
		g_bytes_unref(bytes);
		if (marker)
		{
			ImageDecoder::SaveThumbnail(marker, fail_path, uri, mtime, 0, 1, 1, 1);
			g_object_unref(marker);
		}
		g_remove(temp_file_name);
	}
	g_free(temp_file_name);
	g_free(fail_path);
}

static gboolean quiver_thumbnail_fail_marker_valid(const char* uri, time_t mtime)
{
	gchar* fail_path = quiver_thumbnail_fail_path_for_uri(uri);
	if (!g_file_test(fail_path, G_FILE_TEST_EXISTS))
	{
		g_free(fail_path);
		return FALSE;
	}
	std::map<std::string, std::string> meta;
	int w = -1, h = -1;
	gboolean valid = FALSE;
	if (read_png_metadata_and_dimensions(fail_path, meta, &w, &h))
	{
		auto itr = meta.find("Thumb::MTime");
		if (itr != meta.end() && mtime == (time_t)atol(itr->second.c_str()))
		{
			valid = TRUE;
		}
	}
	g_free(fail_path);
	return valid;
}

static void thread_save_thumbnail(gpointer data, gpointer user_data)
{
	(void)user_data;
	ThumbnailSaveThreadData* thumb_data = (ThumbnailSaveThreadData*)data;
	if (thumb_data->m_pTexture)
	{
		ImageDecoder::SaveThumbnail(thumb_data->m_pTexture,
		                            thumb_data->m_strPath.c_str(),
		                            thumb_data->m_strURI.c_str(),
		                            thumb_data->m_mtime,
		                            thumb_data->m_iSize,
		                            thumb_data->m_iWidth,
		                            thumb_data->m_iHeight,
		                            thumb_data->m_iOrientation);
	}
	delete thumb_data;
}
