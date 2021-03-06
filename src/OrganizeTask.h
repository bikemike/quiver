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
	void                SetDateTemplate(std::string strTemplate);

	void                SetAppendedText(std::string strAppend);

	void                SetDayExtension(int extension);

	class PrivateImpl;
	typedef boost::shared_ptr<PrivateImpl> PrivateImplPtr;
protected:
	virtual void        Run();

private:
	PrivateImplPtr m_PrivateImplPtr;
	std::string DoVariableSubstitution(QuiverFile f, std::string strTemplate);

	int m_iCurrentFile;
	std::vector<QuiverFile> m_vectQuiverFiles;

	std::string m_strSrcDirURI;
	bool        m_bIncludeSubfolders;
	std::string m_strDestDirURI;
	std::string m_strDateTemplate;
	std::string m_strAppendedText;
	int         m_iDayExtension;
};

typedef boost::shared_ptr<OrganizeTask> OrganizeTaskPtr;

#endif // FILE_ORGANIZE_TASK_H

