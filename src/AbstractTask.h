#ifndef FILE_ABSTRACT_TASK_H
#define FILE_ABSTRACT_TASK_H

#include <boost/shared_ptr.hpp>

class AbstractTask;

#include "TaskEventSource.h"

#include <string>

class AbstractTask : 
	public TaskEventSource 
{

public:
	                               AbstractTask();

	virtual                        ~AbstractTask();

    typedef enum {
		MSG_TYPE_DEBUG,
		MSG_TYPE_INFO,
		MSG_TYPE_WARNING,
		MSG_TYPE_ERROR,
	} MessageType;

	// return true to hide from UI
	virtual bool                   IsHidden() const {return false;}

	virtual std::string            GetDescription() const = 0;

	// type may be kb, items, images, files, 
	// or anything else the task iterates over
	virtual std::string            GetIterationTypeName(bool shortname, 
			                           bool plural = true) const = 0;

	// get total and current iteration
	virtual int                    GetTotalIterations() const = 0;
	virtual int                    GetCurrentIteration() const = 0;

	double                         GetRunningTimeSeconds() const;

	virtual bool                   CanCancel() const;
	virtual bool                   CanPause() const;

	void                           Cancel();

	void                           Pause ();
	void                           Resume();

	// running is started and not paused and not finished
	bool                           IsRunning() const;
	bool                           IsStarted() const;
	bool                           IsFinished() const;
	bool                           IsPaused() const;

	virtual double                 GetProgress()  const    = 0;
	virtual std::string            GetProgressText() const;

	MessageType                    GetMessageType() const;
	std::string                    GetMessage() const;

	// called by the task manager
	// internally calls the virtual Run() method
	void                           RunTask();

protected:
	// override this method in derived classes
	// make sure to check ShouldCancel and ShouldPause
	// and call EmitTaskProgressUpdatedEvent() now and
	// then
	virtual void                   Run() = 0;

	virtual void                   SetProgressText(std::string strText);

	virtual void                   SetMessage(MessageType, std::string strMsg);

	// pause resume
	bool                           ShouldCancel() const;
	bool                           ShouldPause() const;
	

protected:
	// a task is something you can
	// start and stop, pause
	
	bool                           m_bStarted;
	bool                           m_bShouldCancel;
	bool                           m_bCancelled;
	bool                           m_bShouldPause;
	bool                           m_bPaused;
	bool                           m_bFinished;

	std::string                    m_strProgressText;


private:
	void                           StartTimer();
	void                           StopTimer();

	timeval                        m_tv_start;
	double                         m_dElapsedSeconds;
	bool                           m_bTimerRunning;

	MessageType                    m_MessageType;
	std::string                    m_strMsg;
	
};

typedef boost::shared_ptr<AbstractTask> AbstractTaskPtr;

#endif // FILE_ABSTRACT_TASK_H


