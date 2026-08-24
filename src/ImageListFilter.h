#ifndef FILE_IMAGE_LIST_FILTER_H
#define FILE_IMAGE_LIST_FILTER_H

#include <functional>
#include <string>
#include <vector>

#include "IImageListView.h"
#include "IImageListEventHandler.h"

// Decorates an IImageListView (an ImageList or another filter) with a
// predicate, exposing only the matching items as a live view.
//
// - The view->source index map is rebuilt lazily whenever the source
//   reports structural changes.
// - The current item is sticky: it only moves when the source selects a
//   visible item or the view navigates. When the source's current lands on
//   a filtered-out item, the view keeps its own current; when a tracked
//   item disappears from the source, the view re-anchors to the nearest
//   surviving neighbour (previous preferred).
// - Navigation and selection are forwarded to the source so every
//   consumer of the shared list stays in sync.
// - Source events are re-emitted with translated indices; changes that
//   do not affect the filtered subset are swallowed.
class ImageListFilter : public virtual IImageListView
{
public:
	typedef std::function<bool(const QuiverFile&)> Predicate;

	ImageListFilter(IImageListViewPtr pSource, Predicate pred);
	virtual ~ImageListFilter();

	virtual unsigned int GetSize() const;
	virtual unsigned int GetCurrentIndex() const;

	virtual bool HasNext() const;
	virtual bool HasPrevious() const;
	virtual bool Next();
	virtual bool Previous();
	virtual bool First();
	virtual bool Last();
	virtual bool SetCurrentIndex(unsigned int iIndex);

	virtual QuiverFile GetCurrent() const;
	virtual QuiverFile GetNext() const;
	virtual QuiverFile GetPrevious() const;
	virtual QuiverFile GetFirst() const;
	virtual QuiverFile GetLast() const;
	virtual QuiverFile Get(unsigned int nIndex) const;

	virtual void Remove(unsigned int nIndex);

protected:

	class FilterEventHandler : public IImageListEventHandler
	{
	public:
		FilterEventHandler(ImageListFilter* pParent) : m_pParent(pParent) {}
		virtual void HandleContentsChanged(ImageListEventPtr event);
		virtual void HandleCurrentIndexChanged(ImageListEventPtr event);
		virtual void HandleItemAdded(ImageListEventPtr event);
		virtual void HandleItemRemoved(ImageListEventPtr event);
		virtual void HandleItemChanged(ImageListEventPtr event);
	private:
		ImageListFilter* m_pParent;
	};

	// rebuilds the map when dirty; called from const getters
	void RebuildIfNeeded() const;
	int ResolveViewIndex() const; // resolves + refreshes m_iLastEmittedView / m_szCurrentURI
	bool MapPosOfSource(unsigned int iSourceIdx, unsigned int& iPos) const;

	void HandleContentsChanged(ImageListEventPtr event);
	void HandleCurrentIndexChanged(ImageListEventPtr event);
	void HandleItemAdded(ImageListEventPtr event);
	void HandleItemRemoved(ImageListEventPtr event);
	void HandleItemChanged(ImageListEventPtr event);

	IImageListViewPtr m_pSource;
	Predicate         m_pred;
	IImageListEventHandlerPtr m_pEventHandlerPtr;

	mutable std::vector<unsigned int> m_vectMap; // view idx -> source idx
	mutable bool                      m_bMapValid;
	mutable std::string               m_szCurrentURI; // tracked current item; empty = unset
	mutable int                       m_iLastEmittedView; // -1 = none
};

#endif // FILE_IMAGE_LIST_FILTER_H
