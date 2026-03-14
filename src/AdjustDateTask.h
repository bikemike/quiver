#ifndef FILE_ADJUST_DATE_TASK_H
#define FILE_ADJUST_DATE_TASK_H

#include "AbstractTask.h"
#include "QuiverFile.h"

#include <vector>

class AdjustDateTask : public AbstractTask
{
public:
	enum DateFields
	{
		DATE_FIELD_NONE = 0,
		DATE_FIELD_MODIFICATION_TIME        = 1,
		DATE_FIELD_EXIF_DATE_TIME           = 1 << 1,
		DATE_FIELD_EXIF_DATE_TIME_ORIG      = 1 << 2,
		DATE_FIELD_EXIF_DATE_TIME_DIGITIZED = 1 << 3,
	};
public:
	                    AdjustDateTask(int adj_years, int adj_days, int adj_hours, int adj_mins, int adj_secs );
	                    AdjustDateTask(tm tm_new_date);

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

	void                SetAdjustDateFields(DateFields f);
	void                AddAdjustDateFields(DateFields f);

protected:
	virtual void        Run();

private:
	bool m_bAdjustDate;
	tm   m_tmNewDate;

	int  m_iAdjYears;
	int  m_iAdjDays;
	int  m_iAdjHours;
	int  m_iAdjMins;
	int  m_iAdjSecs;

	int m_iCurrentFile;

	double m_dFileSavePercent;

	DateFields m_flagsDateFields;

	std::vector<QuiverFile> m_vectQuiverFiles;
};

typedef boost::shared_ptr<AdjustDateTask> AdjustDateTaskPtr;

#endif // FILE_ADJUST_DATE_TASK_H

