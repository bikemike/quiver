#include <config.h>

#include <iostream>
#include <algorithm>
#include <gtk/gtk.h>

#include <gio/gio.h>

#include <stdlib.h>
#include <set>

extern "C"
{
#include "strnatcmp.h"
}

// comment this define out if __gnu_cxx is not available
#ifdef HAVE_CXX0X
#include <unordered_map>
#include <unordered_set>
typedef std::unordered_map<std::string,GFileMonitor*> PathMonitorMap;
typedef std::unordered_set<std::string> StringSet;
typedef std::unordered_map<std::string,GFileMonitorEvent> PathChangedMap; 
#elif defined(HAVE_TR1)
#include <tr1/unordered_map>
#include <tr1/unordered_set>
typedef std::tr1::unordered_map<std::string,GFileMonitor*> PathMonitorMap;
typedef std::tr1::unordered_set<std::string> StringSet;
typedef std::tr1::unordered_map<std::string,GFileMonitorEvent> PathChangedMap; 
#elif defined(HAVE_EXT)
#include <ext/hash_map>
#include <ext/hash_set>

#ifndef QUIVER_STRING_HASH
#define QUIVER_STRING_HASH

namespace __gnu_cxx {
	template<> struct hash<std::string>
	{
		size_t operator()(const std::string &s) const
		{ return __stl_hash_string(s.c_str()); }
	};
}
#endif
typedef __gnu_cxx::hash_map<std::string,GFileMonitor*> PathMonitorMap;
typedef __gnu_cxx::hash_set<std::string> StringSet;
typedef __gnu_cxx::hash_map<std::string,GFileMonitorEvent> PathChangedMap; 
#else

#include <map>
#include <set>

typedef std::map<std::string,GFileMonitor*> PathMonitorMap;
typedef std::set<std::string> StringSet;
typedef std::map<std::string,GFileMonitorEvent> PathChangedMap; 

#endif

typedef std::pair<std::string,GFileMonitor*> PathMonitorPair;
typedef std::pair<std::string,GFileMonitorEvent> PathChangedPair;



#include <vector>

#include "ImageList.h"

using namespace std;

// ============================================================================
// ImageListImpl: private implementation (hidden from header file)
// ============================================================================

typedef std::vector<QuiverFile> QuiverFileList;

class ImageListImpl
{
public:
/* constructor*/
	ImageListImpl(ImageList *pImageList);
	
/* destructor */
	~ImageListImpl();
	
/* member functions */
	
	static void LoadMimeTypes();
	
	void Add(const std::list<std::string> *file_list, bool bRecursive = false);
	void SetImageList(const std::list<std::string> *file_list, bool bRecursive = false);
	// like set, but keep the current image
	// selected if it's in the new list 
	bool UpdateImageList(const std::list<std::string> *file_list);
	
	bool AddDirectory(const gchar* uri, bool bRecursive = false);
	bool AddFile(const gchar*  uri);
	bool AddFile(const gchar* uri,GFileInfo *info);
	
	bool RemoveMonitor(string uri);
	void Reload();
	void Clear();

	bool RemoveFile(unsigned int iIndex);

	void SetCurrentImage(std::string uri);
	
	void Sort();
	void Sort(bool bUpdateCurrentIndex);
	void Sort(ImageList::SortBy o,bool descending, bool bUpdateCurrentIndex);

	
/* member variables */
	
	// pointer to the parent structure
	ImageList *m_pImageList; 
	
	// mime types supported by the imagelist
	static StringSet c_setSupportedMimeTypes;
	
	// the actual image list (but really a QuiverFile list)
	QuiverFileList m_QuiverFileList;

	// file monitors
	PathMonitorMap m_mapDirs;
	PathMonitorMap m_mapFiles;
	
	PathChangedMap m_mapPathChanged;
	
	guint m_iTimeoutPathChanged;
	
	
	
	
	
	unsigned int m_iCurrentIndex;

	ImageList::SortBy m_SortBy;
	bool m_bSortAscend;
	bool m_bEnableMonitor;

};

// sort functions
class SortByFilename;
class SortByFilenameNatural;
class SortByFileExtension;
class SortByDate;
class SortByDateModified;


StringSet ImageListImpl::c_setSupportedMimeTypes;

// ============================================================================


ImageList::ImageList() : m_ImageListImplPtr( new ImageListImpl(this) )
{
	m_ImageListImplPtr->m_bEnableMonitor = false;
}
ImageList::ImageList(bool bEnableMonitor) : m_ImageListImplPtr( new ImageListImpl(this) )
{
	m_ImageListImplPtr->m_bEnableMonitor = bEnableMonitor;
}

unsigned int
ImageList::GetSize()  const
{
	return m_ImageListImplPtr->m_QuiverFileList.size();
}

unsigned int
ImageList::GetCurrentIndex() const
{
	return m_ImageListImplPtr->m_iCurrentIndex;
}

