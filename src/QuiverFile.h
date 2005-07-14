#ifndef FILE_QUIVER_FILE_H
#define FILE_QUIVER_FILE_H


#include <string>
#include <libgnomevfs/gnome-vfs.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <exif-data.h>
#include <exif-loader.h>
#include <boost/shared_ptr.hpp>


#include "ImageCache.h"


struct IptcData;
//struct GnomeVFSFileInfo;
struct DBData;

class QuiverFile {
	// Associations
	// Attributes
public:
	boost::shared_ptr<int> intPtr;

	typedef enum _QuiverDataFlags
	{
		QUIVER_FILE_DATA_NONE = 0,
		QUIVER_FILE_DATA_INFO = 1 << 0,
		QUIVER_FILE_DATA_EXIF = 1 << 1,
		QUIVER_FILE_DATA_IPTC = 1 << 2,
		QUIVER_FILE_DATA_DB = 1 << 3,
	} QuiverDataFlags;

	// Operations
	
	QuiverFile(const gchar*  uri);
	QuiverFile(const gchar* , GnomeVFSFileInfo *info);
	~QuiverFile();
	
	const gchar* GetURI(); // get the uri for this QuiverFile
	//static QuiverFile GetInstance(std::string path);

	// copy constructor
	QuiverFile(const QuiverFile& a);
	// operator overloading
	QuiverFile& QuiverFile::operator=(const QuiverFile &rhs);
	GdkPixbuf * GetThumbnail();
	GdkPixbuf * GetExifThumbnail();
	void LoadExifData();
	ExifData *GetExifData();
	int GetExifOrientation();
	GnomeVFSFileInfo * GetFileInfo();

	
private:
	//GnomeVFSFileInfo *info;
	//gnome_vfs_make_uri_canonical
	//gnome_vfs_get_local_path_from_uri
	int * p_ref_count;
	gchar* m_szURI;

	GnomeVFSFileInfo *file_info;
 	ExifData *exif_data;
	IptcData *iptc_data;
	DBData *db_data;
	
	// if load is successful, set the flags accordingly
	QuiverDataFlags *data_loaded_flags;
	// if the load of data is attempted, set flag accordingly
	QuiverDataFlags *data_exist_flags;
	static ImageCache c_ThumbnailCache;
};

#endif
