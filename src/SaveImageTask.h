#ifndef FILE_SAVE_IMAGE_TASK_H
#define FILE_SAVE_IMAGE_TASK_H

#include "AbstractTask.h"
#include "QuiverFile.h"

#include <vector>

class SaveImageTask : public AbstractTask
{
public:
	                    SaveImageTask(QuiverFile file);

	virtual std::string GetDescription() const;

	// quantity type may be kb, items, images, files, 
	// or anything else the task iterates over
	virtual std::string GetIterationTypeName(bool shortname = false, bool plural = true) const;

	// get total and current iteration
	virtual int         GetTotalIterations() const;
	virtual int         GetCurrentIteration() const;

	virtual double      GetProgress() const;

	virtual bool        IsHidden() const {return true;}

protected:
	virtual void        Run();

private:
	double m_dFileSavePercent;
	QuiverFile m_QuiverFile;
};

typedef boost::shared_ptr<SaveImageTask> SaveImageTaskPtr;

#endif // FILE_SAVE_IMAGE_TASK_H

