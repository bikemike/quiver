#include <config.h>

#include <iostream>
#include <algorithm>
#include <gtk/gtk.h>

#include <libgnomevfs/gnome-vfs.h>
#include <stdlib.h>


#include <vector>

#include "ImageList.h"

using namespace std;

// ============================================================================
// ImageListImpl: private implementation (hidden from header file)
// ============================================================================

typedef std::vector<QuiverFile> QuiverFileList;
typedef std::vector<QuiverFile>::iterator QuiverFileListIter;

class ImageListImpl
{
public:
	/* constructor*/
	ImageListImpl();
	
	/* destructor */
	//~ImageListImpl();
	
	/* member functions */
	
	static void LoadMimeTypes();
	
	void Add(std::list<std::string> *file_list);
	void SetImageList(std::list<std::string> *file_list);
	
	void AddDirectory(const gchar* uri);
	void AddFile(const gchar*  uri);
	void AddFile(const gchar* uri,GnomeVFSFileInfo *info);

	void SetCurrentImage(std::string uri);
	void Sort(ImageList::SortOrder o,bool descending);

	/* member variables */
	static ImageList::SortOrder c_SortOrder;
	static std::set<std::string> c_setSupportedMimeTypes;
	
	QuiverFileList m_QuivFileList;

	unsigned int m_iCurrentIndex;

	static bool QuiverFileCompare(const QuiverFile & left , const QuiverFile & right);
};


ImageList::SortOrder ImageListImpl::c_SortOrder;
set<string> ImageListImpl::c_setSupportedMimeTypes;

// ============================================================================


ImageList::ImageList() : m_ImageListImplPtr( new ImageListImpl() )
{
}


/*

ImageList::ImageList(std::string filepath)
{
	LoadMimeTypes();
	c_SortOrder = SORT_FILENAME;
	list<string> file_list;
	file_list.push_back(filepath);
	SetImageList(&file_list);
}
*/

unsigned int
ImageList::GetSize()  const
{
	return m_ImageListImplPtr->m_QuivFileList.size();
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
	if ( new_index  < GetSize() )
	{
		m_ImageListImplPtr->m_iCurrentIndex = new_index;
		rval = true;
	}
	return rval;
}


