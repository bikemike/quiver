#ifndef FILE_VIDEO_DATE_EDIT_TASK_H
#define FILE_VIDEO_DATE_EDIT_TASK_H

#include "AbstractTask.h"
#include "QuiverFile.h"

// Sets a video's creation_time metadata via a lossless stream-copy remux
// (no re-encode). Writes to a temp file in the same directory, then
// atomically replaces the original on success.
class VideoDateEditTask : public AbstractTask
{
public:
	// new_epoch is the desired creation instant; it is written as an ISO 8601
	// string with local UTC offset so readers that honor offsets agree with
	// quiver's naive-local-wall-clock interpretation of offset-less times.
	                    VideoDateEditTask(QuiverFile f, time_t new_epoch);

	virtual std::string GetDescription() const;
	virtual std::string GetIterationTypeName(bool shortname = false, bool plural = true) const;
	virtual int         GetTotalIterations() const;
	virtual int         GetCurrentIteration() const;
	virtual double      GetProgress() const;

protected:
	virtual void        Run();

private:
	QuiverFile m_QuiverFile;
	time_t     m_tNewEpoch;
	double     m_dPercent;
};

typedef boost::shared_ptr<VideoDateEditTask> VideoDateEditTaskPtr;

#endif // FILE_VIDEO_DATE_EDIT_TASK_H
