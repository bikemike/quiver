#include <config.h>

#include <iostream>
#include <algorithm>
#include <gtk/gtk.h>

#include <libgnomevfs/gnome-vfs.h>
#include <stdlib.h>
#include <set>

// comment this define out if __gnu_cxx is not available
#define USE_EXT

#ifdef USE_EXT

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

typedef __gnu_cxx::hash_map<std::string,GnomeVFSMonitorHandle*> PathMonitorMap;
typedef __gnu_cxx::hash_set<std::string> StringSet;
typedef __gnu_cxx::hash_map<std::string,GnomeVFSMonitorEventType> PathChangedMap; 

#else

#include <map>

typedef std::map<std::string,GnomeVFSMonitorHandle*> PathMonitorMap;
typedef std::set<std::string> StringSet;

#endif

typedef std::pair<std::string,GnomeVFSMonitorHandle*> PathMonitorPair;
typedef std::pair<std::string,GnomeVFSMonitorEventType> PathChangedPair;



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
	
	void Add(std::list<std::string> *file_list);
	void SetImageList(std::list<std::string> *file_list);
	bool UpdateImageList(std::list<std::string> *file_list);
	
	bool IsSupportedFileType(const gchar* uri, GnomeVFSFileInfo *info);
	
	bool AddDirectory(const gchar* uri);
	bool AddFile(const gchar*  uri);
	bool AddFile(const gchar* uri,GnomeVFSFileInfo *info);
	
	bool RemoveMonitor(string path);
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

};

// sort functions
class SortByFilename;
class SortByFileExtension;
class SortByDate;


StringSet ImageListImpl::c_setSupportedMimeTypes;

// ============================================================================


