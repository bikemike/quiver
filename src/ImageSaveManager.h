#ifndef FILE_IMAGE_SAVE_MANAGER_H
#define FILE_IMAGE_SAVE_MANAGER_H

#include <boost/shared_ptr.hpp>
#include <map>

#include "QuiverFile.h"

class ImageSaveManager;
typedef boost::shared_ptr<ImageSaveManager> ImageSaveManagerPtr;

class IImageSaver
{
public:
	virtual ~IImageSaver(){};

	// progress callback
	typedef void (*ImageSaveProgressCallback) (double dProgess, void* user_data);

	virtual std::string GetMimeType() = 0;

	virtual bool SaveImage(QuiverFile quiverFile,
			GdkPixbuf *pixbuf = NULL,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL) = 0;

	virtual bool SaveImageAs(QuiverFile quiverFile, std::string strFileName,
			GdkPixbuf *pixbuf = NULL,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL) = 0;
private:
};

typedef boost::shared_ptr<IImageSaver> IImageSaverPtr;

typedef std::map<std::string, IImageSaverPtr> ImageSaverMap;
typedef std::pair<std::string, IImageSaverPtr> ImageSaverPair;

class ImageSaveManager : public IImageSaver
{
public:
	static ImageSaveManagerPtr GetInstance();
	bool IsFormatSupported(std::string strMimeType);
	
	virtual std::string GetMimeType(){return "";}

	bool SaveImage(QuiverFile quiverFile,
			GdkPixbuf *pixbuf = NULL,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL);

	bool SaveImageAs(QuiverFile quiverFile, std::string strFileName,
			GdkPixbuf *pixbuf = NULL,
		   	ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL);

	~ImageSaveManager();


private:
	ImageSaveManager();
	static ImageSaveManagerPtr c_pImageSaveManagerPtr;
	ImageSaverMap m_mapImageSavers;

};

#endif

