#ifndef FILE_IMAGELIST_H
#define FILE_IMAGELIST_H

#include <string>
#include <list>
#include <set>


#include "QuiverFile.h"
#include "ImageListEventSource.h"

class ImageListImpl;
typedef boost::shared_ptr<ImageListImpl> ImageListImplPtr;
	
class ImageList : public virtual ImageListEventSource
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
	//~ImageList();	

	void SetImageList(std::list<std::string> *file_list);
	void Add(std::list<std::string> *file_list);


	void Remove(unsigned int iIndex);
	void RemoveRange(unsigned int iStart, unsigned int iEnd);

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

	void Sort(SortOrder o,bool descending);

private:
	ImageListImplPtr m_ImageListImplPtr;
	
};
#endif