ImageList::ImageList() : m_ImageListImplPtr( new ImageListImpl(this) )
{
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

void ImageList::Add(std::list<std::string> *file_list)
{
	int iOldSize, iNewSize;
	
	iOldSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	m_ImageListImplPtr->Add(file_list);
	
	iNewSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	if (iOldSize != iNewSize)
	{ 
		EmitContentsChangedEvent();
	}
}


void ImageList::SetImageList(list<string> *file_list)
{
	int iOldSize, iNewSize;
	
	iOldSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	m_ImageListImplPtr->SetImageList(file_list);
	
	iNewSize = m_ImageListImplPtr->m_mapDirs.size() + m_ImageListImplPtr->m_mapFiles.size();
	
	if (!(0 == iOldSize && 0 == iNewSize))
	{ 
		EmitContentsChangedEvent();
	}	
}

void ImageList::UpdateImageList(list<string> *file_list)
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

bool ImageList::IsSupportedFileType(const gchar* uri, GnomeVFSFileInfo *info)
{
	return m_ImageListImplPtr->IsSupportedFileType(uri, info);
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
		//printf("%d: %s\n",GetCurrentIndex(),strURI.c_str());
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
	
	if (bWasEmpty && !bIsEmpty || !bWasEmpty && bIsEmpty)
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
		GnomeVFSMonitorEventType event_type = itr->second;
		
		switch (event_type)
		{
			case GNOME_VFS_MONITOR_EVENT_DELETED:
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
			case GNOME_VFS_MONITOR_EVENT_STARTEXECUTING:
				break;
			case GNOME_VFS_MONITOR_EVENT_STOPEXECUTING:
				break;
			case GNOME_VFS_MONITOR_EVENT_CREATED:
			case GNOME_VFS_MONITOR_EVENT_CHANGED:
			case GNOME_VFS_MONITOR_EVENT_METADATA_CHANGED:
				/*
				// FIXME: this if should never happen, but should test just in case
				// for now, it is commented out until it has been tested
				if (gnome_vfs_uris_match (monitor_uri,info_uri))
				{
					list<string> l;
					l.push_back(monitor_uri);
					impl->Add(&l);
				}
				else
				*/
				
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
void vfs_monitor_callback (GnomeVFSMonitorHandle *handle,
				const gchar *monitor_uri,
				const gchar *info_uri,
				GnomeVFSMonitorEventType event_type,
				gpointer user_data);

static
void        vfs_monitor_callback      (GnomeVFSMonitorHandle *handle,
                                             const gchar *monitor_uri,
                                             const gchar *info_uri,
                                             GnomeVFSMonitorEventType event_type,
                                             gpointer user_data)
{
	ImageListImpl *impl = (ImageListImpl*)user_data;

	gdk_threads_enter();
	
	switch (event_type)
	{
		case GNOME_VFS_MONITOR_EVENT_DELETED:
		case GNOME_VFS_MONITOR_EVENT_CREATED:
		case GNOME_VFS_MONITOR_EVENT_CHANGED:
		case GNOME_VFS_MONITOR_EVENT_METADATA_CHANGED:
			if (0 != impl->m_iTimeoutPathChanged)
			{
				g_source_remove(impl->m_iTimeoutPathChanged);
			}

			impl->m_mapPathChanged[info_uri] = event_type;

			impl->m_iTimeoutPathChanged = g_timeout_add(50, timeout_path_changed,impl);

			break;
		default:
			break;
	}

	gdk_threads_leave();
}
											 


ImageListImpl::ImageListImpl(ImageList *pImageList)
{
	m_pImageList = pImageList;
	
	LoadMimeTypes();
	m_SortBy = ImageList::SORT_BY_FILENAME;

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

static 
string PathToURI(const string &path)
{
	string strURI = path;
	if ('~' == path[0])
	{
		char *expanded = gnome_vfs_expand_initial_tilde(strURI.c_str());
		strURI = expanded;
		g_free(expanded);
	}
	gchar* uri_tmp = gnome_vfs_make_uri_from_shell_arg (strURI.c_str());
	gchar *uri = gnome_vfs_make_uri_canonical(uri_tmp);
	
	strURI = uri;
	g_free(uri);
	g_free(uri_tmp);

	return strURI;
}


void ImageListImpl::Add(std::list<std::string> *file_list)
{
	if (0 == file_list->size())
	{
		return;
	}
	
	bool bNewList = true;
	
	string strCurrentURI;
	if (0 < m_QuiverFileList.size())
	{
		strCurrentURI = m_QuiverFileList[m_iCurrentIndex].GetURI();
		bNewList = false;
	}
	else
	{
		m_iCurrentIndex = 0;
	}
	
	list<string>::iterator list_itr;
	for (list_itr = file_list->begin(); file_list->end() != list_itr ; ++list_itr)
	{
		bool bAdded = false;
		string strURI = PathToURI(*list_itr);
		
		GnomeVFSURI * vfs_uri = gnome_vfs_uri_new (strURI.c_str());
		GnomeVFSFileInfo *info;
		GnomeVFSResult result;
		
		if (vfs_uri == NULL) 
		{
			printf ("%s is not a valid URI.\n", strURI.c_str());
		}
		else
		{
			info = gnome_vfs_file_info_new ();
			result = gnome_vfs_get_file_info (strURI.c_str(),info,(GnomeVFSFileInfoOptions)
											  (GNOME_VFS_FILE_INFO_DEFAULT|
											  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
											  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
											  GNOME_VFS_FILE_INFO_FOLLOW_LINKS|
											  GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS)
											  );
			if ( GNOME_VFS_OK == result )
			{
				if (GNOME_VFS_FILE_INFO_FIELDS_TYPE  & info->valid_fields)
				{
					if (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY)
					{
						AddDirectory(strURI.c_str());
					}
					else if (info->type == GNOME_VFS_FILE_TYPE_REGULAR)
					{
						if (bNewList && 1 == file_list->size())
						{
							// special case to load the whole directory if only
							// one file is specified
							if (GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME & info->valid_fields)
							{
								//print the symlink
								strURI = PathToURI(info->symlink_name);
							}
	
							// get parent directory, then set the current image to uri
							vfs_uri = gnome_vfs_uri_new (strURI.c_str());
							if (NULL != vfs_uri)
							{
								if ( gnome_vfs_uri_has_parent(vfs_uri) )
								{
									GnomeVFSURI *parent = gnome_vfs_uri_get_parent(vfs_uri);
									gchar *parent_str = gnome_vfs_uri_to_string (parent,GNOME_VFS_URI_HIDE_NONE);
									gnome_vfs_uri_unref(parent);
	
									GnomeVFSFileInfo *dir_info = gnome_vfs_file_info_new ();
									result = gnome_vfs_get_file_info (parent_str,dir_info,(GnomeVFSFileInfoOptions)GNOME_VFS_FILE_INFO_DEFAULT);
									if (GNOME_VFS_OK == result && dir_info->type == GNOME_VFS_FILE_TYPE_DIRECTORY)
									{
										AddDirectory(parent_str);
										strCurrentURI = strURI;
										bAdded = true;
									}
									g_free (parent_str);
									gnome_vfs_file_info_unref (dir_info);
									//cout << "freed parent" << endl;
								}

							}
								
						}
						
						if (!bAdded)
						{
							// regular file
							// check if directory is already in the list,
							// if so, don't add the file
							gchar *dir_uri = gnome_vfs_uri_extract_dirname(vfs_uri);
							gchar *dir = gnome_vfs_make_uri_canonical(dir_uri);
							
							PathMonitorMap::iterator itr;
							itr = m_mapDirs.find(dir);
							if (m_mapDirs.end() != itr)
							{
								//printf("dir already exists: %s\n",dir);
							}
							else
							{
								if ( AddFile(strURI.c_str(),info) )
								{
									
									pair<PathMonitorMap::iterator,bool> p;
									p.second = false;
	
									p = m_mapFiles.insert(PathMonitorPair(strURI.c_str(),NULL)); 
									bAdded = p.second;
									if (bAdded)
									{
										gnome_vfs_monitor_add(&p.first->second,strURI.c_str(),GNOME_VFS_MONITOR_FILE,vfs_monitor_callback,this);
									}
								}
							}
							
							g_free(dir_uri);
							g_free(dir);
						}
					}
				}
				gnome_vfs_uri_unref(vfs_uri);
			}
			else
			{
				printf ("%s is not a valid URI.\n", strURI.c_str());
			}
			gnome_vfs_file_info_unref(info);
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

void ImageListImpl::SetImageList(list<string> *file_list)
{

	
	//printf("Setting the image list\n");
	if (0 == file_list->size())
	{
		return;
	}
	
	Clear();
	
	Add(file_list);

	//printf("done setting the image list\n");
}


bool ImageListImpl::UpdateImageList(list<string> *file_list)
{
	// create a set to find the differences
	// remove the ones not in the file_list but in
	// the monitor list, then add the difference of 
	// the other way around
	bool bUpdated = false;
	
	// can't use hash_set here because we need sorted
	// sets for set_difference and hash_sets are not 
	// sorted in the proper fashion
	std::set<string> setNewFiles;
	std::set<string> setOldFiles;
	
	string strCurrentURI;
	if (0 < m_QuiverFileList.size())
	{
		strCurrentURI = m_QuiverFileList[m_iCurrentIndex].GetURI();
	}
	
	PathMonitorMap::iterator itr;
	for (itr = m_mapDirs.begin(); m_mapDirs.end() != itr; ++itr)
	{
		setOldFiles.insert(itr->first);
	}
	for (itr = m_mapFiles.begin(); m_mapFiles.end() != itr; ++itr)
	{
		setOldFiles.insert(itr->first);
	}
	
	list<string>::iterator listItr;
	for (listItr = file_list->begin(); file_list->end() != listItr; ++listItr)
	{
		setNewFiles.insert ( PathToURI(*listItr) );
	}
	
	StringSet setOnlyInOld;
	set_difference(setOldFiles.begin(), setOldFiles.end(), setNewFiles.begin(), setNewFiles.end(), inserter(setOnlyInOld,setOnlyInOld.begin()));
	
	StringSet::iterator setItr;
	for (setItr = setOnlyInOld.begin(); setOnlyInOld.end() != setItr; ++setItr)
	{
		bUpdated = true;
		RemoveMonitor((*setItr));
	}
	
	StringSet setOnlyInNew;
	set_difference(setNewFiles.begin(), setNewFiles.end(), setOldFiles.begin(), setOldFiles.end(), inserter(setOnlyInNew,setOnlyInOld.begin()));
	
	list<string> listNew;
	listNew.insert(listNew.begin(),setOnlyInNew.begin(), setOnlyInNew.end());

	if (bUpdated && !strCurrentURI.empty())
	{
		SetCurrentImage(strCurrentURI);
	}

	if (0 != listNew.size())
	{
		bUpdated = true;
		Add(&listNew);
	}

	return bUpdated;
}


bool ImageListImpl::AddDirectory(const gchar* uri)
{
	bool bAdded = false;
	pair<PathMonitorMap::iterator,bool> p;
	p.second = false;
	p = m_mapDirs.insert(PathMonitorPair(uri,NULL));
	bAdded = p.second;

	if (bAdded)
	{
		// add directory monitor
		gnome_vfs_monitor_add(&p.first->second,uri,GNOME_VFS_MONITOR_DIRECTORY,vfs_monitor_callback,this);
		
		// remove any file monitor entries that are in this directory
		PathMonitorMap::iterator itr;
		itr = m_mapFiles.begin();
		while (m_mapFiles.end() != itr)
		{
			gchar * dirname = g_path_get_dirname(itr->first.c_str());
			if (gnome_vfs_uris_match (uri,dirname))
			{
				QuiverFile qfile(itr->first.c_str());
				QuiverFileList::iterator qitr = m_QuiverFileList.end();
				qitr = find(m_QuiverFileList.begin(),m_QuiverFileList.end(),qfile);
				m_QuiverFileList.erase(qitr);
				
				if (NULL != itr->second)
				{
					gnome_vfs_monitor_cancel(itr->second);
				}
				m_mapFiles.erase(itr++);
			}
			else
			{
				++itr;
			}
			g_free(dirname);
		}

		// 
		// now add the directory items to the file list
		//
		GnomeVFSResult result;
		GnomeVFSDirectoryHandle *dir_handle;
	
		result = gnome_vfs_directory_open (&dir_handle,uri,(GnomeVFSFileInfoOptions)
								  (GNOME_VFS_FILE_INFO_DEFAULT|
								  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
								  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
								  GNOME_VFS_FILE_INFO_FOLLOW_LINKS|
								  GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS
								  ));
		//printf("opened dir: %d: %s\n",result,gnome_vfs_result_to_string (result));
		if ( GNOME_VFS_OK == result )
		{
			GnomeVFSURI * vfs_uri_dir = gnome_vfs_uri_new(uri);
	
			while ( GNOME_VFS_OK == result )
			{
				GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();;
				result = gnome_vfs_directory_read_next(dir_handle,info);
				
				if (GNOME_VFS_OK == result )
				{
					if (info->type == GNOME_VFS_FILE_TYPE_REGULAR)
					{
						GnomeVFSURI * vfs_uri_file = gnome_vfs_uri_append_path(vfs_uri_dir,info->name);
						gchar *str_uri_file = gnome_vfs_uri_to_string (vfs_uri_file,GNOME_VFS_URI_HIDE_NONE);
						
						gnome_vfs_uri_unref(vfs_uri_file);
	
						AddFile(str_uri_file,info);
	
						free (str_uri_file);
					}
				}
				gnome_vfs_file_info_unref (info);
			}
			gnome_vfs_uri_unref(vfs_uri_dir);
			gnome_vfs_directory_close (dir_handle);
		}
	}
	return bAdded;
}



bool ImageListImpl::AddFile(const gchar*  uri)
{
	bool bAdded = false;
	GnomeVFSResult result;
	GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,info,(GnomeVFSFileInfoOptions)
									  (GNOME_VFS_FILE_INFO_DEFAULT|
									  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
									  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
									  GNOME_VFS_FILE_INFO_FOLLOW_LINKS|
									  GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS)
									 );
	if ( GNOME_VFS_OK == result )
	{
		bAdded = AddFile(uri,info);
	}
	gnome_vfs_file_info_unref(info);
	
	return bAdded;
}

bool ImageListImpl::AddFile(const gchar* uri, GnomeVFSFileInfo *info)
{
	bool bAdded = false;
	if (GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE & info->valid_fields)
	{
		if ( c_setSupportedMimeTypes.end() != c_setSupportedMimeTypes.find( info->mime_type ) )
		{
			QuiverFile f(uri,info);
			m_QuiverFileList.push_back(f);
			bAdded = true;
		}
		else
		{
			//printf("Unsupported mime_type: %s\n",info->mime_type);
		}
	}
	return bAdded;
}

bool ImageListImpl::IsSupportedFileType(const gchar* uri, GnomeVFSFileInfo *info)
{
	bool bSupported = false;
	if (GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE & info->valid_fields)
	{
		if ( c_setSupportedMimeTypes.end() != c_setSupportedMimeTypes.find( info->mime_type ) )
		{
			bSupported = true;
		}
		else
		{
			//printf("Unsupported mime_type: %s\n",info->mime_type);
		}
	}
	return bSupported;
}

bool ImageListImpl::RemoveMonitor(string path)
{
	bool bErased = false;
	PathMonitorMap::iterator itr;

	string strURI = PathToURI(path);
	
	itr = m_mapDirs.find(strURI);
	if (m_mapDirs.end() != itr)
	{
		QuiverFileList::iterator qitr;
		qitr = m_QuiverFileList.begin();

		while (m_QuiverFileList.end() != qitr)
		{
			gchar * dirname = g_path_get_dirname(qitr->GetURI());
			if (gnome_vfs_uris_match (strURI.c_str(),dirname))
			{
				int iIndex = qitr - m_QuiverFileList.begin();
				RemoveFile(iIndex);
			}
			else
			{
				++qitr;
			}
			g_free(dirname);
		}
		if (NULL != itr->second)
		{
			gnome_vfs_monitor_cancel(itr->second);
		}
		m_mapDirs.erase(itr);
		bErased = true;
	}
	else
	{
		itr = m_mapFiles.find(strURI);
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
					gnome_vfs_monitor_cancel(itr->second);
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
			gnome_vfs_monitor_cancel(itr->second);
		}
	}
	for (itr = m_mapFiles.begin(); m_mapFiles.end() != itr; ++itr)
	{
		files.push_back(itr->first);
		gnome_vfs_monitor_cancel(itr->second);
		if (NULL != itr->second)
		{
			gnome_vfs_monitor_cancel(itr->second);
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
			gnome_vfs_monitor_cancel(itr->second);
		}
	}
	for (itr = m_mapFiles.begin(); m_mapFiles.end() != itr; ++itr)
	{
		if (NULL != itr->second)
		{
			gnome_vfs_monitor_cancel(itr->second);
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
					gnome_vfs_monitor_cancel(path_itr->second);
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
		return (0 > strcmp(a.GetURI(),b.GetURI()) );
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
			SortByFilename byFile;
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



