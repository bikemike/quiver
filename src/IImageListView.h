#ifndef FILE_I_IMAGE_LIST_VIEW_H
#define FILE_I_IMAGE_LIST_VIEW_H

#include <boost/shared_ptr.hpp>

#include "ImageListEventSource.h"
#include "QuiverFile.h"

// read/navigate surface over an ordered collection of QuiverFiles.
// ImageList implements it directly; ImageListFilter decorates any
// instance so a consumer (the viewer) can browse a filtered subset
// while the underlying list stays the single source of truth.
class IImageListView : public virtual ImageListEventSource
{
public:
	virtual ~IImageListView() {}

	virtual unsigned int GetSize() const = 0;
	virtual unsigned int GetCurrentIndex() const = 0;

	virtual bool HasNext() const = 0;
	virtual bool HasPrevious() const = 0;
	virtual bool Next() = 0;
	virtual bool Previous() = 0;
	virtual bool First() = 0;
	virtual bool Last() = 0;
	virtual bool SetCurrentIndex(unsigned int iIndex) = 0;

	virtual QuiverFile GetCurrent() const = 0;
	virtual QuiverFile GetNext() const = 0;
	virtual QuiverFile GetPrevious() const = 0;
	virtual QuiverFile GetFirst() const = 0;
	virtual QuiverFile GetLast() const = 0;
	virtual QuiverFile Get(unsigned int nIndex) const = 0;

	virtual void Remove(unsigned int nIndex) = 0;
};

typedef boost::shared_ptr<IImageListView> IImageListViewPtr;

#endif // FILE_I_IMAGE_LIST_VIEW_H
