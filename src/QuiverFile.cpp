#include "config.h"

#include <string>
#include <algorithm>
#include <iostream>
#include <map>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>

#include <gtk/gtk.h>
#include <gst/gst.h>

#include "QuiverFile.h"
#include "Timer.h"
#include "QuiverUtils.h"
#include "QuiverVideoOps.h"
#include "ImageCache.h"
#include "MD5.h"
#include "strnatcmp.h"

using namespace std;
// for sorting
typedef std::multimap<string, QuiverFile*> StringPtrQuiverFileMap;
typedef std::multimap<int, QuiverFile*> IntPtrQuiverFileMap;

// for sorting
struct QuiverFileCompare
{
	bool operator() (QuiverFile* pf1, QuiverFile* pf2)
	{
		return (strnatcmp(pf1->GetFileName().c_str(), pf2->GetFileName().c_str()) < 0);
	}
};


static GdkPixbuf* scale_pixbuf(GdkPixbuf* pixbuf, int size);
static void GetImageDimensions(const gchar *pszFilename, gint *width, gint *height);


class QuiverFile::QuiverFileImpl
{
public:
	// constructor / destructor
	QuiverFileImpl(const string& filePath, const string& fileName, GdkPixbuf* pThumb = NULL) :
		m_strFileName(fileName),
		m_strFilePath(filePath),
		m_pThumb(pThumb),
		m_iWidth(-1),
		m_iHeight(-1),
		m_iOrientation(0),
		m_iFileSize(-1),
		m_bIsDirectory(false),
		m_bIsImage(false),
		m_bIsVideo(false),
		m_bIsArchive(false),
		m_pExifData(NULL),
		m_bExifRead(false),
		m_cachedTimeT(0)
	{
		Initialize();
	}

	QuiverFileImpl(const QuiverFileImpl& qf) :
		m_strFileName(qf.m_strFileName),
		m_strFilePath(qf.m_strFilePath),
		m_strURI(qf.m_strURI),
		m_strFileExtension(qf.m_strFileExtension),
		m_pThumb(qf.m_pThumb ? g_object_ref(qf.m_pThumb) : NULL),
		m_iWidth(qf.m_iWidth),
		m_iHeight(qf.m_iHeight),
		m_iOrientation(qf.m_iOrientation),
		m_iFileSize(qf.m_iFileSize),
		m_bIsDirectory(qf.m_bIsDirectory),
		m_bIsImage(qf.m_bIsImage),
		m_bIsVideo(qf.m_bIsVideo),
		m_bIsArchive(qf.m_bIsArchive),
		m_pExifData(NULL),
		m_bExifRead(qf.m_bExifRead),
		m_cachedTimeT(qf.m_cachedTimeT)
	{
        if (qf.m_pExifData) {
            exif_data_ref(qf.m_pExifData);
            m_pExifData = qf.m_pExifData;
        }
	}

	~QuiverFileImpl()
	{
		if (m_pThumb)
		{
			g_object_unref(m_pThumb);
		}
		if (m_pExifData)
		{
			exif_data_unref(m_pExifData);
		}
	}

	// methods
	string GetFileName() const
	{
		return m_strFileName;
	}

	string GetFilePath() const
	{
		return m_strFilePath;
	}

	const gchar* GetURI() const
	{
		return m_strURI.c_str();
	}

	const string& GetFileExtension() const
	{
		return m_strFileExtension;
	}

	void SetFileName(const string& strFileName)
	{
		m_strFileName = strFileName;
		SetURI();
	}

	void SetFilePath(const string& strFilePath)
	{
		m_strFilePath = strFilePath;
		SetURI();
	}

	void SetURI()
	{
		gchar* uri = g_filename_to_uri(m_strFilePath.c_str(), NULL, NULL);
		if (uri)
		{
			m_strURI = uri;
			g_free(uri);
		}
	}

	bool IsDirectory() const
	{
		if (m_iFileSize == -1)
		{
			const_cast<QuiverFileImpl*>(this)->ReadFileData();
		}
		return m_bIsDirectory;
	}

	bool IsImage() const
	{
		return m_bIsImage;
	}

	bool IsVideo() const
	{
		return m_bIsVideo;
	}

