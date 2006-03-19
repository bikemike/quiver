#ifndef FILE_IMAGELIST_H
#define FILE_IMAGELIST_H

#include "QuiverFile.h"
#include <string>
#include <list>
#include <set>

	
class ImageList
{
public:

	typedef enum 
	{
		SORT_FILENAME,
		SORT_FILE_EXTENSION,
		SORT_FILE_DATE,
		SORT_EXIF_DATE
	} SortOrder;


	ImageList();
	ImageList(std::string filepath);
	
	//void LoadImageList(std::string path, std::string filename);
	
	void SetImageList(std::list<std::string> file_list);
	void SetImageList(std::list<std::string> *file_list);
	void AddImageList(std::list<std::string> *file_list);

	//void ImageList::AddDirectory(GnomeVFSFileInfo *info);
	
	void ImageList::AddDirectory(const gchar* uri);

	void ImageList::AddFile(const gchar*  uri);
	void ImageList::AddFile(const gchar* uri,GnomeVFSFileInfo *info);
	//void ImageList::AddFile(std::string uri);
	void ImageList::SetCurrentImage(std::string uri);
	void ImageList::RemoveCurrentImage();
	
	
	
	bool HasNext();
	bool HasPrevious();
	QuiverFile* GetNext();
	QuiverFile* PeekNext();
	QuiverFile* GetPrevious();
	QuiverFile* PeekPrevious();
	
	QuiverFile* GetLast();
	QuiverFile* GetFirst();
	
	QuiverFile* GetCurrent();
	int GetCurrentIndex();

	int GetSize();



	
	static void LoadMimeTypes();

	static bool QuiverFileCompare(const QuiverFile & left , const QuiverFile & right);
	void Sort(SortOrder o,bool reverse);
private:

	static SortOrder c_SortOrder;
	static std::set<std::string> c_setSupportedMimeTypes;
	
	std::list<QuiverFile> m_ImageList;
	std::list<QuiverFile>::iterator m_itrCurrent;

	int m_iIndex;
	
};


#endif
