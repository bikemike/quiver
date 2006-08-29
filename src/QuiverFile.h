#ifndef FILE_QUIVER_FILE_H
#define FILE_QUIVER_FILE_H


#include <string>
#include <libgnomevfs/gnome-vfs.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <exif-data.h>
#include <exif-loader.h>
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
	QuiverFile(const gchar* , GnomeVFSFileInfo *info);
	~QuiverFile();
	
	const gchar* GetURI();

	bool HasThumbnail(bool large = false);

	GdkPixbuf *GetExifThumbnail();
	GdkPixbuf *GetThumbnail(bool bLargeThumb = false);

	ExifData *GetExifData();
	GnomeVFSFileInfo * GetFileInfo();

	unsigned long long GetFileSize();
	
	GdkPixbuf *GetIcon(int width_desired,int height_desired);
	
	std::string GetFilePath();
	int GetWidth();
	int GetHeight();
	
	int GetOrientation();
	
	double GetLoadTimeInSeconds();
	
	void SetWidth(int );
	void SetHeight(int);
	void SetLoadTimeInSeconds(double);
	bool IsWriteable();
	

	
	
private:
	void LoadExifData();
	class QuiverFileImpl;
	boost::shared_ptr<QuiverFileImpl> m_QuiverFilePtr;

};

#endif
