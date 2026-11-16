#include "ImageListFilter.h"

#include <algorithm>

ImageListFilter::ImageListFilter(IImageListViewPtr pSource, Predicate pred)
	: m_pSource(pSource), m_pred(pred), m_bMapValid(false), m_iLastSourceSize(0), m_iLastEmittedView(-1)
{
	m_pEventHandlerPtr.reset(new FilterEventHandler(this));
	if (m_pSource)
	{
		m_pSource->AddEventHandler(m_pEventHandlerPtr);
	}
}

ImageListFilter::~ImageListFilter()
{
	if (m_pSource)
	{
		m_pSource->RemoveEventHandler(m_pEventHandlerPtr);
	}
}

void ImageListFilter::SetPredicate(Predicate pred)
{
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		m_pred = pred;
		m_bMapValid = false;
		m_iLastSourceSize = 0;
	}
	/* Emit after unlocking: listeners may call back into this filter or its
	 * source, and holding m_MapMutex here inverts the lock order used by
	 * thumbnail loader threads (child filter -> source filter). */
	EmitContentsChangedEvent();
}

ImageListFilter::Predicate ImageListFilter::GetPredicate() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	return m_pred;
}

static bool ImageListFilterMatch(const ImageListFilter::Predicate& pred,
	const QuiverFile& file)
{
	return !pred || pred(file);
}

void ImageListFilter::RebuildIfNeeded() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	unsigned int n = m_pSource ? m_pSource->GetSize() : 0;
	if (m_bMapValid && m_iLastSourceSize == n)
		return;

	m_vectMap.clear();
	for (unsigned int i = 0 ; i < n ; i++)
	{
		if (ImageListFilterMatch(m_pred, SourceGet(i)))
			m_vectMap.push_back(i);
	}
	m_iLastSourceSize = n;
	m_bMapValid = true;
}

QuiverFile ImageListFilter::SourceGet(unsigned int iSourceIdx) const
{
	if (!m_pSource || iSourceIdx >= m_pSource->GetSize())
		return QuiverFile();
	return m_pSource->Get(iSourceIdx);
}

bool ImageListFilter::MapPosOfSource(unsigned int iSourceIdx, unsigned int& iPos) const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	RebuildIfNeeded();
	std::vector<unsigned int>::const_iterator itr =
		std::lower_bound(m_vectMap.begin(), m_vectMap.end(), iSourceIdx);
	if (m_vectMap.end() != itr && *itr == iSourceIdx)
	{
		iPos = itr - m_vectMap.begin();
		return true;
	}
	return false;
}

int ImageListFilter::ResolveViewIndex() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	RebuildIfNeeded();
	if (m_vectMap.empty())
	{
		m_iLastEmittedView = -1;
		return -1;
	}

	unsigned int iPos = 0;

	// 1. the tracked item is still in the view: keep it (fast path first)
	if (!m_szCurrentURI.empty())
	{
		std::vector<unsigned int>::size_type n = m_vectMap.size();
		int iStart = (0 <= m_iLastEmittedView &&
			(unsigned int)m_iLastEmittedView < n) ? m_iLastEmittedView : 0;

		for (unsigned int v = 0 ; v < n ; v++)
		{
			unsigned int iCur = (iStart + v) % n;
			const char* uri = SourceGet(m_vectMap[iCur]).GetURI();
			if (uri != NULL && m_szCurrentURI == uri)
			{
				m_iLastEmittedView = (int)iCur;
				return m_iLastEmittedView;
			}
		}

		// 2. the tracked item vanished from the source: re-anchor around
		//    its old position, previous preferred
		iPos = (0 <= m_iLastEmittedView)
			? std::min((unsigned int)m_iLastEmittedView, (unsigned int)(n - 1)) : 0;
	}
	else
	{
		// 3. nothing tracked yet: adopt the source's current if visible,
		//    otherwise the first visible item
		if (!MapPosOfSource(m_pSource->GetCurrentIndex(), iPos))
			iPos = 0;
	}

	const char* uri = SourceGet(m_vectMap[iPos]).GetURI();
	m_szCurrentURI = uri ? uri : "";
	m_iLastEmittedView = (int)iPos;
	return m_iLastEmittedView;
}

unsigned int ImageListFilter::GetSize() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	RebuildIfNeeded();
	return m_vectMap.size();
}

unsigned int ImageListFilter::GetCurrentIndex() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	int i = ResolveViewIndex();
	return (0 > i) ? 0 : (unsigned int)i;
}

