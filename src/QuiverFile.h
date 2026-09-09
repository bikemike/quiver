#ifndef FILE_QUIVER_FILE_H
#define FILE_QUIVER_FILE_H


#include <memory>
#include <string>

#include <gio/gio.h>

#include <gdk/gdk.h>
#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

#include <boost/shared_ptr.hpp>

#include "QuiverVideoOps.h"

namespace Exiv2 {
class ExifData;
}

class QuiverFile {
	// Associations
	// Attributes
public:

	typedef enum _QuiverDataFlags
	{
		QUIVER_FILE_DATA_NONE = 0,
		QUIVER_FILE_DATA_INFO = 1 << 0,
		QUIVER_FILE_DATA_EXIF = 1 << 1,
		QUIVER_FILE_DATA_IPTC = 1 << 2,
		QUIVER_FILE_DATA_DB   = 1 << 3,
	} QuiverDataFlags;

	// Operations
	
	QuiverFile();
	QuiverFile(const gchar*  uri);
	QuiverFile(const gchar* , GFileInfo *info);
	~QuiverFile();

	static void ClearThumbnailCache();

	const gchar* GetURI() const;

	bool HasThumbnail(int iSize = 0);

	bool Modified() const;

	bool IsFolder() const;
	bool IsVideo();

	GdkTexture *GetThumbnailTexture(int iSize = 0,
		QuiverVideoOps::VideoAbortFn abort_fn = NULL,
		gpointer abort_data = NULL);
	GdkTexture *GetExifThumbnailTexture();
	GdkTexture *GetIconTexture(int width_desired, int height_desired);

#if HAVE_GDK_PIXBUF
	GdkPixbuf *GetExifThumbnail();
	GdkPixbuf *GetThumbnail(int iSize = 0,
		QuiverVideoOps::VideoAbortFn abort_fn = NULL,
		gpointer abort_data = NULL);
	GdkPixbuf* GetIcon(int width_desired,int height_desired);
#endif
	
	void RemoveCachedThumbnail(int iSize = 0);

	std::shared_ptr<Exiv2::ExifData> GetExifData();
	bool SetExifData(std::shared_ptr<Exiv2::ExifData> pExifData);
	
	const char* GetMimeType();
	GFileInfo* GetFileInfo();

	unsigned long long GetFileSize();
	
	gchar* GetIconName();
	
	void Reload();
	
	std::string GetFileName() const;
	std::string GetFilePath() const;

	bool IsWidthHeightSet() const;

	int GetWidth();
	int GetHeight();

	
	int GetOrientation() ;
	
	time_t GetTimeT(bool fromExif = true) const;
	
	double GetLoadTimeInSeconds() const;
	
	void SetWidth(int );
	void SetHeight(int);
	void SetLoadTimeInSeconds(double);
	bool IsWriteable();
	
	bool operator== (const QuiverFile &other) const;
	bool operator!= (const QuiverFile &other) const;

private:
	class QuiverFileImpl;
	boost::shared_ptr<QuiverFileImpl> m_QuiverFilePtr;

};

#endif