bool
ImageList::SetCurrentIndex(unsigned int new_index)
{
	bool rval = false;
	if ( new_index  < GetSize())
	{
		if ( new_index != m_ImageListImplPtr->m_iCurrentIndex)
		{
			m_ImageListImplPtr->m_iCurrentIndex = new_index;
			EmitCurrentIndexChangedEvent(m_ImageListImplPtr->m_iCurrentIndex);
		}
		rval = true;
	}
	return rval;
}


void 
ImageList::Remove(unsigned int iIndex)
{
	unsigned int iOldIndex = GetCurrentIndex();	

	m_ImageListImplPtr->RemoveFile(iIndex);

	if (iOldIndex != GetCurrentIndex())
	{
		EmitCurrentIndexChangedEvent(GetCurrentIndex());
	}
}



bool 
ImageList::HasNext()  const
{
	return (0 < GetSize() && GetCurrentIndex() < GetSize()-1);
}

bool
ImageList::HasPrevious()  const
{
	return 0 < GetCurrentIndex() ;
}

QuiverFile
ImageList::GetNext() const
{
	assert( HasNext() );
	return m_ImageListImplPtr->m_QuiverFileList[GetCurrentIndex()+1];
}

QuiverFile
ImageList::GetCurrent() const
{
	assert ( GetSize() );
	return m_ImageListImplPtr->m_QuiverFileList[GetCurrentIndex()];
}

QuiverFile
ImageList::GetPrevious() const
{
	assert( HasPrevious() );
	return m_ImageListImplPtr->m_QuiverFileList[GetCurrentIndex()-1];
}

QuiverFile
ImageList::GetFirst() const
{
	assert (0 <GetSize() );
	return m_ImageListImplPtr->m_QuiverFileList[0];	
}

QuiverFile
ImageList::GetLast() const
{
	assert (0 <GetSize() );
	return m_ImageListImplPtr->m_QuiverFileList[GetSize()-1];	
}

bool
ImageList::Next() 
{
	bool rval = HasNext();
	if (rval)
	{
		rval = SetCurrentIndex(GetCurrentIndex()+1);
	}
	return rval;
}

bool
ImageList::Previous() 
{
	bool rval = HasPrevious();
	if (rval)
	{
		rval = SetCurrentIndex(GetCurrentIndex()-1);
	}
	return rval;
}

bool
ImageList::First() 
{
	SetCurrentIndex(0);

	return (0 < GetSize());
}

bool
ImageList::Last() 
{
	if (0 < GetSize())
	{
		SetCurrentIndex(GetSize()-1);
	}
	
	return (0 < GetSize());
}

QuiverFile 
ImageList::Get(unsigned int n) const
{
	assert ( n < GetSize() );
	return m_ImageListImplPtr->m_QuiverFileList[n];
}

QuiverFile 
ImageList::operator[](unsigned int n)
{
	assert ( n < GetSize() );
	return m_ImageListImplPtr->m_QuiverFileList[n];
}

QuiverFile const 
ImageList::operator[](unsigned int n) const
{
	assert ( n < GetSize() );
	return m_ImageListImplPtr->m_QuiverFileList[n];
}

void ImageList::Add(const std::list<std::string> *file_list, bool bRecursive/* = false*/)
{
	int iOldSize, iNewSize;
	
	iOldSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	m_ImageListImplPtr->Add(file_list, bRecursive);
	
	iNewSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	if (iOldSize != iNewSize)
	{ 
		EmitContentsChangedEvent();
	}
}


void ImageList::SetImageList(const std::list<std::string> *file_list, bool bRecursive /* = false */)
{
	int iOldSize, iNewSize;
	
	iOldSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	m_ImageListImplPtr->SetImageList(file_list, bRecursive);
	
	iNewSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	if (!(0 == iOldSize && 0 == iNewSize))
	{ 
		EmitContentsChangedEvent();
	}	
}

void ImageList::UpdateImageList(const list<string> *file_list)
{
	int iOldSize, iNewSize;
	bool bUpdated = false;
	
	iOldSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	bUpdated = m_ImageListImplPtr->UpdateImageList(file_list);
	
	iNewSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	if (bUpdated && !(0 == iOldSize && 0 == iNewSize))
	{ 
		EmitContentsChangedEvent();
	}	
}

std::list<std::string> ImageList::GetFolderList()
{
	list<string> listFolders;
	PathMonitorMap::iterator itr;
	for (itr = m_ImageListImplPtr->m_mapDirs.begin(); m_ImageListImplPtr->m_mapDirs.end() != itr; ++itr)
	{
		listFolders.push_back(itr->first);
	}	return listFolders;
}

std::list<std::string> ImageList::GetFileList()
{
	list<string> listFiles;
	PathMonitorMap::iterator itr;
	for (itr = m_ImageListImplPtr->m_mapFiles.begin(); m_ImageListImplPtr->m_mapFiles.end() != itr; ++itr)
	{
		listFiles.push_back(itr->first);
	}
	return listFiles;
}

std::vector<QuiverFile> ImageList::GetQuiverFiles()
{
	return m_ImageListImplPtr->m_QuiverFileList;
}


