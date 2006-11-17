#ifndef FILE_IMAGE_SAVE_MANAGER_H
#define FILE_IMAGE_SAVE_MANAGER_H

#include <boost/shared_ptr.hpp>
#include <map>

#include "QuiverFile.h"

class ImageSaveManager;
typedef boost::shared_ptr<ImageSaveManager> ImageSaveManagerPtr;

class IImageSaver;
typedef boost::shared_ptr<IImageSaver> IImageSaverPtr;

typedef std::map<std::string, IImageSaverPtr> ImageSaverMap;
typedef std::pair<std::string, IImageSaverPtr> ImageSaverPair;

class ImageSaveManager
{
public:
	static ImageSaveManagerPtr GetInstance();
	bool IsFormatSupported(std::string strMimeType);
	
	bool SaveImage(QuiverFile quiverFile, GdkPixbuf *pixbuf = NULL);
	bool SaveImageAs(QuiverFile quiverFile, std::string strFileName, GdkPixbuf *pixbuf = NULL);

	~ImageSaveManager();

private:
	ImageSaveManager();
	static ImageSaveManagerPtr c_pImageSaveManagerPtr;
	ImageSaverMap m_mapImageSavers;

};

class IImageSaver
{
public:
	virtual ~IImageSaver(){};

	virtual std::string GetMimeType() = 0;
	virtual bool SaveImage(QuiverFile quiverFile, GdkPixbuf *pixbuf = NULL) = 0;
	virtual bool SaveImageAs(QuiverFile quiverFile, std::string strFileName, GdkPixbuf *pixbuf = NULL) = 0;
	
	//FIXME: will need to add ability to save with progress

private:
};

#endif

