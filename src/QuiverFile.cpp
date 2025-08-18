#include <config.h>

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

#include "QuiverFile.h"
#include "Timer.h"
#include "QuiverUtils.h"
#include "QuiverVideoOps.hh"
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

/*
class QuiverFile::QuiverFileImpl
{
public:
	// constructor / destructor
	QuiverFileImpl(const string& filePath, const string& fileName, GdkPixbuf* pThumb = NULL);
	QuiverFileImpl(const QuiverFileImpl& qf);
	~QuiverFileImpl();

	// methods
	string GetFileName() const;
	string GetFilePath() const;
	const gchar* GetURI() const;
	const string& GetMD5() const;
	const string& GetFileExtension() const;

	void SetFileName(const string& strFileName);
	void SetFilePath(const string& strFilePath);

	bool IsDirectory() const;
	bool IsImage() const;
	bool IsVideo() const;
	bool IsArchive() const;

	int GetWidth() const;
	int GetHeight() const;
	int GetOrientation() const;
	int GetFileSize() const;

	void SetOrientation(int iOrientation);
	void SetExifData(ExifData* pExifData);
	ExifData* GetExifData(bool bMustExist = false);

	GdkPixbuf* GetIcon(int size);

	GdkPixbuf* GetThumbnail(int size);
	void SetThumbnail(GdkPixbuf *pThumb);

	void RemoveFromThumbnailCache();

	time_t GetTimeT(bool bUseExif = true) const;
	string GetDate(const char* pszFormat, bool bUseExif = true);

	static ImageCache& GetThumbnailCache();

private:
	void Initialize();
	void SetURI();
	void ReadExifData();
	void ReadVideoData();
	void ReadImageData();
	void ReadFileData();

	string m_strFileName;
	string m_strFilePath;
	string m_strURI;
	string m_strMD5;
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
	//m_pImpl = new QuiverFileImpl("", "");
}

QuiverFile::QuiverFile(const QuiverFile& qf)
{
	//if (NULL != qf.m_pImpl)
	//{
	//	m_pImpl = new QuiverFileImpl(*(qf.m_pImpl));
	//}
	//else
	//{
	//	m_pImpl = NULL;
	//}
}

QuiverFile::QuiverFile(const gchar* uri)
{
	//gchar* filename = g_filename_from_uri(uri, NULL, NULL);
	//if (filename)
	//{
	//	string strPath, strName;
	//	//QuiverUtils::SplitPath(filename, strPath, strName);
	//	//m_pImpl = new QuiverFileImpl(strPath, strName);
	//	g_free(filename);
	//}
	//else
	//{
	//	//m_pImpl = new QuiverFileImpl("", "");
	//}
}

QuiverFile::QuiverFile(const gchar* path, GFileInfo *info)
{
	//const char* name = g_file_info_get_name(info);
	//m_pImpl = new QuiverFileImpl(path, name);
}


QuiverFile::~QuiverFile()
{
	//if (NULL != m_pImpl)
	//{
	//	delete m_pImpl;
	//	m_pImpl = NULL;
	//}
}

QuiverFile& QuiverFile::operator=(const QuiverFile& qf)
{
	//if (this != &qf)
	//{
	//	if (NULL != m_pImpl)
	//	{
	//		delete m_pImpl;
	//		m_pImpl = NULL;
	//	}

	//	if (NULL != qf.m_pImpl)
	//	{
	//		m_pImpl = new QuiverFileImpl(*(qf.m_pImpl));
	//	}
	//}
	return *this;
}

bool QuiverFile::operator==(const QuiverFile& qf) const
{
	//if (NULL == m_pImpl && NULL == qf.m_pImpl) return true;
	//if (NULL == m_pImpl || NULL == qf.m_pImpl) return false;
	//return (m_pImpl->GetURI() == qf.m_pImpl->GetURI());
	return true;
}

bool QuiverFile::operator!=(const QuiverFile& qf) const
{
	//if (NULL == m_pImpl && NULL == qf.m_pImpl) return false;
	//if (NULL == m_pImpl || NULL == qf.m_pImpl) return true;
	//return (m_pImpl->GetURI() != qf.m_pImpl->GetURI());
	return false;
}

const gchar* QuiverFile::GetURI() const
{
	return "";//m_pImpl->GetURI();
}

GdkPixbuf* QuiverFile::GetThumbnail(int size)
{
	return NULL; //m_pImpl->GetThumbnail(size);
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
	return false; //m_pImpl->IsVideo();
}


ExifData* QuiverFile::GetExifData()
{
	return NULL; //m_pImpl->GetExifData(false);
}

bool QuiverFile::SetExifData(ExifData* pExifData)
{
	//m_pImpl->SetExifData(pExifData);
	return true;
}


unsigned long long QuiverFile::GetFileSize()
{
	return 0; //m_pImpl->GetFileSize();
}

GdkPixbuf* QuiverFile::GetIcon(int width_desired,int height_desired)
{
	return NULL; //m_pImpl->GetIcon(std::max(width_desired,height_desired));
}


std::string QuiverFile::GetFileName() const
{
	return ""; //m_pImpl->GetFileName();
}
std::string QuiverFile::GetFilePath() const
{
	return ""; //m_pImpl->GetFilePath();
}

int QuiverFile::GetWidth()
{
	return 0; //m_pImpl->GetWidth();
}
int QuiverFile::GetHeight()
{
	return 0; //m_pImpl->GetHeight();
}


int QuiverFile::GetOrientation()
{
	return 1; //m_pImpl->GetOrientation();
}

time_t QuiverFile::GetTimeT(bool fromExif) const
{
	return 0; //m_pImpl->GetTimeT(fromExif);
}


QuiverFile::QuiverFileImpl::QuiverFileImpl(const string& filePath, const string& fileName, GdkPixbuf* pThumb)
{

}

QuiverFile::QuiverFileImpl::QuiverFileImpl(const QuiverFileImpl& qf)
{

}

QuiverFile::QuiverFileImpl::~QuiverFileImpl()
{

}

void QuiverFile::QuiverFileImpl::Initialize()
{

}

string QuiverFile::QuiverFileImpl::GetFileName() const
{
	return m_strFileName;
}

string QuiverFile::QuiverFileImpl::GetFilePath() const
{
	return m_strFilePath;
}

const gchar* QuiverFile::QuiverFileImpl::GetURI() const
{
	return m_strURI.c_str();
}

const string& QuiverFile::QuiverFileImpl::GetMD5() const
{
	if (m_strMD5.empty())
	{
		if (IsDirectory())
		{
			//m_strMD5 = "d41d8cd98f00b204e9800998ecf8427e";
		}
		else
		{
			//CMD5 md5;
			//md5.Generate(m_strFilePath.c_str());
			//m_strMD5 = md5.GetHash();
		}
	}
	return m_strMD5;
}

const string& QuiverFile::QuiverFileImpl::GetFileExtension() const
{
	return m_strFileExtension;
}

void QuiverFile::QuiverFileImpl::SetFileName(const string& strFileName)
{
	m_strFileName = strFileName;
	SetURI();
}

void QuiverFile::QuiverFileImpl::SetFilePath(const string& strFilePath)
{
	m_strFilePath = strFilePath;
	SetURI();
}

void QuiverFile::QuiverFileImpl::SetURI()
{
	gchar* uri = g_filename_to_uri(m_strFilePath.c_str(), NULL, NULL);
	if (uri)
	{
		m_strURI = uri;
		g_free(uri);
	}
}

bool QuiverFile::QuiverFileImpl::IsDirectory() const
{
	if (m_iFileSize == -1)
	{
		const_cast<QuiverFileImpl*>(this)->ReadFileData();
	}
	return m_bIsDirectory;
}

bool QuiverFile::QuiverFileImpl::IsImage() const
{
	return m_bIsImage;
}

bool QuiverFile::QuiverFileImpl::IsVideo() const
{
	return m_bIsVideo;
}

bool QuiverFile::QuiverFileImpl::IsArchive() const
{
	return m_bIsArchive;
}

int QuiverFile::QuiverFileImpl::GetWidth() const
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

int QuiverFile::QuiverFileImpl::GetHeight() const
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

int QuiverFile::QuiverFileImpl::GetOrientation() const
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

int QuiverFile::QuiverFileImpl::GetFileSize() const
{
	if (-1 == m_iFileSize)
	{
		const_cast<QuiverFileImpl*>(this)->ReadFileData();
	}
	return m_iFileSize;
}

void QuiverFile::QuiverFileImpl::SetOrientation(int iOrientation)
{
	m_iOrientation = iOrientation;
}

void QuiverFile::QuiverFileImpl::SetExifData(ExifData* pExifData)
{
	if (NULL != m_pExifData)
	{
		exif_data_unref(m_pExifData);
		m_pExifData = NULL;
	}
	m_pExifData = pExifData; // assume new data is already ref'd
	m_bExifRead = true;
}

ExifData* QuiverFile::QuiverFileImpl::GetExifData(bool bMustExist)
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

void QuiverFile::QuiverFileImpl::ReadExifData()
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

void QuiverFile::QuiverFileImpl::ReadVideoData()
{
	//QuiverVideoOps::GetVideoDimensions(m_strFilePath.c_str(), &m_iWidth, &m_iHeight, &m_iOrientation);
}

void QuiverFile::QuiverFileImpl::ReadImageData()
{
	GetImageDimensions(m_strFilePath.c_str(), &m_iWidth, &m_iHeight);
	ReadExifData();
}

void QuiverFile::QuiverFileImpl::ReadFileData()
{
	struct stat stat_buf;
	if (0 == stat(m_strFilePath.c_str(), &stat_buf))
	{
		m_iFileSize = stat_buf.st_size;
		m_bIsDirectory = S_ISDIR(stat_buf.st_mode);
		m_cachedTimeT = stat_buf.st_mtime;
	}
}

GdkPixbuf* QuiverFile::QuiverFileImpl::GetIcon(int size)
{
	GdkPixbuf* pixbuf = NULL;
	if (IsDirectory())
	{
		GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
		GtkIconPaintable *icon_paintable = gtk_icon_theme_lookup_icon(icon_theme, "folder", NULL, size, 1, GTK_TEXT_DIR_NONE, (GtkIconLookupFlags)0);
		if (icon_paintable)
		{
			GdkPaintable* paintable = GDK_PAINTABLE(icon_paintable);
			//pixbuf = gdk_paintable_get_pixbuf(paintable);
			g_object_unref(icon_paintable);
		}
	}
	else if (IsArchive())
	{
		GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
		GtkIconPaintable *icon_paintable = gtk_icon_theme_lookup_icon(icon_theme, "package-x-generic", NULL, size, 1, GTK_TEXT_DIR_NONE, (GtkIconLookupFlags)0);
		if (icon_paintable)
		{
			GdkPaintable* paintable = GDK_PAINTABLE(icon_paintable);
			//pixbuf = gdk_paintable_get_pixbuf(paintable);
			g_object_unref(icon_paintable);
		}
	}
	else if (IsVideo())
	{
		GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
		GtkIconPaintable *icon_paintable = gtk_icon_theme_lookup_icon(icon_theme, "video-x-generic", NULL, size, 1, GTK_TEXT_DIR_NONE, (GtkIconLookupFlags)0);
		if (icon_paintable)
		{
			GdkPaintable* paintable = GDK_PAINTABLE(icon_paintable);
			//pixbuf = gdk_paintable_get_pixbuf(paintable);
			g_object_unref(icon_paintable);
		}
	}
	else
	{
		pixbuf = GetThumbnail(size);
	}
	return pixbuf;
}

GdkPixbuf* QuiverFile::QuiverFileImpl::GetThumbnail(int size)
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
			GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
			GError *error = NULL;
			if (gdk_pixbuf_loader_write(loader, (const guchar*)m_strFilePath.c_str(), m_strFilePath.length(), &error))
			{
				GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
				if (NULL != pixbuf)
				{
					m_pThumb = scale_pixbuf(pixbuf, size);
					m_thumbnailCache.AddPixbuf(m_strURI, m_pThumb);
					g_object_ref(m_pThumb);
				}
			}
			gdk_pixbuf_loader_close(loader, &error);
			g_object_unref(loader);
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

void QuiverFile::QuiverFileImpl::SetThumbnail(GdkPixbuf* pThumb)
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

void QuiverFile::QuiverFileImpl::RemoveFromThumbnailCache()
{
	m_thumbnailCache.RemovePixbuf(m_strURI);
}

time_t QuiverFile::QuiverFileImpl::GetTimeT(bool bUseExif) const
{
	if (bUseExif && IsImage())
	{
		//ReadExifData();
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
		//ReadFileData();
	}
	return m_cachedTimeT;
}

string QuiverFile::QuiverFileImpl::GetDate(const char* pszFormat, bool bUseExif)
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

ImageCache& QuiverFile::QuiverFileImpl::GetThumbnailCache()
{
	return m_thumbnailCache;
}

static GdkPixbuf* scale_pixbuf(GdkPixbuf* pixbuf, int size)
{
	gint width = gdk_pixbuf_get_width(pixbuf);
	gint height = gdk_pixbuf_get_height(pixbuf);
	gint dest_x = 0, dest_y = 0;
	gint dest_width, dest_height;

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
	GdkPixbufLoader *loader;
	GError *error = NULL;

	*width = -1;
	*height = -1;

	loader = gdk_pixbuf_loader_new();
	if (gdk_pixbuf_loader_write(loader, (const guchar*)pszFilename, strlen(pszFilename), &error))
	{
		*width = gdk_pixbuf_loader_get_pixbuf(loader) ? gdk_pixbuf_get_width(gdk_pixbuf_loader_get_pixbuf(loader)) : -1;
		*height = gdk_pixbuf_loader_get_pixbuf(loader) ? gdk_pixbuf_get_height(gdk_pixbuf_loader_get_pixbuf(loader)) : -1;
	}
	gdk_pixbuf_loader_close(loader, &error);
	g_object_unref(loader);
}

*/