void ImageList::Sort(SortBy o, bool bSortAscending)
{
	unsigned int iOldIndex = GetCurrentIndex();	
	bool bNewOrder = false;
	if (o != m_ImageListImplPtr->m_SortBy || bSortAscending != m_ImageListImplPtr->m_bSortAscend)
	{
		bNewOrder = true;
	}
	
	m_ImageListImplPtr->Sort(o, bSortAscending, true);

	if (bNewOrder || iOldIndex != GetCurrentIndex())
	{
		EmitContentsChangedEvent();
	}
}

void ImageList::Reload()
{
	unsigned int iOldIndex = GetCurrentIndex();

	m_ImageListImplPtr->Reload();	

	if (iOldIndex != GetCurrentIndex())
	{
		EmitCurrentIndexChangedEvent(GetCurrentIndex());
	}

}

void ImageList::Reverse()
{
	m_ImageListImplPtr->m_bSortAscend = !m_ImageListImplPtr->m_bSortAscend;
	std::reverse(m_ImageListImplPtr->m_QuiverFileList.begin(),m_ImageListImplPtr->m_QuiverFileList.end());
	m_ImageListImplPtr->m_iCurrentIndex = GetSize() - m_ImageListImplPtr->m_iCurrentIndex -1;
	EmitContentsChangedEvent();
}

void ImageList::Clear()
{
	bool bWasEmpty, bIsEmpty;
	bWasEmpty = (0 == m_ImageListImplPtr->m_mapDirs.size() && 0 == m_ImageListImplPtr->m_mapFiles.size());
	
	m_ImageListImplPtr->Clear();
	
	bIsEmpty = (0 == m_ImageListImplPtr->m_mapDirs.size() && 0 == m_ImageListImplPtr->m_mapFiles.size());
	
	if ( bWasEmpty != bIsEmpty )
	{ 
		EmitContentsChangedEvent();
	}
}

//=============================================================================
// ImageListImpl: private implementation 
//=============================================================================

static gboolean timeout_path_changed(gpointer user_data)
{
	// FIXME: go through this list of 
	// changes and sort them out.
	
	ImageListImpl *impl = (ImageListImpl*)user_data;
	gdk_threads_enter();
	
	PathChangedMap::iterator itr;
	
	/*
	printf("==================\n");
	for (itr = impl->m_mapPathChanged.begin(); impl->m_mapPathChanged.end() != itr; ++itr)
	{
		printf("TIMEOUT: [%d] %s\n",itr->second, itr->first.c_str());
	}
	printf("==================\n\n");
	*/
	
	bool bContentsChanged = false;
	
	for (itr = impl->m_mapPathChanged.begin(); impl->m_mapPathChanged.end() != itr; ++itr)
	{
		GFileMonitorEvent event_type = itr->second;
		
		switch (event_type)
		{
			case G_FILE_MONITOR_EVENT_DELETED:
				printf("MONITOR IDLE DEL!\n");
				if (impl->RemoveMonitor(itr->first))
				{
					unsigned int iOldSize = impl->m_QuiverFileList.size();

					if (iOldSize != impl->m_QuiverFileList.size())
					{
						// FIXME: if this is just a file, we should 
						// emit an item removed event rather than
						// a contents changed event
						bContentsChanged = true;
					}
				}
				else
				{
					QuiverFileList::iterator qitr;
					QuiverFile f(itr->first.c_str());
					qitr = find(impl->m_QuiverFileList.begin(),impl->m_QuiverFileList.end(),f);
					int iIndex = qitr - impl->m_QuiverFileList.begin();
					if (impl->RemoveFile(iIndex))
					{
						if (!bContentsChanged)
						{
							impl->m_pImageList->EmitItemRemovedEvent(iIndex);
						}
					}
				}
				break;
			case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
			case G_FILE_MONITOR_EVENT_UNMOUNTED:
			case G_FILE_MONITOR_EVENT_MOVED:
				break;
			case G_FILE_MONITOR_EVENT_CREATED:
			case G_FILE_MONITOR_EVENT_CHANGED:
			case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
			case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
				// basically we have to deal with all these events in the same way,
				// if the item is not in the list, add it, and reload it.
				
				{
					QuiverFileList::iterator qitr;
					QuiverFile f(itr->first.c_str());
					qitr = find(impl->m_QuiverFileList.begin(),impl->m_QuiverFileList.end(),f);
					if (impl->m_QuiverFileList.end() == qitr)
					{
						// file did not exist in list so emit an add event
						if (impl->AddFile(itr->first.c_str()))
						{
							impl->Sort(true);
							 
							QuiverFile f(itr->first.c_str());
		 			
							QuiverFileList::iterator qitr;
							qitr = find(impl->m_QuiverFileList.begin(),impl->m_QuiverFileList.end(),f);
							if (impl->m_QuiverFileList.end() != qitr)
							{
								if (!bContentsChanged)
								{
									impl->m_pImageList->EmitItemAddedEvent(qitr - impl->m_QuiverFileList.begin());
								}
							}
						}
					}
					else
					{
						// file was already in the list, so emit a changed event
						//printf("change file %s\n",itr->first.c_str());
						qitr->Reload();
						if (!bContentsChanged)
						{
							impl->m_pImageList->EmitItemChangedEvent(qitr - impl->m_QuiverFileList.begin());
						}
					}
					
				}
				break;
			default:
				break;
		}
	}
	
	if (bContentsChanged)
	{
		impl->m_pImageList->EmitContentsChangedEvent();
	}
	
	impl->m_mapPathChanged.clear();
	
	gdk_threads_leave();
	return FALSE;
}

