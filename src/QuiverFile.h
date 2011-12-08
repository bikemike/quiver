#ifndef FILE_QUIVER_FILE_H
#define FILE_QUIVER_FILE_H


#include <string>

#include <gio/gio.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#include <boost/shared_ptr.hpp>


#include "ImageCache.h"

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

	const gchar* GetURI() const;

	bool HasThumbnail(int iSize = 0);

	bool Modified() const;

	bool IsVideo();

	GdkPixbuf *GetExifThumbnail();
	GdkPixbuf *GetThumbnail(int iSize = 0);
	
	void RemoveCachedThumbnail(int iSize = 0);

	ExifData *GetExifData();
	bool SetExifData(ExifData* pExifData);
	
	const char* GetMimeType();
	GFileInfo* GetFileInfo();

	unsigned long long GetFileSize();
	
	GdkPixbuf* GetIcon(int width_desired,int height_desired);
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