bool ImageListFilter::HasNext() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	int i = ResolveViewIndex();
	return (0 <= i) && ((size_t)i + 1 < m_vectMap.size());
}

bool ImageListFilter::HasPrevious() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	int i = ResolveViewIndex();
	return (0 < i);
}

bool ImageListFilter::Next()
{
	unsigned int iTarget = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		if (!HasNext())
			return false;
		iTarget = (unsigned int)ResolveViewIndex() + 1;
	}
	return SetCurrentIndex(iTarget);
}

bool ImageListFilter::Previous()
{
	unsigned int iTarget = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		if (!HasPrevious())
			return false;
		iTarget = (unsigned int)ResolveViewIndex() - 1;
	}
	return SetCurrentIndex(iTarget);
}

bool ImageListFilter::First()
{
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		if (GetSize() == 0)
			return false;
	}
	return SetCurrentIndex(0);
}

bool ImageListFilter::Last()
{
	unsigned int iTarget = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		const unsigned int iSize = GetSize();
		if (iSize == 0)
			return false;
		iTarget = iSize - 1;
	}
	return SetCurrentIndex(iTarget);
}

bool ImageListFilter::SetCurrentIndex(unsigned int iIndex)
{
	unsigned int iSourceIndex = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		RebuildIfNeeded();
		if (iIndex >= m_vectMap.size())
			return false;
		iSourceIndex = m_vectMap[iIndex];
	}

	/* The source emits its change event synchronously while this call is in
	 * progress.  Do not hold our mutex across it: the event handler and the
	 * thumbnail loader take these mutexes in the opposite direction. */
	const bool bOk = m_pSource->SetCurrentIndex(iSourceIndex);
	if (bOk)
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		const char* uri = SourceGet(iSourceIndex).GetURI();
		m_szCurrentURI = uri ? uri : "";
	}
	return bOk;
}

QuiverFile ImageListFilter::GetCurrent() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	int i = ResolveViewIndex();
	if (0 > i)
		return QuiverFile();
	return SourceGet(m_vectMap[(size_t)i]);
}

QuiverFile ImageListFilter::GetNext() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	if (!HasNext())
		return QuiverFile();
	return Get((unsigned int)ResolveViewIndex() + 1);
}

QuiverFile ImageListFilter::GetPrevious() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	if (!HasPrevious())
		return QuiverFile();
	return Get((unsigned int)ResolveViewIndex() - 1);
}

QuiverFile ImageListFilter::GetFirst() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	if (0 == GetSize())
		return QuiverFile();
	return Get(0);
}

QuiverFile ImageListFilter::GetLast() const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	if (0 == GetSize())
		return QuiverFile();
	return Get(GetSize() - 1);
}

QuiverFile ImageListFilter::Get(unsigned int nIndex) const
{
	std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
	RebuildIfNeeded();
	if (nIndex >= m_vectMap.size())
		return QuiverFile();
	return SourceGet(m_vectMap[nIndex]);
}

void ImageListFilter::Remove(unsigned int nIndex)
{
	unsigned int iSourceIndex = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		RebuildIfNeeded();
		if (nIndex >= m_vectMap.size())
			return;
		iSourceIndex = m_vectMap[nIndex];
	}
	m_pSource->Remove(iSourceIndex);
}

// event translation
//
// Every event is emitted after releasing m_MapMutex.  Listeners can call
// back into this filter or the source chain, and worker threads acquire the
// child filter mutex before the source mutex; emitting under our lock would
// invert that order and deadlock.

void ImageListFilter::HandleContentsChanged(ImageListEventPtr event)
{ (void)event;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		m_bMapValid = false;
		m_szCurrentURI.clear();
		m_iLastSourceSize = 0;
		m_iLastEmittedView = -1;
	}
	EmitContentsChangedEvent();
}

void ImageListFilter::HandleCurrentIndexChanged(ImageListEventPtr event)
{
	bool bEmit = false;
	unsigned int iEmitNew = 0;
	unsigned int iEmitOld = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		const int iOld = m_iLastEmittedView;

		// a selection on a filtered-out item (e.g. a folder clicked in the
		// browser) must not move this view
		if (!ImageListFilterMatch(m_pred, SourceGet(event->GetIndex())))
			return;

		const char* uri = SourceGet(event->GetIndex()).GetURI();
		m_szCurrentURI = uri ? uri : "";
		const int iNew = ResolveViewIndex();

		if (iNew != iOld)
		{
			bEmit = true;
			iEmitNew = (0 <= iNew) ? (unsigned int)iNew : 0;
			iEmitOld = (0 < iOld) ? (unsigned int)iOld : 0;
		}
	}
	if (bEmit)
		EmitCurrentIndexChangedEvent(iEmitNew, iEmitOld);
}