static
void monitor_callback (
	GFileMonitor     *monitor,
	GFile            *file,
	GFile            *other_file,
	GFileMonitorEvent event_type,
	gpointer          user_data) 
{
	ImageListImpl *impl = (ImageListImpl*)user_data;

	gdk_threads_enter();

	char* uri = g_file_get_uri(file);
	
	switch (event_type)
	{
		case G_FILE_MONITOR_EVENT_CHANGED:
		case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		case G_FILE_MONITOR_EVENT_DELETED:
		case G_FILE_MONITOR_EVENT_CREATED:
		case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
		case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
		case G_FILE_MONITOR_EVENT_UNMOUNTED:
		case G_FILE_MONITOR_EVENT_MOVED:

			if (0 != impl->m_iTimeoutPathChanged)
			{
				g_source_remove(impl->m_iTimeoutPathChanged);
			}

			impl->m_mapPathChanged[uri] = event_type;

			printf("MONITOR CALLBACK! %d\n", event_type);
			impl->m_iTimeoutPathChanged = g_timeout_add(50, timeout_path_changed,impl);

			break;
		default:
			break;
	}

	g_free(uri);

	gdk_threads_leave();
}


ImageListImpl::ImageListImpl(ImageList *pImageList)
{
	m_pImageList = pImageList;
	
	LoadMimeTypes();
	m_SortBy = ImageList::SORT_BY_FILENAME_NATURAL;

	m_bSortAscend = true;
	m_iCurrentIndex = 0;
}

ImageListImpl::~ImageListImpl()
{
	Clear();
}


void ImageListImpl::LoadMimeTypes()
{
	if ( c_setSupportedMimeTypes.empty())
	{
		cout << "Supported file types: " << endl;
		GSList *formats = gdk_pixbuf_get_formats ();
		//GSList *writable_formats = NULL;
		GdkPixbufFormat * fmt;
		while ( NULL != formats )
		{
			fmt = (GdkPixbufFormat*)formats->data;
			//cout << gdk_pixbuf_format_get_name(fmt) <<": " << endl;
			//cout << gdk_pixbuf_format_get_description(fmt) << endl;
			gchar ** ext_ptr_head = gdk_pixbuf_format_get_extensions(fmt);
			gchar ** ext_ptr = ext_ptr_head;
			while (NULL != *ext_ptr)
			{
				//cout << *ext_ptr << "," ;
				ext_ptr++;
			}
			g_strfreev(ext_ptr_head);
			//cout << endl;
			ext_ptr_head = gdk_pixbuf_format_get_mime_types(fmt);
			ext_ptr = ext_ptr_head;
			while (NULL != *ext_ptr)
			{
				c_setSupportedMimeTypes.insert(*ext_ptr);
				cout << *ext_ptr << "," ;
				ext_ptr++;
			}
			g_strfreev(ext_ptr_head);
			formats = g_slist_next(formats);
		}
		cout << endl;
		//g_slist_foreach (formats, add_if_writable, &writable_formats);
		g_slist_free (formats);
#ifdef QUIVER_MAEMO
		c_setSupportedMimeTypes.insert("sketch/png");
#endif
	}
}

void ImageListImpl::SetCurrentImage(string uri)
{
	if (!uri.empty())
	{
		QuiverFileList::iterator itr;
		
		QuiverFile f(uri.c_str());
		
		itr = std::find(m_QuiverFileList.begin(),m_QuiverFileList.end(),f);
		if (m_QuiverFileList.end() != itr)
		{
			m_iCurrentIndex = itr -m_QuiverFileList.begin() ;			
		}
	}
}