	bool IsArchive() const
	{
		return m_bIsArchive;
	}

	int GetWidth() const
	{
		if (-1 == m_iWidth)
		{
			if (IsVideo())
			{
				const_cast<QuiverFileImpl*>(this)->ReadVideoData();
			}
			else
			{
				const_cast<QuiverFileImpl*>(this)->ReadImageData();
			}
		}
		return m_iWidth;
	}

	int GetHeight() const
	{
		if (-1 == m_iHeight)
		{
			if (IsVideo())
			{
				const_cast<QuiverFileImpl*>(this)->ReadVideoData();
			}
			else
			{
				const_cast<QuiverFileImpl*>(this)->ReadImageData();
			}
		}
		return m_iHeight;
	}

	int GetOrientation() const
	{
		if (IsVideo())
		{
			const_cast<QuiverFileImpl*>(this)->ReadVideoData();
		}
		else
		{
			const_cast<QuiverFileImpl*>(this)->ReadImageData();
		}
		return m_iOrientation;
	}

	int GetFileSize() const
	{
		if (-1 == m_iFileSize)
		{
			const_cast<QuiverFileImpl*>(this)->ReadFileData();
		}
		return m_iFileSize;
	}

	void SetOrientation(int iOrientation)
	{
		m_iOrientation = iOrientation;
	}

	void SetExifData(ExifData* pExifData)
	{
		if (NULL != m_pExifData)
		{
			exif_data_unref(m_pExifData);
			m_pExifData = NULL;
		}
		m_pExifData = pExifData; // assume new data is already ref'd
		m_bExifRead = true;
	}

	ExifData* GetExifData(bool bMustExist = false)
	{
		ReadExifData();
		if (NULL != m_pExifData)
		{
			exif_data_ref(m_pExifData);
		}
		else if (bMustExist)
		{
			m_pExifData = exif_data_new();
			exif_data_ref(m_pExifData);
		}
		return m_pExifData;
	}

