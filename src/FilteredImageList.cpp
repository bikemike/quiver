#include "FilteredImageList.h"

#include "ImageListEvent.h"
#include "QuiverMetrics.h"

#include <unordered_map>

class FilteredImageList::ViewHandler : public IImageListEventHandler
{
public:
	explicit ViewHandler(FilteredImageList* owner) : m_Owner(owner) {}

	void HandleContentsChanged(ImageListEventPtr) override
	{
		m_Owner->InvalidateMasterIndex();
		m_Owner->EmitContentsChangedEvent();
	}
	void HandleCurrentIndexChanged(ImageListEventPtr event) override
	{
		m_Owner->EmitCurrentIndexChangedEvent(event->GetIndex(),
			event->GetOldIndex());
	}
	void HandleItemAdded(ImageListEventPtr event) override
	{
		m_Owner->InvalidateMasterIndex();
		m_Owner->EmitItemAddedEvent(event->GetIndex());
	}
	void HandleItemRemoved(ImageListEventPtr event) override
	{
		m_Owner->InvalidateMasterIndex();
		m_Owner->EmitItemRemovedEvent(event->GetIndex());
	}
	void HandleItemChanged(ImageListEventPtr event) override
	{
		/* The master-index cache is keyed by URI.  An item change (metadata
		 * save, thumbnail update, ...) does not change that mapping, so
		 * invalidating here made every metadata/thumbnail event rebuild a
		 * 60k-entry URI map on the next browser selection query.  Renames
		 * invalidate explicitly through ApplyRenames(). */
		m_Owner->EmitItemChangedEvent(event->GetIndex());
	}
	void HandleLoadProgress(double fraction, int current, int total) override
	{
		m_Owner->EmitLoadProgress(fraction, current, total);
	}

private:
	FilteredImageList* m_Owner;
};

FilteredImageList::FilteredImageList(ImageListPtr master,
	IImageListViewPtr view)
	: ImageList(false), m_Master(master), m_View(view)
{
	if (m_View)
	{
		m_ViewHandler = IImageListEventHandlerPtr(new ViewHandler(this));
		m_View->AddEventHandler(m_ViewHandler);
	}
}

FilteredImageList::~FilteredImageList()
{
	if (m_View && m_ViewHandler)
		m_View->RemoveEventHandler(m_ViewHandler);
	m_ViewHandler.reset();
}

void FilteredImageList::SetView(IImageListViewPtr view)
{
	if (m_View == view)
		return;
	if (m_View && m_ViewHandler)
		m_View->RemoveEventHandler(m_ViewHandler);
	InvalidateMasterIndex();
	m_View = view;
	if (m_View)
	{
		if (!m_ViewHandler)
			m_ViewHandler = IImageListEventHandlerPtr(new ViewHandler(this));
		m_View->AddEventHandler(m_ViewHandler);
	}
	EmitContentsChangedEvent();
}

void FilteredImageList::SetImageList(std::string file, bool bRecursive)
{
	if (m_Master) m_Master->SetImageList(std::move(file), bRecursive);
}
void FilteredImageList::SetImageList(const std::list<std::string>* file_list,
	bool bRecursive)
{
	if (m_Master) m_Master->SetImageList(file_list, bRecursive);
}
void FilteredImageList::SetToSingleFile(const std::string& uri,
	ImageListAttributesPtr attributes)
{
	if (m_Master) m_Master->SetToSingleFile(uri, attributes);
}
void FilteredImageList::Add(const std::list<std::string>* file_list,
	bool bRecursive)
{
	if (m_Master) m_Master->Add(file_list, bRecursive);
}
void FilteredImageList::UpdateImageList(const std::list<std::string>* file_list)
{
	if (m_Master) m_Master->UpdateImageList(file_list);
}
void FilteredImageList::UpdateImageListAsync(const std::list<std::string>* file_list,
	bool bRecursive, bool bSelectFirstItem, const std::string& strSelectURI,
	const std::set<std::string>* pRecursiveFolders, bool bPreserveCurrentIndex)
{
	if (m_Master) m_Master->UpdateImageListAsync(file_list, bRecursive,
		bSelectFirstItem, strSelectURI, pRecursiveFolders, bPreserveCurrentIndex);
}
void FilteredImageList::UpdateImageListAsync(ImageListAttributesPtr attributes,
	bool bSelectFirstItem, const std::string& strSelectURI)
{
	if (m_Master) m_Master->UpdateImageListAsync(attributes, bSelectFirstItem,
		strSelectURI);
}
void FilteredImageList::StopAsyncLoad()
{
	if (m_Master) m_Master->StopAsyncLoad();
}
void FilteredImageList::StopAsyncSort()
{
	if (m_Master) m_Master->StopAsyncSort();
}
ImageListAttributesPtr FilteredImageList::GetAttributes() const
{
	return m_Master ? m_Master->GetAttributes() : ImageListAttributesPtr();
}
std::list<std::string> FilteredImageList::GetFolderList()
{
	return m_Master ? m_Master->GetFolderList() : std::list<std::string>();
}
std::list<std::string> FilteredImageList::GetFileList()
{
	return m_Master ? m_Master->GetFileList() : std::list<std::string>();
}
std::vector<QuiverFile> FilteredImageList::GetQuiverFiles()
{
	return m_Master ? m_Master->GetQuiverFiles() : std::vector<QuiverFile>();
}
void FilteredImageList::ApplyRenames(
	const std::vector<std::pair<std::string, std::string>>& renames)
{
	if (m_Master) m_Master->ApplyRenames(renames);
	/* URI-keyed master indices are stale after a rename, even though the
	 * view only emits ItemChanged for the affected rows. */
	InvalidateMasterIndex();
}
bool FilteredImageList::IsURIInImageList(const std::string& uri,
	const std::string& src_uri) const
{
	return m_Master ? m_Master->IsURIInImageList(uri, src_uri) : false;
}
void FilteredImageList::RemoveRange(unsigned int iStart, unsigned int iEnd)
{
	if (m_Master) m_Master->RemoveRange(iStart, iEnd);
}
void FilteredImageList::Reload()
{
	if (m_Master) m_Master->Reload();
}
void FilteredImageList::Reverse()
{
	if (m_Master) m_Master->Reverse();
}
void FilteredImageList::Clear()
{
	if (m_Master) m_Master->Clear();
}
bool FilteredImageList::SetCurrentFile(std::string file)
{
	const bool ok = m_Master ? m_Master->SetCurrentFile(file) : false;
	if (!m_View || m_View->GetSize() == 0)
		return ok;

	if (m_View->GetCurrent().GetURI() == file)
		return ok;

	for (unsigned int i = 0; i < m_View->GetSize(); i++)
	{
		if (m_View->Get(i).GetURI() == file)
		{
			m_View->SetCurrentIndex(i);
			break;
		}
	}
	return ok;
}
void FilteredImageList::Sort(SortBy o, bool bSortAscending, bool bAsync)
{
	if (m_Master) m_Master->Sort(o, bSortAscending, bAsync);
}
ImageList::SortBy FilteredImageList::GetSortBy() const
{
	return m_Master ? m_Master->GetSortBy() : SORT_BY_FILENAME;
}
bool FilteredImageList::GetSortAscending() const
{
	return m_Master ? m_Master->GetSortAscending() : true;
}