void ImageListImpl::Add(const std::list<std::string> *file_list, bool bRecursive/* = false*/)
{
	if (0 == file_list->size())
	{
		return;
	}
	
	bool bNewList = true;
	
	string strCurrentURI;
	string strURI;

	if (0 < m_QuiverFileList.size())
	{
		strCurrentURI = m_QuiverFileList[m_iCurrentIndex].GetURI();
		bNewList = false;
	}
	else
	{
		m_iCurrentIndex = 0;
	}
	
	list<string>::const_iterator list_itr;
	for (list_itr = file_list->begin(); file_list->end() != list_itr ; ++list_itr)
	{
		bool bAdded = false;

		GFile* gFile = g_file_new_for_commandline_arg(list_itr->c_str());
		
		if (NULL != gFile)
		{
			char* uri = g_file_get_uri(gFile);
			strURI = uri;
			g_free(uri);

			GFileInfo* info = g_file_query_info(gFile,
				G_FILE_ATTRIBUTE_STANDARD_NAME ","
				G_FILE_ATTRIBUTE_STANDARD_TYPE ","
				G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
				G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
				G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE ","
				G_FILE_ATTRIBUTE_STANDARD_SIZE ","
				G_FILE_ATTRIBUTE_STANDARD_ICON ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH ","
				G_FILE_ATTRIBUTE_TIME_MODIFIED ","
				G_FILE_ATTRIBUTE_TIME_CREATED,
				G_FILE_QUERY_INFO_NONE,
				NULL,
				NULL);

			if ( NULL != info)
			{
				GFileType type = g_file_info_get_file_type(info);
				if (G_FILE_TYPE_DIRECTORY == type)
				{
					AddDirectory(strURI.c_str(), bRecursive);
				}
				else if (G_FILE_TYPE_REGULAR == type)
				{
					if (bNewList && 1 == file_list->size())
					{
						// special case to load the whole directory if only
						// FIXME: add symlink support?

						// get parent directory, then set the current image to uri
						GFile* parent = g_file_get_parent(gFile);
						if (NULL != parent)
						{
							char* path = g_file_get_uri(parent);
							AddDirectory(path);
							g_free(path);

							strCurrentURI = strURI;

							bAdded = true;
						}
						g_object_unref(parent);
					}
					
					if (!bAdded)
					{
						// regular file
						// check if directory is already in the list,
						// if so, don't add the file
						GFile* parent = g_file_get_parent(gFile);
						char *dir_uri = g_file_get_uri(parent);
						
						PathMonitorMap::iterator itr;
						itr = m_mapDirs.find(dir_uri);
						if (m_mapDirs.end() == itr)
						{
							// directory not in list
							if ( AddFile(strURI.c_str(), info) )
							{
								pair<PathMonitorMap::iterator,bool> p;

								p = m_mapFiles.insert(PathMonitorPair(strURI,NULL)); 
								bAdded = p.second;
								if (bAdded && m_bEnableMonitor)
								{
									p.first->second = g_file_monitor(gFile, G_FILE_MONITOR_NONE, NULL, NULL);
									g_signal_connect(G_OBJECT(p.first->second), "changed", G_CALLBACK(monitor_callback), this);
								}
							}
						}
						
						g_object_unref(parent);
						g_free(dir_uri);
					}
				}
				g_object_unref(info);
			}
			g_object_unref(gFile);
		}
	}

	if (!strCurrentURI.empty())
	{
		SetCurrentImage(strCurrentURI);
	}
	
	// sort - update the current index if the 
	// string is not empty
	Sort(!strCurrentURI.empty());

}

void ImageListImpl::SetImageList(const std::list<std::string> *file_list, bool bRecursive /* = false */)
{
	Clear();
	Add(file_list, bRecursive);
}


bool ImageListImpl::UpdateImageList(const list<string> *file_list)
{
	// create a set to find the differences
	// remove the ones not in the file_list but in
	// the monitor list, then add the difference of 
	// the other way around
	bool bUpdate = false;
	
	// can't use hash_set here because we need sorted
	// sets for set_difference and hash_sets are not 
	// sorted in the proper fashion
	std::set<string> setNewFiles;
	std::set<string> setOldFiles;

	PathMonitorMap::iterator itr;
	for (itr = m_mapDirs.begin(); m_mapDirs.end() != itr; ++itr)
	{
		setOldFiles.insert(itr->first);
	}
	for (itr = m_mapFiles.begin(); m_mapFiles.end() != itr; ++itr)
	{
		setOldFiles.insert(itr->first);
	}

	list<string>::const_iterator listItr;
	for (listItr = file_list->begin(); file_list->end() != listItr; ++listItr)
	{
		GFile* file = g_file_new_for_commandline_arg(listItr->c_str());
		char* uri = g_file_get_uri(file);
		setNewFiles.insert ( uri );
		g_free(uri);
		g_object_unref(file);
	}
	
	StringSet setInBoth;
	set_intersection(setOldFiles.begin(), setOldFiles.end(), setNewFiles.begin(), setNewFiles.end(), inserter(setInBoth,setInBoth.begin()));

	if (0 == setInBoth.size())
	{
		if (0 < setOldFiles.size())
		{
			bUpdate = true;
		}
	}
	else
	{
		StringSet setOnlyInOld;
		set_difference(setOldFiles.begin(), setOldFiles.end(), setNewFiles.begin(), setNewFiles.end(), inserter(setOnlyInOld,setOnlyInOld.begin()));

		StringSet::iterator setItr;
		if (0 < setOnlyInOld.size())
		{
			bUpdate = true;
		}
	}

	StringSet setOnlyInNew;
	set_difference(setNewFiles.begin(), setNewFiles.end(), setOldFiles.begin(), setOldFiles.end(), inserter(setOnlyInNew,setOnlyInNew.begin()));
	
	list<string> listNew;
	listNew.insert(listNew.begin(),setOnlyInNew.begin(), setOnlyInNew.end());

	if (0 < listNew.size())
	{
		bUpdate = true;
	}

	string strCurrentURI;
	if (0 < m_QuiverFileList.size())
	{
		strCurrentURI = m_QuiverFileList[m_iCurrentIndex].GetURI();
	}

	SetImageList(file_list);

	if (!strCurrentURI.empty())
	{
			SetCurrentImage(strCurrentURI);
	}

	return bUpdate;
}


