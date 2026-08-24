#include "ImageListFilter.h"

#include <algorithm>

ImageListFilter::ImageListFilter(IImageListViewPtr pSource, Predicate pred)
	: m_pSource(pSource), m_pred(pred), m_bMapValid(false), m_iLastEmittedView(-1)
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

void ImageListFilter::RebuildIfNeeded() const
{
	if (m_bMapValid)
		return;

	m_vectMap.clear();
	unsigned int n = m_pSource ? m_pSource->GetSize() : 0;
	for (unsigned int i = 0 ; i < n ; i++)
	{
		if (m_pred(m_pSource->Get(i)))
			m_vectMap.push_back(i);
	}
	m_bMapValid = true;
}

bool ImageListFilter::MapPosOfSource(unsigned int iSourceIdx, unsigned int& iPos) const
{
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
			if (m_szCurrentURI == m_pSource->Get(m_vectMap[iCur]).GetURI())
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

	m_szCurrentURI = m_pSource->Get(m_vectMap[iPos]).GetURI();
	m_iLastEmittedView = (int)iPos;
	return m_iLastEmittedView;
}

unsigned int ImageListFilter::GetSize() const
{
	RebuildIfNeeded();
	return m_vectMap.size();
}

unsigned int ImageListFilter::GetCurrentIndex() const
{
	int i = ResolveViewIndex();
	return (0 > i) ? 0 : (unsigned int)i;
}

bool ImageListFilter::HasNext() const
{
	int i = ResolveViewIndex();
	return (0 <= i) && ((size_t)i + 1 < m_vectMap.size());
}

bool ImageListFilter::HasPrevious() const
{
	int i = ResolveViewIndex();
	return (0 < i);
}

bool ImageListFilter::Next()
{
	if (!HasNext())
		return false;
	return SetCurrentIndex((unsigned int)ResolveViewIndex() + 1);
}

bool ImageListFilter::Previous()
{
	if (!HasPrevious())
		return false;
	return SetCurrentIndex((unsigned int)ResolveViewIndex() - 1);
}

bool ImageListFilter::First()
{
	return SetCurrentIndex(0);
}

bool ImageListFilter::Last()
{
	return SetCurrentIndex(GetSize() - 1);
}

bool ImageListFilter::SetCurrentIndex(unsigned int iIndex)
{
	RebuildIfNeeded();
	if (iIndex >= m_vectMap.size())
		return false;
	bool bOk = m_pSource->SetCurrentIndex(m_vectMap[iIndex]);
	if (bOk)
		m_szCurrentURI = m_pSource->Get(m_vectMap[iIndex]).GetURI();
	return bOk;
}

QuiverFile ImageListFilter::GetCurrent() const
{
	int i = ResolveViewIndex();
	if (0 > i)
		return QuiverFile();
	return m_pSource->Get(m_vectMap[(size_t)i]);
}

QuiverFile ImageListFilter::GetNext() const
{
	if (!HasNext())
		return QuiverFile();
	return Get((unsigned int)ResolveViewIndex() + 1);
}

QuiverFile ImageListFilter::GetPrevious() const
{
	if (!HasPrevious())
		return QuiverFile();
	return Get((unsigned int)ResolveViewIndex() - 1);
}

QuiverFile ImageListFilter::GetFirst() const
{
	if (0 == GetSize())
		return QuiverFile();
	return Get(0);
}

QuiverFile ImageListFilter::GetLast() const
{
	if (0 == GetSize())
		return QuiverFile();
	return Get(GetSize() - 1);
}

QuiverFile ImageListFilter::Get(unsigned int nIndex) const
{
	RebuildIfNeeded();
	if (nIndex >= m_vectMap.size())
		return QuiverFile();
	return m_pSource->Get(m_vectMap[nIndex]);
}

void ImageListFilter::Remove(unsigned int nIndex)
{
	RebuildIfNeeded();
	if (nIndex >= m_vectMap.size())
		return;
	m_pSource->Remove(m_vectMap[nIndex]);
}

// event translation

void ImageListFilter::HandleContentsChanged(ImageListEventPtr event)
{ (void)event;
	m_bMapValid = false;
	EmitContentsChangedEvent();
}

void ImageListFilter::HandleCurrentIndexChanged(ImageListEventPtr event)
{
	int iOld = m_iLastEmittedView;

	// a selection on a filtered-out item (e.g. a folder clicked in the
	// browser) must not move this view
	if (!m_pred(m_pSource->Get(event->GetIndex())))
		return;

	m_szCurrentURI = m_pSource->Get(event->GetIndex()).GetURI();
	int iNew = ResolveViewIndex();

	if (iNew != iOld)
	{
		EmitCurrentIndexChangedEvent(
			(0 <= iNew) ? (unsigned int)iNew : 0,
			(0 < iOld) ? (unsigned int)iOld : 0);
	}
}

void ImageListFilter::HandleItemAdded(ImageListEventPtr event)
{
	bool bVisible = m_pred(m_pSource->Get(event->GetIndex()));
	m_bMapValid = false;
	if (bVisible)
	{
		unsigned int iPos = 0;
		MapPosOfSource(event->GetIndex(), iPos);
		EmitItemAddedEvent(iPos);
	}
	// an invisible addition does not change the view at all
}

void ImageListFilter::HandleItemRemoved(ImageListEventPtr event)
{
	unsigned int iOldPos = 0;
	bool bWasVisible = MapPosOfSource(event->GetIndex(), iOldPos);
	m_bMapValid = false;
	if (bWasVisible)
	{
		EmitItemRemovedEvent(iOldPos);
	}
	// if the tracked current vanished it is re-anchored lazily by
	// ResolveViewIndex()
}

void ImageListFilter::HandleItemChanged(ImageListEventPtr event)
{
	unsigned int iOldPos = 0;
	bool bWasVisible = MapPosOfSource(event->GetIndex(), iOldPos);
	// a change can alter folder-ness, so the map may be stale too
	m_bMapValid = false;
	bool bIsVisible = m_pred(m_pSource->Get(event->GetIndex()));
	if (bWasVisible || bIsVisible)
	{
		unsigned int iPos = iOldPos;
		if (bIsVisible)
			MapPosOfSource(event->GetIndex(), iPos);
		EmitItemChangedEvent(iPos);
	}
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
