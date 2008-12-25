#ifndef FILE_RENAME_TASK_H
#define FILE_RENAME_TASK_H

#include "AbstractTask.h"
#include "QuiverFile.h"

#include "ImageList.h"

#include <string>
#include <vector>

class RenameTask : public AbstractTask
{

public:
	                    RenameTask();
	virtual             ~RenameTask();

	virtual std::string GetDescription() const;

	// quantity type may be kb, items, images, files, 
	// or anything else the task iterates over
	virtual std::string GetIterationTypeName(bool shortname = false, bool plural = true) const;

	// get total and current iteration
	virtual int         GetTotalIterations() const;
	virtual int         GetCurrentIteration() const;

	virtual double      GetProgress() const;

	void                AddFile(QuiverFile quiverFile);
	void                AddFiles(std::vector<QuiverFile> vectQuiverFiles);


	void                SetInputFolder(std::string strSrcURI);
	void                SetIncludeSubfolders(bool bIncludeSubfolders);

	// the output directory must be specified
	void                SetOutputFolder(std::string strDestDirURI);

	// if not set, the default format is: YYYY-MM-DD
	// pulled from the exif data
	void                SetTemplate(std::string strTemplate);

	void                SetStartNumber(int number);

	void                SetSortBy(ImageList::SortBy sortBy);

	class PrivateImpl;
	typedef boost::shared_ptr<PrivateImpl> PrivateImplPtr;
protected:
	virtual void        Run();

private:
	PrivateImplPtr m_PrivateImplPtr;
	std::string DoVariableSubstitution(QuiverFile f, std::string strTemplate);

	unsigned int      m_iCurrentFile;
	std::vector<QuiverFile> m_vectQuiverFiles;

	std::string       m_strSrcDirURI;
	bool              m_bIncludeSubfolders;
	std::string       m_strDestDirURI;
	std::string       m_strTemplate;
	int               m_iStartNumber;
	ImageList::SortBy m_eSortBy;
};

typedef boost::shared_ptr<RenameTask> RenameTaskPtr;

#endif // FILE_RENAME_TASK_H