bool ImageListImpl::AddDirectory(const gchar* uri, bool bRecursive /* = false */)
{
	bool bAdded = false;
	pair<PathMonitorMap::iterator,bool> p;
	p = m_mapDirs.insert(PathMonitorPair(uri,NULL));
	bAdded = p.second;

	if (bAdded)
	{
		GFile* dir = g_file_new_for_uri(uri);

		p.first->second = NULL;
		// add directory monitor
		if (m_bEnableMonitor)
		{
			p.first->second = g_file_monitor(dir, G_FILE_MONITOR_NONE, NULL, NULL);
			g_signal_connect(G_OBJECT(p.first->second), "changed", G_CALLBACK(monitor_callback), this);

			// remove any file monitor entries that are in this directory
			PathMonitorMap::iterator itr;
			itr = m_mapFiles.begin();
			while (m_mapFiles.end() != itr)
			{
				GFile* file = g_file_new_for_uri(itr->first.c_str());
				GFile* parent = g_file_get_parent(file);

				if (g_file_equal(dir, parent))
				{
					QuiverFile qfile(itr->first.c_str());
					QuiverFileList::iterator qitr = m_QuiverFileList.end();
					qitr = find(m_QuiverFileList.begin(),m_QuiverFileList.end(),qfile);
					m_QuiverFileList.erase(qitr);
					
					if (NULL != itr->second)
					{
						g_object_unref(itr->second);
					}
					m_mapFiles.erase(itr++);
				}
				else
				{
					++itr;
				}
				g_object_unref(file);
				g_object_unref(parent);
			}
		}

		// 
		// now add the directory items to the file list
		GFileEnumerator* enumerator = g_file_enumerate_children(dir,
				G_FILE_ATTRIBUTE_STANDARD_NAME ","
				G_FILE_ATTRIBUTE_STANDARD_TYPE ","
				G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
				G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
				G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE ","
				G_FILE_ATTRIBUTE_STANDARD_SIZE ","
				G_FILE_ATTRIBUTE_STANDARD_ICON ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE ","
				G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH ","
				G_FILE_ATTRIBUTE_TIME_MODIFIED ","
				G_FILE_ATTRIBUTE_TIME_CREATED,
				G_FILE_QUERY_INFO_NONE,
				NULL,
				NULL);

		if ( NULL != enumerator )
		{
			GFileInfo* info = NULL;
			while ( NULL != (info = g_file_enumerator_next_file(enumerator, NULL, NULL)) )
			{
				const char* name = g_file_info_get_name(info);
				GFile* child = g_file_get_child(dir, name);
				GFileType type = g_file_info_get_file_type(info);
				char* child_uri = g_file_get_uri(child);

				if (G_FILE_TYPE_REGULAR == type)
				{
					AddFile(child_uri, info);
				}
				else if (G_FILE_TYPE_DIRECTORY == type && bRecursive)
				{
					if (0 != strcmp(".", name) && 0 != strcmp("..", name))
					{
						AddDirectory(child_uri, bRecursive);
					}
				}

				g_object_unref(info);
				g_object_unref(child);
				g_free(child_uri);
			}

			g_object_unref(enumerator);
		}
		g_object_unref(dir);
	}
	return bAdded;
}



bool ImageListImpl::AddFile(const gchar*  uri)
{
	bool bAdded = false;
	GFile* gFile = g_file_new_for_uri(uri);
	GFileInfo* info = g_file_query_info(gFile,
		G_FILE_ATTRIBUTE_STANDARD_NAME ","
		G_FILE_ATTRIBUTE_STANDARD_TYPE ","
		G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
		G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
		G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE ","
		G_FILE_ATTRIBUTE_STANDARD_SIZE ","
		G_FILE_ATTRIBUTE_STANDARD_ICON ","
		G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
		G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE ","
		G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE ","
		G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH ","
		G_FILE_ATTRIBUTE_TIME_MODIFIED ","
		G_FILE_ATTRIBUTE_TIME_CREATED,
		G_FILE_QUERY_INFO_NONE,
		NULL,
		NULL);

	if ( NULL != info )
	{
		bAdded = AddFile(uri,info);
	}

	g_object_unref(info);
	g_object_unref(gFile);
	
	return bAdded;
}