	void ReadExifData()
	{
		if (!m_bExifRead)
		{
			m_bExifRead = true;
			if (IsImage())
			{
				m_pExifData = exif_data_new_from_file(m_strFilePath.c_str());
				if (m_pExifData)
				{
					ExifByteOrder byteOrder = exif_data_get_byte_order(m_pExifData);
					ExifEntry *entry = exif_content_get_entry(m_pExifData->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
					if (entry)
					{
						m_iOrientation = exif_get_short(entry->data, byteOrder);
					}
				}
			}
		}
	}

	void ReadVideoData()
	{
		//QuiverVideoOps::GetVideoDimensions(m_strFilePath.c_str(), &m_iWidth, &m_iHeight, &m_iOrientation);
	}

	void ReadImageData()
	{
		GetImageDimensions(m_strFilePath.c_str(), &m_iWidth, &m_iHeight);
		ReadExifData();
	}

	void ReadFileData()
	{
		struct stat stat_buf;
		if (0 == stat(m_strFilePath.c_str(), &stat_buf))
		{
			m_iFileSize = stat_buf.st_size;
			m_bIsDirectory = S_ISDIR(stat_buf.st_mode);
			m_cachedTimeT = stat_buf.st_mtime;
		}
	}

	GdkPixbuf* GetIcon(int size)
	{
		GdkPixbuf* pixbuf = NULL;
		if (IsDirectory())
		{
			GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
			GtkIconPaintable *icon_paintable = gtk_icon_theme_lookup_icon(icon_theme, "folder", NULL, size, 1, GTK_TEXT_DIR_NONE, (GtkIconLookupFlags)0);
			if (icon_paintable)
			{
				// TODO: How to get a pixbuf from a paintable?
				g_object_unref(icon_paintable);
			}
		}
		else if (IsArchive())
		{
			GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
			GtkIconPaintable *icon_paintable = gtk_icon_theme_lookup_icon(icon_theme, "package-x-generic", NULL, size, 1, GTK_TEXT_DIR_NONE, (GtkIconLookupFlags)0);
			if (icon_paintable)
			{
				// TODO: How to get a pixbuf from a paintable?
				g_object_unref(icon_paintable);
			}
		}
		else if (IsVideo())
		{
			GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
			GtkIconPaintable *icon_paintable = gtk_icon_theme_lookup_icon(icon_theme, "video-x-generic", NULL, size, 1, GTK_TEXT_DIR_NONE, (GtkIconLookupFlags)0);
			if (icon_paintable)
			{
				// TODO: How to get a pixbuf from a paintable?
				g_object_unref(icon_paintable);
			}
		}
		else
		{
			pixbuf = GetThumbnail(size);
		}
		return pixbuf;
	}

	GdkPixbuf* GetThumbnail(int size)
	{
		if (NULL != m_pThumb)
		{
			guint current_thumb_width = gdk_pixbuf_get_width(m_pThumb);
			if (current_thumb_width != (guint)size)
			{
				g_object_unref(m_pThumb);
				m_pThumb = NULL;
			}
		}

		if (NULL == m_pThumb)
		{
			m_pThumb = m_thumbnailCache.GetPixbuf(m_strURI);
		}

		if (NULL != m_pThumb)
		{
			guint pixbuf_width = gdk_pixbuf_get_width(m_pThumb);
			guint pixbuf_height = gdk_pixbuf_get_height(m_pThumb);

			if (pixbuf_width > (guint)size || pixbuf_height > (guint)size)
			{
				GdkPixbuf* pScaled = scale_pixbuf(m_pThumb, size);
				g_object_unref(m_pThumb);
				m_pThumb = pScaled;
			}
			g_object_ref(m_pThumb);
		}
		else
		{
			if (IsImage())
			{
				GError *error = NULL;
				GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_size(m_strFilePath.c_str(), size, size, &error);
				if (pixbuf)
				{
					m_pThumb = pixbuf;
					m_thumbnailCache.AddPixbuf(m_strURI, m_pThumb);
					g_object_ref(m_pThumb);
				}
			}
			else if (IsVideo())
			{
				GdkPixbuf* pixbuf = QuiverVideoOps::LoadPixbuf(m_strFilePath.c_str(), 0, 0);
				if (pixbuf)
				{
					m_pThumb = scale_pixbuf(pixbuf, size);
					m_thumbnailCache.AddPixbuf(m_strURI, m_pThumb);
					g_object_ref(m_pThumb);
					g_object_unref(pixbuf);
				}
			}
		}
		return m_pThumb;
	}

	void SetThumbnail(GdkPixbuf* pThumb)
	{
		if (NULL != m_pThumb)
		{
			g_object_unref(m_pThumb);
		}
		m_pThumb = pThumb;
		if (NULL != m_pThumb)
		{
			g_object_ref(m_pThumb);
		}
	}

	void RemoveFromThumbnailCache()
	{
		m_thumbnailCache.RemovePixbuf(m_strURI);
	}

	time_t GetTimeT(bool bUseExif = true) const
	{
		if (bUseExif && IsImage())
		{
			const_cast<QuiverFileImpl*>(this)->ReadExifData();
			if (NULL != m_pExifData)
			{
				ExifEntry *entry = exif_content_get_entry(m_pExifData->ifd[EXIF_IFD_EXIF], EXIF_TAG_DATE_TIME_ORIGINAL);
				if (entry)
				{
					struct tm tm;
					if (strptime((char*)entry->data, "%Y:%m:%d %H:%M:%S", &tm))
					{
						return mktime(&tm);
					}
				}
			}
		}
		if (0 == m_cachedTimeT)
		{
			const_cast<QuiverFileImpl*>(this)->ReadFileData();
		}
		return m_cachedTimeT;
	}

	string GetDate(const char* pszFormat, bool bUseExif = true)
	{
		char output[256] = {0};
		time_t rawtime = GetTimeT(bUseExif);
		struct tm * timeinfo = localtime(&rawtime);
		if (NULL != timeinfo)
		{
			strftime(output, sizeof(output) - 1, pszFormat, timeinfo);
		}
		return output;
	}

	static ImageCache& GetThumbnailCache()
	{
		return m_thumbnailCache;
	}
private:
	void Initialize()
	{
		SetURI();
		string lowerCase = m_strFileName;
		std::transform(lowerCase.begin(), lowerCase.end(), lowerCase.begin(), ::tolower);
		if (lowerCase.rfind(".jpg") != string::npos ||
			lowerCase.rfind(".jpeg") != string::npos ||
			lowerCase.rfind(".png") != string::npos ||
			lowerCase.rfind(".gif") != string::npos ||
			lowerCase.rfind(".bmp") != string::npos)
		{
			m_bIsImage = true;
		}
		else if (lowerCase.rfind(".avi") != string::npos ||
			lowerCase.rfind(".mpg") != string::npos ||
			lowerCase.rfind(".mpeg") != string::npos ||
			lowerCase.rfind(".wmv") != string::npos ||
			lowerCase.rfind(".mov") != string::npos)
		{
			m_bIsVideo = true;
		}
		else if (lowerCase.rfind(".zip") != string::npos ||
			lowerCase.rfind(".rar") != string::npos ||
			lowerCase.rfind(".7z") != string::npos)
		{
			m_bIsArchive = true;
		}

		size_t pos = m_strFileName.rfind('.');
		if (pos != string::npos)
		{
			m_strFileExtension = m_strFileName.substr(pos + 1);
		}
	}

	string m_strFileName;
	string m_strFilePath;
	string m_strURI;
	string m_strFileExtension;

	mutable GdkPixbuf* m_pThumb;

	mutable int m_iWidth;
	mutable int m_iHeight;
	mutable int m_iOrientation;
	mutable int m_iFileSize;
	mutable bool m_bIsDirectory;
	mutable bool m_bIsImage;
	mutable bool m_bIsVideo;
	mutable bool m_bIsArchive;

	mutable ExifData* m_pExifData;
	mutable bool m_bExifRead;

	mutable time_t m_cachedTimeT;

	static ImageCache m_thumbnailCache;
};

ImageCache QuiverFile::QuiverFileImpl::m_thumbnailCache(200);

QuiverFile::QuiverFile()
{
	m_QuiverFilePtr.reset(new QuiverFileImpl("", ""));
}

QuiverFile::QuiverFile(const gchar* uri)
{
	gchar* filename = g_filename_from_uri(uri, NULL, NULL);
	if (filename)
	{
		string strPath, strName;
		GFile* file = g_file_new_for_path(filename);
		strName = g_file_get_basename(file);
		GFile* parent = g_file_get_parent(file);
		if (parent)
		{
			strPath = g_file_get_path(parent);
			g_object_unref(parent);
		}
		g_object_unref(file);

		m_QuiverFilePtr.reset(new QuiverFileImpl(strPath, strName));
		g_free(filename);
	}
	else
	{
		m_QuiverFilePtr.reset(new QuiverFileImpl("", ""));
	}
}

QuiverFile::QuiverFile(const gchar* path, GFileInfo *info)
{
	const char* name = g_file_info_get_name(info);
	m_QuiverFilePtr.reset(new QuiverFileImpl(path, name));
}


QuiverFile::~QuiverFile()
{
}

bool QuiverFile::operator==(const QuiverFile& qf) const
{
	if (NULL == m_QuiverFilePtr.get() && NULL == qf.m_QuiverFilePtr.get()) return true;
	if (NULL == m_QuiverFilePtr.get() || NULL == qf.m_QuiverFilePtr.get()) return false;
	return (m_QuiverFilePtr->GetURI() == qf.m_QuiverFilePtr->GetURI());
}

bool QuiverFile::operator!=(const QuiverFile& qf) const
{
	if (NULL == m_QuiverFilePtr.get() && NULL == qf.m_QuiverFilePtr.get()) return false;
	if (NULL == m_QuiverFilePtr.get() || NULL == qf.m_QuiverFilePtr.get()) return true;
	return (m_QuiverFilePtr->GetURI() != qf.m_QuiverFilePtr->GetURI());
}

const gchar* QuiverFile::GetURI() const
{
	return m_QuiverFilePtr->GetURI();
}

GdkPixbuf* QuiverFile::GetThumbnail(int size)
{
	return m_QuiverFilePtr->GetThumbnail(size);
}

GFileInfo* QuiverFile::GetFileInfo()
{
	GFile* file = g_file_new_for_uri(GetURI());
	GFileInfo* info = g_file_query_info(file, "standard::name,standard::type,time::modified,standard::size", G_FILE_QUERY_INFO_NONE, NULL, NULL);
	g_object_unref(file);
	return info;
}

bool QuiverFile::IsVideo()
{
	return m_QuiverFilePtr->IsVideo();
}


ExifData* QuiverFile::GetExifData()
{
	return m_QuiverFilePtr->GetExifData(false);
}

bool QuiverFile::SetExifData(ExifData* pExifData)
{
	m_QuiverFilePtr->SetExifData(pExifData);
	return true;
}


unsigned long long QuiverFile::GetFileSize()
{
	return m_QuiverFilePtr->GetFileSize();
}

GdkPixbuf* QuiverFile::GetIcon(int width_desired,int height_desired)
{
	return m_QuiverFilePtr->GetIcon(std::max(width_desired,height_desired));
}

GdkPixbuf* QuiverFile::GetPixbuf()
{
	return gdk_pixbuf_new_from_file(m_QuiverFilePtr->GetFilePath().c_str(), NULL);
}


std::string QuiverFile::GetFileName() const
{
	return m_QuiverFilePtr->GetFileName();
}
std::string QuiverFile::GetFilePath() const
{
	return m_QuiverFilePtr->GetFilePath();
}

int QuiverFile::GetWidth()
{
	return m_QuiverFilePtr->GetWidth();
}
int QuiverFile::GetHeight()
{
	return m_QuiverFilePtr->GetHeight();
}


int QuiverFile::GetOrientation()
{
	return m_QuiverFilePtr->GetOrientation();
}

time_t QuiverFile::GetTimeT(bool fromExif) const
{
	return m_QuiverFilePtr->GetTimeT(fromExif);
}

void QuiverFile::Reload()
{
}

bool QuiverFile::IsFolder() const
{
	return false;
}

gchar* QuiverFile::GetIconName()
{
	return NULL;
}

void QuiverFile::SetWidth(int width)
{
}

void QuiverFile::SetHeight(int height)
{
}

bool QuiverFile::HasThumbnail(int size)
{
	return false;
}

double QuiverFile::GetLoadTimeInSeconds() const
{
	return 0;
}

void QuiverFile::SetLoadTimeInSeconds(double seconds)
{
}

const char* QuiverFile::GetMimeType()
{
	return "";
}

bool QuiverFile::Modified() const
{
	return false;
}

bool QuiverFile::IsWriteable()
{
	return false;
}

void QuiverFile::RemoveCachedThumbnail(int size)
{
}

bool QuiverFile::IsWidthHeightSet() const
{
	return false;
}

static GdkPixbuf* scale_pixbuf(GdkPixbuf* pixbuf, int size)
{
	gint width = gdk_pixbuf_get_width(pixbuf);
	gint height = gdk_pixbuf_get_height(pixbuf);
	gint dest_width, dest_height, dest_x = 0, dest_y = 0;

	if (width > height)
	{
		dest_width = size;
		dest_height = (gint)((double)height * ((double)dest_width / (double)width));
	}
	else
	{
		dest_height = size;
		dest_width = (gint)((double)width * ((double)dest_height / (double)height));
	}

	if (dest_width < size)
	{
		dest_x = (size - dest_width) / 2;
	}
	if (dest_height < size)
	{
		dest_y = (size - dest_height) / 2;
	}

	GdkPixbuf* pEmptyThumb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, size, size);
	gdk_pixbuf_fill(pEmptyThumb, 0x00000000);
	gdk_pixbuf_composite(pixbuf, pEmptyThumb, dest_x, dest_y, dest_width, dest_height, dest_x, dest_y, (double)dest_width / (double)width, (double)dest_height / (double)height, GDK_INTERP_BILINEAR, 255);

	return pEmptyThumb;
}

static void GetImageDimensions(const gchar *pszFilename, gint *width, gint *height)
{
	GError *error = NULL;
	GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(pszFilename, &error);
	if (pixbuf)
	{
		*width = gdk_pixbuf_get_width(pixbuf);
		*height = gdk_pixbuf_get_height(pixbuf);
		g_object_unref(pixbuf);
	}
}
