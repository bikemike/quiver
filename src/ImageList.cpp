#include <config.h>

#include <iostream>
#include <algorithm>
#include <gtk/gtk.h>

#include <libgnomevfs/gnome-vfs.h>
#include <stdlib.h>

#include "ImageList.h"

using namespace std;

ImageList::SortOrder ImageList::c_SortOrder;
set<string> ImageList::c_setSupportedMimeTypes;

ImageList::ImageList()
{
	LoadMimeTypes();
	c_SortOrder = SORT_FILENAME;
}


ImageList::ImageList(std::string filepath)
{
	LoadMimeTypes();
	c_SortOrder = SORT_FILENAME;
	list<string> file_list;
	file_list.push_back(filepath);
	SetImageList(&file_list);
}

void ImageList::LoadMimeTypes()
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


void ImageList::RemoveCurrentImage()
{
	// lets remove the current image;
	string new_current;
	if (HasNext())
	{
		new_current = PeekNext();
	}
	else if (HasPrevious())
	{
		new_current = PeekPrevious();
	}
	if ( 0 < GetSize() )
	{
		m_ImageList.erase(m_itrCurrent);
	}
	SetCurrentImage(new_current);
}

void ImageList::SetImageList(list<string> file_list)
{
	SetImageList(&file_list);
}

void ImageList::AddImageList(std::list<std::string> *file_list)
{
	if (0 == file_list->size())
	{
		if (0 == GetSize())
			SetCurrentImage("");
		return;
	}
	
	string current;
	if (0 < GetSize())
	{
		current = GetCurrent();
	}
	
	if (1 == file_list->size())
	{
		m_iIndex = 1;

		gchar* uri = gnome_vfs_make_uri_from_shell_arg (file_list->front().c_str());
		
		//cout << "is this valid: " << uri << endl;
		
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
											  GNOME_VFS_FILE_INFO_FOLLOW_LINKS)
											  );
			if ( GNOME_VFS_OK == result )
			{
				if (GNOME_VFS_FILE_INFO_FIELDS_TYPE  & info->valid_fields)
				{
					if (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY)
					{
						cout << "adding dir" << endl;
						AddDirectory(uri);
						m_ImageList.sort(QuiverFileCompare);
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
									m_ImageList.sort(QuiverFileCompare);
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
											  GNOME_VFS_FILE_INFO_FOLLOW_LINKS)
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
		m_ImageList.sort(QuiverFileCompare);
		SetCurrentImage("");
	}
	if (!current.empty())
	{
		SetCurrentImage(current);
	}
}

void ImageList::SetImageList(list<string> *file_list)
{
	if (0 == file_list->size())
	{
		if (0 == GetSize())
			SetCurrentImage("");
		return;
	}
	
	m_ImageList.clear();
	AddImageList(file_list);
}