bool ImageListImpl::AddFile(const gchar* uri, GFileInfo *info)
{
	bool bAdded = false;

	const char* content_type = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
	if (NULL != content_type)
	{
		gchar* mimetype = g_content_type_get_mime_type(content_type);
		
		if ( c_setSupportedMimeTypes.end() != c_setSupportedMimeTypes.find(mimetype ) )
		{
			QuiverFile f(uri, info);
			m_QuiverFileList.push_back(f);
			bAdded = true;
		}
		else if (g_strstr_len(mimetype, 5, "video") == mimetype) // video
		{
			QuiverFile f(uri, info);
			m_QuiverFileList.push_back(f);
			bAdded = true;
		}
		else
		{
			//printf("Unsupported mime_type: %s\n",mimetype);
		}
		g_free(mimetype);
	}
	return bAdded;
}


bool ImageListImpl::RemoveMonitor(string uri)
{
	bool bErased = false;
	PathMonitorMap::iterator itr;

	// find the path in the dirs map.
	// if it exists, remove all files in that directory
	itr = m_mapDirs.find(uri);
	if (m_mapDirs.end() != itr)
	{
		QuiverFileList::iterator qitr;
		qitr = m_QuiverFileList.begin();

		GFile* file = g_file_new_for_uri(uri.c_str());

		while (m_QuiverFileList.end() != qitr)
		{
			GFile* qfile = g_file_new_for_uri(qitr->GetURI());
			GFile* parent = g_file_get_parent(qfile);

			if (g_file_equal(file, parent))
			{
				int iIndex = qitr - m_QuiverFileList.begin();
				RemoveFile(iIndex);
			}
			else
			{
				++qitr;
			}
			g_object_unref(parent);
			g_object_unref(qfile);
		}
		g_object_unref(file);
		if (NULL != itr->second)
		{
			g_object_unref(itr->second);
		}
		m_mapDirs.erase(itr);
		bErased = true;
	}
	else
	{
		itr = m_mapFiles.find(uri);
		if (m_mapFiles.end() != itr)
		{
			QuiverFile qfile(itr->first.c_str());
			
			QuiverFileList::iterator qitr = m_QuiverFileList.end();
			qitr = find(m_QuiverFileList.begin(),m_QuiverFileList.end(),qfile);
			if ( m_QuiverFileList.end() != qitr )
			{
				int iIndex = qitr - m_QuiverFileList.begin();
				RemoveFile(iIndex);
			}
			else
			{
				if (NULL != itr->second)
				{
					g_object_unref(itr->second);
				}
				m_mapFiles.erase(itr);
			}

			bErased = true;
		}
	}
		
	return bErased;
}

void ImageListImpl::Reload()
{
	string strURI;
	
	if (m_QuiverFileList.size())
	{
		strURI = m_QuiverFileList[m_iCurrentIndex].GetURI();
		//printf("%d: %s\n",iOldIndex,strURI.c_str());
	}
	
	list<string> files;
	PathMonitorMap::iterator itr;
	for (itr = m_mapDirs.begin(); m_mapDirs.end() != itr; ++itr)
	{
		files.push_back(itr->first);
		if (NULL != itr->second)
		{
			g_object_unref(itr->second);
		}
	}
	for (itr = m_mapFiles.begin(); m_mapFiles.end() != itr; ++itr)
	{
		files.push_back(itr->first);
		if (NULL != itr->second)
		{
			g_object_unref(itr->second);
		}
	}
	m_mapDirs.clear();
	m_mapFiles.clear();
	
	Clear();
	
	Add(&files);
	
	if (!strURI.empty())
	{
		SetCurrentImage(strURI);
	}
}

void ImageListImpl::Clear()
{
	PathMonitorMap::iterator itr;
	for (itr = m_mapDirs.begin(); m_mapDirs.end() != itr; ++itr)
	{
		if (NULL != itr->second)
		{
			g_object_unref(itr->second);
		}
	}
	for (itr = m_mapFiles.begin(); m_mapFiles.end() != itr; ++itr)
	{
		if (NULL != itr->second)
		{
			g_object_unref(itr->second);
		}
	}
	
	m_mapDirs.clear();
	m_mapFiles.clear();
	m_QuiverFileList.clear();
}

bool ImageListImpl::RemoveFile(unsigned int iIndex)
{
	bool bErased = false;

	QuiverFileList::iterator itr = m_QuiverFileList.begin();
	
	if (iIndex < m_QuiverFileList.size())
	{
		itr += iIndex;
		
		if (m_QuiverFileList.end() != itr)
		{
			PathMonitorMap::iterator path_itr;
			path_itr = m_mapFiles.find(itr->GetURI());
			if (m_mapFiles.end() != path_itr)
			{
				if (NULL != path_itr->second)
				{
					g_object_unref(path_itr->second);
				}
				m_mapFiles.erase(path_itr);
			}
			
			m_QuiverFileList.erase(itr);
			bErased = true;
	
			if (m_iCurrentIndex > iIndex)
			{
				m_iCurrentIndex--;
			}
			else if (m_iCurrentIndex == iIndex)
			{
				m_iCurrentIndex =
					MIN(m_iCurrentIndex,m_QuiverFileList.size()-1);
			}
		}
	}
	return bErased;
}


