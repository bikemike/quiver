#include <config.h>

#include <sqlite3.h>
#include <iostream>
#include <fstream>

#include <list>

#include <libgnomevfs/gnome-vfs.h>
#include <libxml++/libxml++.h>

#include "Database.h"
#include "QuiverFile.h"
#include "GlobalColourDescriptor.h"

DatabasePtr Database::c_pDatabasePtr;

enum
{
	QUIVER_DB_COLUMN_ID = 0,
	QUIVER_DB_COLUMN_FILENAME,
	QUIVER_DB_COLUMN_PATHNAME,
	QUIVER_DB_COLUMN_THUMBPATH,
	QUIVER_DB_COLUMN_MTIME,
	QUIVER_DB_COLUMN_FEATURES
};

static int query_callback(void *args, int num_cols, char **value, char **column_name)
{
	for(int i=0; i<num_cols; i++)
	{
		cout << column_name[i] << ": " << value[i] << endl;
	}
	
	return 0;
}

Database::Database()
{
	// constructor
}

Database::~Database()
{
	// destructor
	Close();
}

DatabasePtr Database::GetInstance()
{
	if (NULL == c_pDatabasePtr.get())
	{
		DatabasePtr prefsPtr(new Database());
		c_pDatabasePtr = prefsPtr;
	}
	return c_pDatabasePtr;
}

int Database::Open(std::string db_path)
{
	cout << "Opening database..." << endl;
	int res = sqlite3_open(db_path.c_str(), &m_pDB);
	if(SQLITE_OK != res)
	{
		cerr << "Could not open image database: " << sqlite3_errcode(m_pDB) << ": " << sqlite3_errmsg(m_pDB) << endl;
		sqlite3_close(m_pDB);
		return false;
	}
	
	char *errmsg;
	// Until I find a better way, just create the table if it doesn't exist
	res = sqlite3_exec(m_pDB, "CREATE TABLE Images(ID INTEGER PRIMARY KEY, Filename TEXT, Pathname TEXT, ThumbPath TEXT, mtime BIGINT, Features BLOB)", NULL, NULL, &errmsg);

	if(SQLITE_OK != res)
	{
		cerr << errmsg << endl;
		sqlite3_free(errmsg);
	}

	return true;
}

void Database::Close()
{
	cout << "Closing database..." << endl;
	sqlite3_close(m_pDB);
}