void ImageList::AddDirectory(const gchar* uri)
{
	GnomeVFSResult result;
	GnomeVFSDirectoryHandle *dir_handle;
	GnomeVFSFileInfo *info;

	result = gnome_vfs_directory_open (&dir_handle,uri,(GnomeVFSFileInfoOptions)
							  (GNOME_VFS_FILE_INFO_DEFAULT|
							  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
							  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
							  GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	//printf("opened dir: %d: %s\n",result,gnome_vfs_result_to_string (result));
	if ( GNOME_VFS_OK == result )
	{
		GnomeVFSURI * vfs_uri_dir = gnome_vfs_uri_new(uri);
		info = gnome_vfs_file_info_new ();
		while ( GNOME_VFS_OK == result )
		{
			result = gnome_vfs_directory_read_next(dir_handle,info);
			
			if (GNOME_VFS_OK == result )
			{
				GnomeVFSURI * vfs_uri_file = gnome_vfs_uri_append_path(vfs_uri_dir,info->name);
				gchar *str_uri_file = gnome_vfs_uri_to_string (vfs_uri_file,GNOME_VFS_URI_HIDE_NONE);
				
				//printf("uri: %s\n",str_uri_file);
				gnome_vfs_uri_unref(vfs_uri_file);
				AddFile(str_uri_file,info);
				//cout << "freeing uri file " << endl;
				free (str_uri_file);
				//cout << "freed uri file " << endl;
			}
		}
		gnome_vfs_uri_unref(vfs_uri_dir);
		gnome_vfs_file_info_unref (info);
		gnome_vfs_directory_close (dir_handle);
	}
}

void ImageList::AddFile(const gchar*  uri)
{
	GnomeVFSResult result;
	GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,info,(GnomeVFSFileInfoOptions)
									  (GNOME_VFS_FILE_INFO_DEFAULT|
									  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
									  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
									  GNOME_VFS_FILE_INFO_FOLLOW_LINKS)
									 );
	if ( GNOME_VFS_OK == result )
	{
		AddFile(uri,info);
	}
	gnome_vfs_file_info_unref(info);
}
void ImageList::AddFile(const gchar* uri, GnomeVFSFileInfo *info)
{
	if ( c_setSupportedMimeTypes.end() != c_setSupportedMimeTypes.find( info->mime_type ) )
	{
		QuiverFile f(uri,info);
		printf("Adding %s to list: %s\n",uri,info->mime_type);
		m_ImageList.push_back(f);
	}
	else
	{
		printf("Unsupported mime_type: %s\n",info->mime_type);
	}
}

void ImageList::SetCurrentImage(string uri)
{
	m_itrCurrent = m_ImageList.begin();
	m_iIndex = 1;
	
	if (!uri.empty())
	{
		list<QuiverFile>::iterator itr;
		int i;
		for (i = 1,itr = m_ImageList.begin();itr != m_ImageList.end(); ++itr,i++)
		{
			if (itr->GetURI() == uri)
			{
				m_iIndex = i;
				m_itrCurrent = itr;
				break;
			}
		}
	}
}

bool ImageList::HasNext()
{
	list<QuiverFile>::iterator itr;
	itr = m_itrCurrent;
	return ( ++itr != m_ImageList.end() );
}

bool ImageList::HasPrevious()
{
	return ( m_itrCurrent != m_ImageList.begin() );
}

string ImageList::GetNext()
{
	if (HasNext())
	{
		//printf("index: %d\n",m_iIndex);
		m_iIndex++;
		return ((++m_itrCurrent)->GetURI());
	}
	return string();
}

string ImageList::PeekNext()
{
	if (HasNext())
	{
		std::list<QuiverFile>::iterator itr = m_itrCurrent;
		return ((++itr)->GetURI());
	}
	return string();
}


string ImageList::GetCurrent()
{
	if (GetSize() && m_itrCurrent != m_ImageList.end())
		return m_itrCurrent->GetURI();
	else
		return string();
}

int ImageList::GetCurrentIndex()
{
	return m_iIndex;
}

string ImageList::GetPrevious()
{
	if (HasPrevious())
	{
		m_iIndex--;
		return (--m_itrCurrent)->GetURI();
	}
	return string();
}
string ImageList::PeekPrevious()
{
	if (HasPrevious())
	{
		std::list<QuiverFile>::iterator itr = m_itrCurrent;
		return (--itr)->GetURI();
	}
	return string();
}

int ImageList::GetSize()
{
	return m_ImageList.size();
}

void ImageList::Sort(SortOrder o,bool reverse)
{
	if (reverse)
		m_ImageList.reverse();
}


bool ImageList::QuiverFileCompare(const QuiverFile & l , const QuiverFile & r)
{
	QuiverFile left = l,right = r;
	int retval = false;

	switch (c_SortOrder)
	{
		case SORT_FILENAME:
			retval = (0 > strcmp(left.GetURI(),right.GetURI()) );
			
		break;

		case SORT_FILE_EXTENSION:
		break;

		case SORT_FILE_DATE:
		break;
		
		case SORT_EXIF_DATE:
		break;
	}
	return retval;
}