void ImageListImpl::Sort()
{
	Sort(m_SortBy,m_bSortAscend, true);
}

void ImageListImpl::Sort(bool bUpdateCurrentIndex)
{
	Sort(m_SortBy, m_bSortAscend, bUpdateCurrentIndex);
}


class SortByFilename : public std::binary_function<QuiverFile,QuiverFile,bool>
{
public:
	bool operator()(const QuiverFile &a, const QuiverFile &b) const
	{
		return (0 > strcasecmp(a.GetURI(),b.GetURI()) );
	}
};

class SortByFilenameNatural : public std::binary_function<QuiverFile,QuiverFile,bool>
{
public:
	bool operator()(const QuiverFile &a, const QuiverFile &b) const
	{
		int rval = 0;
		string file1 = a.GetURI();
		string file2 = b.GetURI();

		string::size_type sep1 = file1.find_last_of(G_DIR_SEPARATOR);
		string::size_type sep2 = file2.find_last_of(G_DIR_SEPARATOR);
		if (string::npos != sep1 && string::npos != sep2)
		{
			string::size_type ext1 = file1.find_first_of(".",sep1);
			string::size_type ext2 = file2.find_first_of(".",sep2);

			if (0 == rval &&  string::npos != ext1 && string::npos != ext2)
			{
				rval = strnatcasecmp( file1.substr(0,ext1).c_str(), file2.substr(0, ext2).c_str());

				if (0 == rval)
				{
					rval = strnatcasecmp( file1.substr(ext1,file1.size() - ext1).c_str(), file2.substr(ext2, file2.size() - ext2).c_str());
				}

			}
			else
			{
				rval = strnatcasecmp(a.GetURI(),b.GetURI());
			}
		}
		else
		{
			rval =  strnatcasecmp(a.GetURI(),b.GetURI());
		}

		return (0 > rval);
	}
};

class SortByDateModified : public std::binary_function<QuiverFile,QuiverFile,bool>
{
public:

	bool operator()(const QuiverFile &a, const QuiverFile &b) const
	{
		time_t ta = a.GetTimeT(false);
		time_t tb = b.GetTimeT(false);
		if (ta == tb)
		{
			SortByFilenameNatural byFile;
			return byFile(a,b);
		}
		return ( ta < tb );
	}
};

class SortByDate : public std::binary_function<QuiverFile,QuiverFile,bool>
{
public:

	bool operator()(const QuiverFile &a, const QuiverFile &b) const
	{
		time_t ta = a.GetTimeT();
		time_t tb = b.GetTimeT();
		if (ta == tb)
		{
			SortByFilenameNatural byFile;
			return byFile(a,b);
		}
		return ( ta < tb );
	}
};

void ImageListImpl::Sort(ImageList::SortBy o,bool bSortAscend, bool bUpdateCurrentIndex)
{
	m_SortBy = o;
	m_bSortAscend = bSortAscend;
	
	string strURI;
	if (m_QuiverFileList.size())
	{
		strURI = m_QuiverFileList[m_iCurrentIndex].GetURI();
	}
	
	switch (m_SortBy)
	{
		case ImageList::SORT_BY_FILENAME:
		{
			SortByFilename sortby;
			std::sort(m_QuiverFileList.begin(), m_QuiverFileList.end(), sortby);
			break;
		}
		case ImageList::SORT_BY_FILENAME_NATURAL:
		{
			SortByFilenameNatural sortby;
			std::sort(m_QuiverFileList.begin(), m_QuiverFileList.end(), sortby);
			break;
		}
		case ImageList::SORT_BY_FILE_TYPE:
			//std::sort(m_QuiverFileList.begin(), m_QuiverFileList.end(), SortByFilename());
			break;
		case ImageList::SORT_BY_DATE:
		{
			SortByDate sortby;
			std::sort(m_QuiverFileList.begin(), m_QuiverFileList.end(), sortby);
			//std::sort(m_QuiverFileList.begin(), m_QuiverFileList.end(), SortByFilename());
			break;
		}
		case ImageList::SORT_BY_DATE_MODIFIED:
		{
			SortByDateModified sortby;
			std::sort(m_QuiverFileList.begin(), m_QuiverFileList.end(), sortby);
			//std::sort(m_QuiverFileList.begin(), m_QuiverFileList.end(), SortByFilename());
			break;
		}
		case ImageList::SORT_BY_RANDOM:
		{
			std::random_shuffle(m_QuiverFileList.begin(), m_QuiverFileList.end());
			break;
		}
		default:
			break;
	}
	
	if (!m_bSortAscend)
	{
		std::reverse(m_QuiverFileList.begin(),m_QuiverFileList.end());
	}
	
	if (bUpdateCurrentIndex)
	{
		if (!strURI.empty())
		{
			SetCurrentImage(strURI);
		}
	}
}