void ImageListFilter::HandleItemAdded(ImageListEventPtr event)
{
	bool bEmit = false;
	unsigned int iPos = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		const bool bVisible = ImageListFilterMatch(m_pred,
			SourceGet(event->GetIndex()));
		m_bMapValid = false;
		if (bVisible)
		{
			MapPosOfSource(event->GetIndex(), iPos);
			bEmit = true;
		}
	}
	// an invisible addition does not change the view at all
	if (bEmit)
		EmitItemAddedEvent(iPos);
}

void ImageListFilter::HandleItemRemoved(ImageListEventPtr event)
{
	bool bEmit = false;
	unsigned int iOldPos = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		bEmit = MapPosOfSource(event->GetIndex(), iOldPos);
		m_bMapValid = false;
	}
	// if the tracked current vanished it is re-anchored lazily by
	// ResolveViewIndex()
	if (bEmit)
		EmitItemRemovedEvent(iOldPos);
}

void ImageListFilter::HandleItemChanged(ImageListEventPtr event)
{
	enum class Change { None, Changed, Added, Removed };
	Change change = Change::None;
	unsigned int iPos = 0;
	{
		std::lock_guard<std::recursive_mutex> lock(m_MapMutex);
		const unsigned int iSource = event->GetIndex();
		const unsigned int n = m_pSource ? m_pSource->GetSize() : 0;

		if (iSource >= n)
			return;

		/* A structural change may have left the map dirty.  Rebuild once
		 * here so this event still reaches listeners; subsequent item changes
		 * are applied incrementally. */
		if (!m_bMapValid)
			RebuildIfNeeded();
		if (m_iLastSourceSize != n)
			return;

		auto itr = std::lower_bound(m_vectMap.begin(), m_vectMap.end(),
			iSource);
		const bool bWasVisible = itr != m_vectMap.end() && *itr == iSource;
		unsigned int iOldPos = bWasVisible
			? (unsigned int)(itr - m_vectMap.begin()) : 0;
		const bool bIsVisible = ImageListFilterMatch(m_pred,
			SourceGet(iSource));

		if (bWasVisible && bIsVisible)
		{
			change = Change::Changed;
			iPos = iOldPos;
		}
		else if (!bWasVisible && bIsVisible)
		{
			/* An item that now matches is new to this view.  Add it in place
			 * rather than discarding the whole map and rescanning. */
			iPos = (unsigned int)(itr - m_vectMap.begin());
			m_vectMap.insert(itr, iSource);
			if (0 <= m_iLastEmittedView && iPos <= (unsigned int)m_iLastEmittedView)
				++m_iLastEmittedView;
			change = Change::Added;
		}
		else if (bWasVisible && !bIsVisible)
		{
			iPos = iOldPos;
			m_vectMap.erase(itr);
			if (0 <= m_iLastEmittedView && iPos < (unsigned int)m_iLastEmittedView)
				--m_iLastEmittedView;
			change = Change::Removed;
		}
	}
	switch (change)
	{
	case Change::Changed:
		EmitItemChangedEvent(iPos);
		break;
	case Change::Added:
		EmitItemAddedEvent(iPos);
		break;
	case Change::Removed:
		EmitItemRemovedEvent(iPos);
		break;
	case Change::None:
		break;
	}
}

bool ImageListFilter::IsLoading() const
{
	return m_pSource ? m_pSource->IsLoading() : false;
}

void ImageListFilter::HandleLoadProgress(double fraction, int current, int total)
{
	EmitLoadProgress(fraction, current, total);
}

// event handler plumbing

void ImageListFilter::FilterEventHandler::HandleContentsChanged(ImageListEventPtr event)
{ m_pParent->HandleContentsChanged(event); }
void ImageListFilter::FilterEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event)
{ m_pParent->HandleCurrentIndexChanged(event); }
void ImageListFilter::FilterEventHandler::HandleItemAdded(ImageListEventPtr event)
{ m_pParent->HandleItemAdded(event); }
void ImageListFilter::FilterEventHandler::HandleItemRemoved(ImageListEventPtr event)
{ m_pParent->HandleItemRemoved(event); }
void ImageListFilter::FilterEventHandler::HandleItemChanged(ImageListEventPtr event)
{ m_pParent->HandleItemChanged(event); }
void ImageListFilter::FilterEventHandler::HandleLoadProgress(double fraction, int current, int total)
{ m_pParent->HandleLoadProgress(fraction, current, total); }
