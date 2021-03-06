#ifndef FILE_IMAGELIST_H
#define FILE_IMAGELIST_H

#include <string>
#include <list>

#include "QuiverFile.h"
#include "ImageListEventSource.h"

class ImageListImpl;
typedef boost::shared_ptr<ImageListImpl> ImageListImplPtr;
	
class ImageList : public virtual ImageListEventSource
{
public:

	typedef enum 
	{
		SORT_BY_FILENAME,
		SORT_BY_FILENAME_NATURAL,
		SORT_BY_FILE_TYPE,
		SORT_BY_DATE,
		SORT_BY_RANDOM,
	} SortBy;


	ImageList();
	ImageList(bool bEnableMonitor);

	void SetImageList(const std::list<std::string> *file_list, bool bRecursive = false);
	void Add(const std::list<std::string> *file_list, bool bRecursive = false);
	void UpdateImageList(const std::list<std::string> *file_list);

	std::list<std::string> GetFolderList();
	std::list<std::string> GetFileList();

	std::vector<QuiverFile> GetQuiverFiles();

	void Remove(unsigned int iIndex);
	void RemoveRange(unsigned int iStart, unsigned int iEnd);

	// reload the list from the items in the maps (F5)
	void Reload();
	
	void Reverse(); // reverse the list

	void Clear();
	
	bool HasNext() const ;
	bool HasPrevious() const;
	
	bool Next();
	bool Previous();
	bool First();
	bool Last();

	unsigned int GetSize() const;
	unsigned int GetCurrentIndex() const;
	
	bool SetCurrentIndex(unsigned int new_index );

	QuiverFile GetNext() const;
	QuiverFile GetPrevious() const;
	QuiverFile GetCurrent() const;
	QuiverFile GetFirst() const;
	QuiverFile GetLast() const;

	QuiverFile Get(unsigned int n) const;
	QuiverFile operator[](unsigned int n);
	QuiverFile const operator[](unsigned int n) const;

	void Sort(SortBy o, bool bSortAscending = true);

private:
	ImageListImplPtr m_ImageListImplPtr;
	
};

typedef boost::shared_ptr<ImageList> ImageListPtr;

#endif
