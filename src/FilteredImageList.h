#ifndef FILTERED_IMAGE_LIST_H
#define FILTERED_IMAGE_LIST_H

#include <unordered_map>

#include "ImageList.h"
#include "IImageListEventHandler.h"

// ImageList facade used by Browser.  Structural operations (folder loading,
// sorting, reloads, renames) go to the master list; reads and navigation go
// to the filtered display view.  Events from the display view are re-emitted
// on this object so Browser's existing handler keeps working unchanged.
class FilteredImageList : public ImageList
{
public:
	FilteredImageList(ImageListPtr master, IImageListViewPtr view);
	~FilteredImageList() override;

	void SetView(IImageListViewPtr view);
	IImageListViewPtr GetView() const { return m_View; }

	void SetImageList(std::string file, bool bRecursive = false) override;
	void SetImageList(const std::list<std::string> *file_list, bool bRecursive = false) override;
	void SetToSingleFile(const std::string& uri, ImageListAttributesPtr attributes = ImageListAttributesPtr()) override;
	void Add(const std::list<std::string> *file_list, bool bRecursive = false) override;
	void UpdateImageList(const std::list<std::string> *file_list) override;
	void UpdateImageListAsync(const std::list<std::string> *file_list, bool bRecursive = false, bool bSelectFirstItem = false, const std::string& strSelectURI = "", const std::set<std::string>* pRecursiveFolders = NULL, bool bPreserveCurrentIndex = false) override;
	void UpdateImageListAsync(ImageListAttributesPtr attributes, bool bSelectFirstItem = false, const std::string& strSelectURI = "") override;
	void StopAsyncLoad() override;
	void StopAsyncSort() override;
	ImageListAttributesPtr GetAttributes() const override;
	std::list<std::string> GetFolderList() override;
	std::list<std::string> GetFileList() override;
	std::vector<QuiverFile> GetQuiverFiles() override;
	void ApplyRenames(const std::vector<std::pair<std::string, std::string>>& renames) override;
	bool IsURIInImageList(const std::string& uri, const std::string& src_uri = "") const override;
	void RemoveRange(unsigned int iStart, unsigned int iEnd) override;
	void Reload() override;
	void Reverse() override;
	void Clear() override;
	bool SetCurrentFile(std::string file) override;
	void Sort(SortBy o, bool bSortAscending = true, bool bAsync = false) override;
	SortBy GetSortBy() const override;
	bool GetSortAscending() const override;

	/* Translate icon-view (display subset) indices back to master-list
	 * indices.  Browser consumers that act on the master list need this when
	 * a filter hides items. */
	std::list<unsigned int> MapViewIndicesToMaster(
		const std::list<unsigned int>& viewIndices) const;

	unsigned int GetSize() const override;
	unsigned int GetCurrentIndex() const override;
	bool HasNext() const override;
	bool HasPrevious() const override;
	bool Next() override;
	bool Previous() override;
	bool First() override;
	bool Last() override;
	bool SetCurrentIndex(unsigned int new_index) override;
	QuiverFile GetCurrent() const override;
	QuiverFile GetNext() const override;
	QuiverFile GetPrevious() const override;
	QuiverFile GetFirst() const override;
	QuiverFile GetLast() const override;
	QuiverFile Get(unsigned int n) const override;
	void Remove(unsigned int nIndex) override;
	bool IsLoading() const override;
	QuiverFile operator[](unsigned int n) override;
	QuiverFile const operator[](unsigned int n) const override;

private:
	class ViewHandler;
	/* The master list is asked for a selection's real indices on every
	 * browser selection change.  Rebuilding a URI->index map across the whole
	 * list there made arrow-key navigation O(list size) per key, so the map
	 * is built lazily and invalidated by the display view's structural
	 * events. */
	void InvalidateMasterIndex() const { m_bMasterIndexValid = false; }
	ImageListPtr m_Master;
	IImageListViewPtr m_View;
	IImageListEventHandlerPtr m_ViewHandler;
	mutable std::unordered_map<std::string, unsigned int> m_MasterIndexByURI;
	mutable unsigned int m_MasterIndexSize = 0;
	mutable bool m_bMasterIndexValid = false;
};

#endif