std::list<unsigned int> FilteredImageList::MapViewIndicesToMaster(
	const std::list<unsigned int>& viewIndices) const
{
	if (!m_Master)
		return viewIndices;
	if (viewIndices.empty())
		return {};

	const unsigned int masterSize = m_Master->GetSize();
	if (!m_bMasterIndexValid || m_MasterIndexSize != masterSize)
	{
		QuiverMetricTimer t_build("filtered", "master index rebuild");
		m_MasterIndexByURI.clear();
		m_MasterIndexByURI.reserve(masterSize);
		for (unsigned int i = 0; i < masterSize; i++)
		{
			const gchar* uri = (*m_Master)[i].GetURI();
			if (uri)
				m_MasterIndexByURI.emplace(uri, i);
		}
		m_MasterIndexSize = masterSize;
		m_bMasterIndexValid = true;
		quiver_metric_emit("filtered", "master index size",
			(gdouble)masterSize, "count");
	}

	std::list<unsigned int> result;
	const unsigned int viewSize = GetSize();
	for (unsigned int viewIndex : viewIndices)
	{
		if (viewIndex >= viewSize)
			continue;
		const gchar* uri = Get(viewIndex).GetURI();
		if (!uri)
			continue;
		auto it = m_MasterIndexByURI.find(uri);
		if (it != m_MasterIndexByURI.end())
			result.push_back(it->second);
	}
	return result;
}

unsigned int FilteredImageList::GetSize() const
{
	return m_View ? m_View->GetSize() : 0;
}
unsigned int FilteredImageList::GetCurrentIndex() const
{
	return m_View ? m_View->GetCurrentIndex() : 0;
}
bool FilteredImageList::HasNext() const
{
	return m_View && m_View->HasNext();
}
bool FilteredImageList::HasPrevious() const
{
	return m_View && m_View->HasPrevious();
}
bool FilteredImageList::Next()
{
	return m_View && m_View->Next();
}
bool FilteredImageList::Previous()
{
	return m_View && m_View->Previous();
}
bool FilteredImageList::First()
{
	return m_View && m_View->First();
}
bool FilteredImageList::Last()
{
	return m_View && m_View->Last();
}
bool FilteredImageList::SetCurrentIndex(unsigned int new_index)
{
	return m_View && m_View->SetCurrentIndex(new_index);
}
QuiverFile FilteredImageList::GetCurrent() const
{
	return m_View ? m_View->GetCurrent() : QuiverFile();
}
QuiverFile FilteredImageList::GetNext() const
{
	return m_View ? m_View->GetNext() : QuiverFile();
}
QuiverFile FilteredImageList::GetPrevious() const
{
	return m_View ? m_View->GetPrevious() : QuiverFile();
}
QuiverFile FilteredImageList::GetFirst() const
{
	return m_View ? m_View->GetFirst() : QuiverFile();
}
QuiverFile FilteredImageList::GetLast() const
{
	return m_View ? m_View->GetLast() : QuiverFile();
}
QuiverFile FilteredImageList::Get(unsigned int n) const
{
	return m_View ? m_View->Get(n) : QuiverFile();
}
void FilteredImageList::Remove(unsigned int nIndex)
{
	if (m_View) m_View->Remove(nIndex);
	else if (m_Master) m_Master->Remove(nIndex);
}
bool FilteredImageList::IsLoading() const
{
	return m_View ? m_View->IsLoading() : false;
}
QuiverFile FilteredImageList::operator[](unsigned int n)
{
	return Get(n);
}
QuiverFile const FilteredImageList::operator[](unsigned int n) const
{
	return Get(n);
}
