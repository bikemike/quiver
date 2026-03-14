#ifndef FILE_ORGANIZE_TASK_H
#define FILE_ORGANIZE_TASK_H

#include "AbstractTask.h"
#include "QuiverFile.h"

#include <string>
#include <vector>

class OrganizeTask : public AbstractTask
{

public:
	                    OrganizeTask();
	virtual             ~OrganizeTask();

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
	void                SetFolderTemplate(std::string strTemplate);

	void                SetRenameFiles(bool bRenameFiles);
	void                SetFileTemplate(std::string strTemplate);

	void                SetAppendedText(std::string strAppend);

	void                SetDayExtension(int extension);

	static std::string  DoVariableSubstitution(std::string strTemplate, GDateTime* datetime, int dayExtension = 0);

	class PrivateImpl;
	typedef boost::shared_ptr<PrivateImpl> PrivateImplPtr;
protected:
	virtual void        Run();
	void                Cancelled();

private:
	PrivateImplPtr m_PrivateImplPtr;

	int m_iCurrentFile;
	std::vector<QuiverFile> m_vectQuiverFiles;

	std::string m_strSrcDirURI;
	bool        m_bIncludeSubfolders;
	bool        m_bRenameFiles;
	std::string m_strDestDirURI;
	std::string m_strFolderTemplate;
	std::string m_strFileTemplate;
	std::string m_strAppendedText;
	int         m_iDayExtension;
};

typedef boost::shared_ptr<OrganizeTask> OrganizeTaskPtr;

#endif // FILE_ORGANIZE_TASK_H

