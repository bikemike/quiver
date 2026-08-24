#ifndef FILE_IMAGELIST_H
#define FILE_IMAGELIST_H

#include <string>
#include <list>

#include "QuiverFile.h"
#include "ImageListEventSource.h"
#include "IImageListView.h"


class ImageList : public virtual IImageListView
{
public:

	typedef enum 
	{
		SORT_BY_FILENAME,
		SORT_BY_FILENAME_NATURAL,
		SORT_BY_FILE_TYPE,
		SORT_BY_DATE,
		SORT_BY_DATE_MODIFIED,
		SORT_BY_RANDOM,
	} SortBy;


	ImageList();
	ImageList(bool bEnableMonitor);

	void SetImageList(std::string file, bool bRecursive = false);
	void SetImageList(const std::list<std::string> *file_list, bool bRecursive = false);
	void Add(const std::list<std::string> *file_list, bool bRecursive = false);
	void UpdateImageList(const std::list<std::string> *file_list);
	void UpdateImageListAsync(const std::list<std::string> *file_list, bool bRecursive = false);

	static void AddIgnoredExtension(std::string ext);
	static void ClearIgnoreList(std::string ext);

	std::list<std::string> GetFolderList();
	std::list<std::string> GetFileList();

	std::vector<QuiverFile> GetQuiverFiles();

	virtual void Remove(unsigned int iIndex);
	void RemoveRange(unsigned int iStart, unsigned int iEnd);

	// reload the list from the items in the maps (F5)
	void Reload();

	void Reverse(); // reverse the list

	void Clear();

	virtual bool HasNext() const;
	virtual bool HasPrevious() const;

	virtual bool Next();
	virtual bool Previous();
	virtual bool First();
	virtual bool Last();

	virtual unsigned int GetSize() const;
	virtual unsigned int GetCurrentIndex() const;

	virtual bool SetCurrentIndex(unsigned int new_index );
	bool SetCurrentFile(std::string file);

	virtual QuiverFile GetNext() const;
	virtual QuiverFile GetPrevious() const;
	virtual QuiverFile GetCurrent() const;
	virtual QuiverFile GetFirst() const;
	virtual QuiverFile GetLast() const;

	virtual QuiverFile Get(unsigned int n) const;
	QuiverFile operator[](unsigned int n);
	QuiverFile const operator[](unsigned int n) const;

	void Sort(SortBy o, bool bSortAscending = true);

public:
	class ImageListImpl;
	typedef boost::shared_ptr<ImageListImpl> ImageListImplPtr;

private:
	ImageListImplPtr m_ImageListImplPtr;
	static std::vector<std::string> m_vectIgnorgedExtensions;
	
};

typedef boost::shared_ptr<ImageList> ImageListPtr;

#endif