int Database::AddImage(string img_path, string thmb_path, time_t mtime)
{
	// add an image
	int res;
	char *errmsg;

	time_t last_mtime = GetLastModified(img_path);
	
	if(last_mtime == -1)
	{
		cerr << "Could not reliably gather last modified time for " << img_path << endl; 
		return false;
	}
	
	if(last_mtime == mtime)
	{
		return true;
	}

	if(last_mtime == 0)
	{
		cout << "New image: " << img_path.c_str() << endl;
	}
	else if(last_mtime != mtime)
	{
		cout << "Image changed, updating features: " << img_path.c_str() << endl;
	}
	
	// Extract some features somewhere around here...
	// Create a blob to stick in the database
	int nBins=0;
	QuiverFile *file = new QuiverFile(img_path.c_str());
	GlobalColourDescriptor *desc = new GlobalColourDescriptor(file);
	res = desc->Calculate(64, 128);
	if(res == -1)
	{
		cerr << "Could not load thumbnail for processing: " << img_path.c_str() << endl;
		return false;
	}
	int *blob;
	int count = desc->GetPackedHistogram(&blob, &nBins);
	
//	for(int i=0; i<count*4; i++)
//	{
//		if(i%16==0) printf("\n");
//		printf("%02x ", ((unsigned char*)blob)[i]);
//	}
//	printf("\n");
	desc->LoadPackedHistogram(blob);
	
//	delete [] blob;
	
	count = desc->GetPackedHistogram(&blob, &nBins);
	
//	for(int i=0; i<count; i++)
//	{
//		if(i%16==0) printf("\n");
//		printf("%d ", blob[i]);
//	}
//	printf("\n");

	delete desc;
	delete file;

//	cout << nBins << " colour bins per channel" << endl;
//	cout << count*sizeof(int) << " bytes written to blob for " << img_path.c_str() << endl;	

	sqlite3_stmt *pStmt;
		
	if(last_mtime == 0)
	{		
		res = sqlite3_prepare_v2(m_pDB, "INSERT INTO Images(ID, Filename, Pathname, ThumbPath, mtime, Features) VALUES(NULL, '', ?, '', ?, ?);", -1, &pStmt, 0);
	
		if(res != SQLITE_OK)
		{
			cerr << "Error calling sqlite3_prepare_v2: " << sqlite3_errmsg(m_pDB) << endl;
			delete [] blob;
			return false;
		}
		
		sqlite3_bind_text(pStmt, 1, img_path.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(pStmt, 2, mtime);
		sqlite3_bind_blob(pStmt, 3, blob, count*sizeof(int), SQLITE_TRANSIENT);

	}
	else if(last_mtime != mtime)
	{
		// Finally, update the mtime so we know the last time
		// we updated this image
		res = sqlite3_prepare_v2(m_pDB, "UPDATE Images SET mtime=?,Features=? WHERE Pathname=?", -1, &pStmt, 0);
		
		if(res != SQLITE_OK)
		{
			cerr << "Error calling sqlite3_prepare_v2: " << sqlite3_errmsg(m_pDB) << endl;
			delete [] blob;
			return false;
		}
		
		sqlite3_bind_int64(pStmt, 1, mtime);
		sqlite3_bind_blob(pStmt, 2, blob, count*sizeof(int), SQLITE_TRANSIENT);
		sqlite3_bind_text(pStmt, 3, img_path.c_str(), -1, SQLITE_TRANSIENT);
	}

	delete [] blob;

	res = sqlite3_step(pStmt);
	if(res != SQLITE_DONE)
	{
		cerr << "Error calling sqlite3_step: " << sqlite3_errmsg(m_pDB) << endl;
		sqlite3_finalize(pStmt);
		return false;
	}
	
	sqlite3_finalize(pStmt);

	return true;
}

int Database::RemoveImage(int imageId)
{
	// remove an image from the database (don't touch the file itself)
	char *errmsg;
	char *query = sqlite3_mprintf("DELETE FROM Images WHERE ID=%ld", imageId);
	
	int res = sqlite3_exec(m_pDB, query, NULL, NULL, &errmsg);
	sqlite3_free(query);

	if(SQLITE_OK != res)
	{
		cerr << errmsg << endl;
		sqlite3_free(errmsg);
	}

	return true;
}


int Database::GetImage(string img_path)
{
	// returns the imageId of an image from it's pathname
	// - can be used to verify whether the database is up to date
	// returns imageId if found, zero otherrwise
	return 0;
}

// GetLastModified()
// Returns the last modified date stored in the database
// or zero if the image does not exist in the
// database or -1 if an error occurs
time_t Database::GetLastModified(string img_path)
{
	char *query = sqlite3_mprintf("SELECT mtime FROM Images WHERE Pathname='%q';", img_path.c_str());
	
	sqlite3_stmt *pStmt;
	int res = sqlite3_prepare_v2(m_pDB, query, -1, &pStmt, NULL);
	sqlite3_free(query);
	if(SQLITE_OK != res)
	{
		cerr << "sqlite3_prepare: " << sqlite3_errmsg(m_pDB) << endl;
		sqlite3_free(pStmt);
		return 0;
	}
	
	res = sqlite3_step(pStmt);
	switch(res)
	{
		case SQLITE_BUSY:
		case SQLITE_ERROR:
		case SQLITE_MISUSE:
			// Some error, busy, syntax, etc.
			cerr << "sqlite3_step: " << sqlite3_errmsg(m_pDB) << endl;
			sqlite3_free(pStmt);
			return -1;
		case SQLITE_ROW:
		{
			// Image found
			time_t last_mtime = sqlite3_column_int64(pStmt, 0);
			sqlite3_finalize(pStmt);
			return last_mtime;
		}
		case SQLITE_DONE:
			// Image does not exist in database
			sqlite3_finalize(pStmt);
			return 0;
	}
	
	return 0;
}

int Database::IndexFolder(string folder, bool bRecursive)
{
	// called to verify the contents of a folder have not changed
//	cout << "--- Looking for new images in " << folder.c_str() << endl;
	
	GnomeVFSResult result;
	GnomeVFSDirectoryHandle *dir_handle;

	result = gnome_vfs_directory_open (&dir_handle,folder.c_str(),(GnomeVFSFileInfoOptions)
							  (GNOME_VFS_FILE_INFO_DEFAULT|
							  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
							  GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
							  GNOME_VFS_FILE_INFO_FOLLOW_LINKS|
							  GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS
							  ));
	//printf("opened dir: %d: %s\n",result,gnome_vfs_result_to_string (result));
	if ( GNOME_VFS_OK == result )
	{
		GnomeVFSURI * vfs_uri_dir = gnome_vfs_uri_new(folder.c_str());

		// BEGIN TRANSACTION
		int res = sqlite3_exec(m_pDB, "BEGIN TRANSACTION;", 0, 0, 0);
			
		while ( GNOME_VFS_OK == result )
		{
			GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();
			result = gnome_vfs_directory_read_next(dir_handle,info);
			
			if (GNOME_VFS_OK == result )
			{
				if (info->type == GNOME_VFS_FILE_TYPE_REGULAR)
				{
					GnomeVFSURI * vfs_uri_file = gnome_vfs_uri_append_path(vfs_uri_dir,info->name);
					gchar *str_uri_file = gnome_vfs_uri_to_string (vfs_uri_file,GNOME_VFS_URI_HIDE_NONE);
					
					gnome_vfs_uri_unref(vfs_uri_file);
					
					if (m_pImageList->IsSupportedFileType(str_uri_file, info))
					{
						AddImage(str_uri_file, "", info->mtime);
					}
					
					free (str_uri_file);
				}
				else if (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY)
				{
					if (strcmp(info->name, "..") == 0 || strcmp(info->name, ".") == 0)
					{
//						cout << "Skipping " << info->name << endl;
						continue;
					}
					
					GnomeVFSURI * vfs_uri_file = gnome_vfs_uri_append_path(vfs_uri_dir,info->name);
					gchar *str_uri_file = gnome_vfs_uri_to_string (vfs_uri_file,GNOME_VFS_URI_HIDE_NONE);
					
					gnome_vfs_uri_unref(vfs_uri_file);

					IndexFolder(str_uri_file, bRecursive);

					free (str_uri_file);
					
				}
			}
			gnome_vfs_file_info_unref (info);
		}
		gnome_vfs_uri_unref(vfs_uri_dir);
		gnome_vfs_directory_close (dir_handle);
		
		// END TRANSACTION
		res = sqlite3_exec(m_pDB, "COMMIT TRANSACTION;", 0, 0, 0);
	}
	
	return true;
}

void Database::SetImageList(ImageList *pImageList)
{
	m_pImageList = pImageList;
}

string Database::GetClosestMatch(string img_path)
{
	// Cycle through the entire database, comparing blobs
	sqlite3_stmt *pStmt;
	
	cout << "searching match for " << img_path << endl;
	
	QuiverFile *file = new QuiverFile(img_path.c_str());
	GlobalColourDescriptor *query = new GlobalColourDescriptor(file);
	query->Calculate(64, 128);
	
	int res = sqlite3_prepare_v2(m_pDB, "SELECT Pathname,Features FROM Images", -1, &pStmt, 0);
	
	if(res != SQLITE_OK)
	{
		cerr << "Error calling sqlite3_prepare_v2: " << sqlite3_errmsg(m_pDB) << endl;
		delete file;
		delete query;
		return false;
	}
	
	unsigned int mindist=0xffffffff;
	string best;
	while(SQLITE_ROW == (res = sqlite3_step(pStmt)))
	{
		const unsigned char *path = sqlite3_column_text(pStmt, 0);
		const void *blob = sqlite3_column_blob(pStmt, 1);
		int size = sqlite3_column_bytes(pStmt, 1);
		
		if(strcmp((char*)path, img_path.c_str()) == 0)
		{
			// Don't compare this image to itself
			continue;
		}
		
//		printf("comparing %s\n", path);
		
//		for(int i=0; i<size; i++)
//		{
//			if(i%16==0) printf("\n");
//			printf("%02x ", ((unsigned char*)blob)[i]);
//		}
//		printf("\n");
//		
//		printf("is it %d\n", ((int*)blob)[0]);
		
//		cout << size/sizeof(int)-1 << " " << ((int*)blob)[0]*3 << endl;
		g_assert(size/sizeof(int)-1 == ((int*)blob)[0]*3);
		
		GlobalColourDescriptor *desc = new GlobalColourDescriptor();
		desc->LoadPackedHistogram((int*)blob);
//		desc->Calculate(64,128);

		int dist = *query - *desc;
		
		//cout << "Dist for " << path << " is " << dist << endl;
		
		if(dist < mindist && dist >= 0)
		{
			mindist = dist;
			best = (char *)path;
//			cout << path << " has shortest distance! " << dist << endl;
			
//			for(int i=0; i<size; i++)
//			{
//				if(i%16==0) printf("\n");
//				printf("%02x ", ((unsigned char*)blob)[i]);
//			}
//			printf("\n");
		}
		delete desc;
	}

	cout << "closest match to " << img_path << " is " << best << " with a distance of " << mindist << endl;
	delete query;
	delete file;
	
	return best;
}
