#ifndef FILE_IMAGELIST_H
#define FILE_IMAGELIST_H

#include <string>
#include <list>
#include <set>

#include "QuiverFile.h"
#include "ImageListEventSource.h"
#include "IImageListView.h"
#include "ImageListAttributes.h"


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
		SORT_BY_FILE_SIZE,
		SORT_BY_RANDOM,
	} SortBy;


	ImageList();
	ImageList(bool bEnableMonitor);

	virtual void SetImageList(std::string file, bool bRecursive = false);
	virtual void SetImageList(const std::list<std::string> *file_list, bool bRecursive = false);
	virtual void SetToSingleFile(const std::string& uri, ImageListAttributesPtr attributes = ImageListAttributesPtr());
	virtual void Add(const std::list<std::string> *file_list, bool bRecursive = false);
	virtual void UpdateImageList(const std::list<std::string> *file_list);
	/* bPreserveCurrentIndex is for a request that merely extends the list
	 * already on screen (checking a folder in the folder tree), so the
	 * viewer keeps its place: a viewer on the first item stays on index 0,
	 * while any other index follows the current item to wherever the rebuilt
	 * list put it.  Mutually exclusive with bSelectFirstItem. */
	virtual void UpdateImageListAsync(const std::list<std::string> *file_list, bool bRecursive = false, bool bSelectFirstItem = false, const std::string& strSelectURI = "", const std::set<std::string>* pRecursiveFolders = NULL, bool bPreserveCurrentIndex = false);
	/* Load the folder set described by attributes, installing that exact
	 * attributes object as this list's current definition so its pointer
	 * identity is preserved for shared holders ("recently viewed"). */
	virtual void UpdateImageListAsync(ImageListAttributesPtr attributes, bool bSelectFirstItem = false, const std::string& strSelectURI = "");
	virtual void StopAsyncLoad();
	virtual void StopAsyncSort();

	/* The definition (root folder set + recursive flag) this list was built
	 * from.  May be NULL before the first load completes. */
	virtual ImageListAttributesPtr GetAttributes() const;

	static void AddIgnoredExtension(std::string ext);
	static void ClearIgnoreList(std::string ext);

	virtual std::list<std::string> GetFolderList();
	virtual std::list<std::string> GetFileList();

	virtual std::vector<QuiverFile> GetQuiverFiles();

	// Batch rename support
	virtual void ApplyRenames(const std::vector<std::pair<std::string, std::string>>& renames);
	virtual bool IsURIInImageList(const std::string& uri, const std::string& src_uri = "") const;

	// Static active operation tracking to suppress filesystem monitor chatter from internal tasks
	static void RegisterActiveRenames(const std::vector<std::pair<std::string, std::string>>& renames);
	static void UnregisterActiveRenames(const std::vector<std::pair<std::string, std::string>>& renames);
	static bool IsActiveRename(const std::string& uri);

	static void RegisterActiveCopy(const std::string& src_uri, const std::string& dst_uri);
	static void UnregisterActiveCopy(const std::string& dst_uri);
	static bool GetActiveCopySource(const std::string& dst_uri, std::string& src_uri);

	virtual void Remove(unsigned int iIndex);
	virtual void RemoveRange(unsigned int iStart, unsigned int iEnd);

	// reload the list from the items in the maps (F5)
	virtual void Reload();

	virtual void Reverse(); // reverse the list

	virtual void Clear();

	virtual bool HasNext() const;
	virtual bool HasPrevious() const;

	virtual bool Next();
	virtual bool Previous();
	virtual bool First();
	virtual bool Last();

	virtual unsigned int GetSize() const;
	virtual unsigned int GetCurrentIndex() const;

	virtual bool SetCurrentIndex(unsigned int new_index );
	virtual bool SetCurrentFile(std::string file);

	virtual QuiverFile GetNext() const;
	virtual QuiverFile GetPrevious() const;
	virtual QuiverFile GetCurrent() const;
	virtual QuiverFile GetFirst() const;
	virtual QuiverFile GetLast() const;

	virtual QuiverFile Get(unsigned int n) const;
	virtual QuiverFile operator[](unsigned int n);
	virtual QuiverFile const operator[](unsigned int n) const;

	virtual void Sort(SortBy o, bool bSortAscending = true, bool bAsync = false);
	virtual SortBy GetSortBy() const;
	virtual bool GetSortAscending() const;

	virtual bool IsLoading() const override;

public:
	class ImageListImpl;
	typedef boost::shared_ptr<ImageListImpl> ImageListImplPtr;

private:
	ImageListImplPtr m_ImageListImplPtr;
	static std::vector<std::string> m_vectIgnorgedExtensions;
	
};

typedef boost::shared_ptr<ImageList> ImageListPtr;

#endif