void 
ImageList::Remove(unsigned int iIndex)
{
	QuiverFileList *list = &m_ImageListImplPtr->m_QuivFileList;
	QuiverFileListIter itr = list->begin();
	
	if (iIndex < GetSize())
	{
		itr += iIndex;
		
		if (list->end() != itr)
		{
			list->erase(itr);
		}

		if (m_ImageListImplPtr->m_iCurrentIndex > iIndex)
		{
			m_ImageListImplPtr->m_iCurrentIndex--;
		}
		else if (m_ImageListImplPtr->m_iCurrentIndex == iIndex)
		{
			m_ImageListImplPtr->m_iCurrentIndex =
				MIN(GetCurrentIndex(),GetSize()-1);
		}
		
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
	return m_ImageListImplPtr->m_QuivFileList[GetCurrentIndex()+1];
}

QuiverFile
ImageList::GetCurrent() const
{
	assert ( GetSize() );
	return m_ImageListImplPtr->m_QuivFileList[GetCurrentIndex()];
}

QuiverFile
ImageList::GetPrevious() const
{
	assert( HasPrevious() );
	return m_ImageListImplPtr->m_QuivFileList[GetCurrentIndex()-1];
}

QuiverFile
ImageList::GetFirst() const
{
	assert (0 <GetSize() );
	return m_ImageListImplPtr->m_QuivFileList[0];	
}

QuiverFile
ImageList::GetLast() const
{
	assert (0 <GetSize() );
	return m_ImageListImplPtr->m_QuivFileList[GetSize()-1];	
}

bool
ImageList::Next() 
{
	bool rval = HasNext();
	if (rval)
		m_ImageListImplPtr->m_iCurrentIndex++;
	return rval;
}

bool
ImageList::Previous() 
{
	bool rval = HasPrevious();
	if (rval)
		m_ImageListImplPtr->m_iCurrentIndex--;
	return rval;
}

bool
ImageList::First() 
{
	m_ImageListImplPtr->m_iCurrentIndex = 0;

	return (0 < GetSize());
}

bool
ImageList::Last() 
{
	if (0 < GetSize())
		m_ImageListImplPtr->m_iCurrentIndex = GetSize()-1;
	else
		m_ImageListImplPtr->m_iCurrentIndex = 0;
	
	return (0 < GetSize());
}

QuiverFile 
ImageList::Get(unsigned int n) const
{
	assert ( n < GetSize() );
	return m_ImageListImplPtr->m_QuivFileList[n];
}

QuiverFile 
ImageList::operator[](unsigned int n)
{
	assert ( n < GetSize() );
	return m_ImageListImplPtr->m_QuivFileList[n];
}

QuiverFile const 
ImageList::operator[](unsigned int n) const
{
	assert ( n < GetSize() );
	return m_ImageListImplPtr->m_QuivFileList[n];
}

void ImageList::Add(std::list<std::string> *file_list)
{
	m_ImageListImplPtr->Add(file_list);
}


void ImageList::SetImageList(list<string> *file_list)
{
	m_ImageListImplPtr->SetImageList(file_list);
}


void ImageList::Sort(SortOrder o,bool descending)
{
	m_ImageListImplPtr->Sort(o,descending);
}



//=============================================================================
// ImageListImpl: private implementation 
//=============================================================================

ImageListImpl::ImageListImpl()
{
	LoadMimeTypes();
	c_SortOrder = ImageList::SORT_FILENAME;
	m_iCurrentIndex = 0;
}


void ImageListImpl::LoadMimeTypes()
{
	if ( c_setSupportedMimeTypes.empty())
	{
		cout << "Supported file types: " << endl;
		GSList *formats = gdk_pixbuf_get_formats ();
		//GSList *writable_formats = NULL;
		GdkPixbufFormat * fmt;
		while (NULL != (formats ) )
		{
			fmt = (GdkPixbufFormat*)formats->data;
			//cout << gdk_pixbuf_format_get_name(fmt) <<": " << endl;
			//cout << gdk_pixbuf_format_get_description(fmt) << endl;
			gchar ** ext_ptr = gdk_pixbuf_format_get_extensions(fmt);
			while (NULL != *ext_ptr)
			{
				//cout << *ext_ptr << "," ;
				ext_ptr++;
			}
			//cout << endl;
			ext_ptr = gdk_pixbuf_format_get_mime_types(fmt);
			while (NULL != *ext_ptr)
			{
				c_setSupportedMimeTypes.insert(*ext_ptr);
				cout << *ext_ptr << "," ;
				ext_ptr++;
			}
			formats = g_slist_next(formats);
		}
		cout << endl;
		//g_slist_foreach (formats, add_if_writable, &writable_formats);
		g_slist_free (formats);
	}
}

void ImageListImpl::SetCurrentImage(string uri)
{
	if (!uri.empty())
	{
		QuiverFileListIter itr;
		int i = 0;
		for (itr = m_QuivFileList.begin();itr != m_QuivFileList.end(); ++itr,i++)
		{
			if (itr->GetURI() == uri)
			{
				m_iCurrentIndex = i;

				break;
			}
		}
	}
}

void ImageListImpl::Add(std::list<std::string> *file_list)
{
	if (0 == file_list->size())
	{
		if (0 == m_QuivFileList.size())
			SetCurrentImage("");
		return;
	}
	
	string current;
	if (0 < m_QuivFileList.size())
	{
		current = m_QuivFileList[m_iCurrentIndex].GetURI();
	}
	
	if (1 == file_list->size())
	{
		m_iCurrentIndex = 0;

		gchar* uri = gnome_vfs_make_uri_from_shell_arg (file_list->front().c_str());
		
		cout << "is this valid: " << uri << endl;
		
		GnomeVFSURI * vfs_uri = gnome_vfs_uri_new (uri);
		GnomeVFSFileInfo *info;
		GnomeVFSResult result;
		if (vfs_uri == NULL) 
		{
			printf ("%s is not a valid URI.\n", uri);
		}
		else
		{
			gnome_vfs_uri_unref(vfs_uri);
			info = gnome_vfs_file_info_new ();
			result = gnome_vfs_get_file_info (uri,info,(GnomeVFSFileInfoOptions)
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
						cout << "adding dir" << endl;
						AddDirectory(uri);
						std::sort(m_QuivFileList.begin(), m_QuivFileList.end(), QuiverFileCompare);
						//m_QuivFileList.sort(QuiverFileCompare);
						SetCurrentImage("");
						//directory
					}
					else if (info->type == GNOME_VFS_FILE_TYPE_REGULAR)
					{ // regular file
						cout << "adding file" << endl;
						if (GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME & info->valid_fields)
						{
							//print the symlink
							g_free(uri);
							uri = gnome_vfs_make_uri_from_shell_arg(info->symlink_name);
						}
						// get parent directory, then set the current image to uri

						vfs_uri = gnome_vfs_uri_new (uri);
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
									cout << "adding parent dir" << endl;
									// we can open the dir
									AddDirectory(parent_str);
									std::sort(m_QuivFileList.begin(), m_QuivFileList.end(), QuiverFileCompare);
									//m_QuivFileList.sort(QuiverFileCompare);
									SetCurrentImage(uri);
									//gnome_vfs_directory_close (dir_handle);
								}
								else
								{
									cout << "not adding parent dir" << endl;
									printf("dir info type: %d, %s\n",info->type, parent_str);

									// if we can't open the dir, just add the file
									// (ie, if it is http)
									AddFile(uri);
									SetCurrentImage("");
								}
								
								//cout << "freeing parent" << endl;
								g_free (parent_str);
								gnome_vfs_file_info_unref (dir_info);
								//cout << "freed parent" << endl;
							}
							else
							{
								// just add the file
								AddFile(uri);
								SetCurrentImage("");
							}
							
							gnome_vfs_uri_unref(vfs_uri);
						}
							
					} // end not a directory
					else
					{
						//cout <<" who knows!" << endl;
					}
					
				}
				gnome_vfs_file_info_unref (info);
			}
			else
			{
				cout << "ERROR: could not get file info for " << uri << endl;
			}
			/*
			AddFile(uri);
			*/
			//cout << " adding " << uri << endl;
		}
		g_free(uri);
	}
	else
	{
		list<string>::iterator itr;
		for (itr = file_list->begin(); file_list->end() != itr ; ++itr)
		{
			GnomeVFSResult result;
			gchar* uri = gnome_vfs_make_uri_from_shell_arg (itr->c_str());
			GnomeVFSFileInfo * info = gnome_vfs_file_info_new ();
			result = gnome_vfs_get_file_info (uri,info,(GnomeVFSFileInfoOptions)
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
						AddDirectory(uri);
						//directory
					}
					else if (info->type == GNOME_VFS_FILE_TYPE_REGULAR)
					{
						AddFile(uri,info);
					}
				}
			}
			gnome_vfs_file_info_unref(info);
			g_free(uri);
		}
		//m_QuivFileList.sort(QuiverFileCompare);
		std::sort(m_QuivFileList.begin(), m_QuivFileList.end(), QuiverFileCompare);
		SetCurrentImage("");
	}
	if (!current.empty())
	{
		SetCurrentImage(current);
	}
}

