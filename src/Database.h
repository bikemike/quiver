#ifndef DATABASE_H_
#define DATABASE_H_

#include "ImageList.h"

#include <string>

using namespace std;

class Database
{
public:
	//constructor
	Database();
	~Database();
	
	int Open(string db_path);
	void Close();
	
	int AddImage(string img_path, string thmb_path, time_t mtime);
	int RemoveImage(int imageId);
	
	int GetImage(string img_path);
	time_t GetLastModified(string img_path);
	
	int IndexFolder(string folder, bool bRecursive);
	
	void SetImageList(ImageList* pImageList);
	
	// Query functions...
	int GetClosestMatch(string img_path);
private:
	struct sqlite3 *m_pDB;
	ImageList *m_pImageList;
};

#endif /*DATABASE_H_*/
