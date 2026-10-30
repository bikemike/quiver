#ifndef FILE_SAVE_IMAGE_TASK_H
#define FILE_SAVE_IMAGE_TASK_H

#include "AbstractTask.h"
#include "QuiverFile.h"

#include <functional>
#include <vector>

class SaveImageTask : public AbstractTask
{
public:
	typedef std::function<void(QuiverFile, bool)> NotifyCallback;

	                    SaveImageTask(QuiverFile file, NotifyCallback notifyCb = nullptr);
	virtual             ~SaveImageTask();

	virtual std::string GetDescription() const override;

	// quantity type may be kb, items, images, files, 
	// or anything else the task iterates over
	virtual std::string GetIterationTypeName(bool shortname = false, bool plural = true) const override;

	// get total and current iteration
	virtual int         GetTotalIterations() const override;
	virtual int         GetCurrentIteration() const override;

	virtual double      GetProgress() const override;

	virtual bool        IsHidden() const override {return true;}

protected:
	virtual void        Run() override;

private:
	double m_dFileSavePercent;
	QuiverFile m_QuiverFile;
	NotifyCallback m_notifyCb;
};

typedef boost::shared_ptr<SaveImageTask> SaveImageTaskPtr;

#endif // FILE_SAVE_IMAGE_TASK_H