void ImageListImpl::SetImageList(list<string> *file_list)
{
	//printf("Setting the image list\n");
	if (0 == file_list->size())
	{
		if (0 == m_QuivFileList.size())
			SetCurrentImage("");
		return;
	}
	
	m_QuivFileList.clear();
	Add(file_list);
	//printf("done setting the image list\n");
}


void ImageListImpl::AddDirectory(const gchar* uri)
{
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
				GnomeVFSURI * vfs_uri_file = gnome_vfs_uri_append_path(vfs_uri_dir,info->name);
				gchar *str_uri_file = gnome_vfs_uri_to_string (vfs_uri_file,GNOME_VFS_URI_HIDE_NONE);
				
				//printf("uri: %s\n",str_uri_file);
				gnome_vfs_uri_unref(vfs_uri_file);

				// FIXME:  what should the info be?
				// 
				AddFile(str_uri_file,info);
				//cout << "freeing uri file " << endl;
				free (str_uri_file);
				//cout << "freed uri file " << endl;
			}
			gnome_vfs_file_info_unref (info);
		}
		gnome_vfs_uri_unref(vfs_uri_dir);
		gnome_vfs_directory_close (dir_handle);
	}
}



void ImageListImpl::AddFile(const gchar*  uri)
{
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
		AddFile(uri,info);
	}
	gnome_vfs_file_info_unref(info);
}

void ImageListImpl::AddFile(const gchar* uri, GnomeVFSFileInfo *info)
{
	if ( c_setSupportedMimeTypes.end() != c_setSupportedMimeTypes.find( info->mime_type ) )
	{
		QuiverFile f(uri,info);
		m_QuivFileList.push_back(f);
	}
	else
	{
		//printf("Unsupported mime_type: %s\n",info->mime_type);
	}
}

void ImageListImpl::Sort(ImageList::SortOrder o,bool descending)
{
	//if (descending)
		//m_QuivFileList.reverse();
}



bool ImageListImpl::QuiverFileCompare(const QuiverFile & l , const QuiverFile & r)
{
	QuiverFile left = l,right = r;
	int retval = false;

	switch (c_SortOrder)
	{
		case ImageList::SORT_FILENAME:
			retval = (0 > strcmp(left.GetURI(),right.GetURI()) );
			
		break;

		case ImageList::SORT_FILE_EXTENSION:
		break;

		case ImageList::SORT_FILE_DATE:
		break;
		
		case ImageList::SORT_EXIF_DATE:
		break;
	}
	return retval;
}

