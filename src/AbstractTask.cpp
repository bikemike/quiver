#include "AbstractTask.h"

#include <iostream>
#include <sys/time.h>

using namespace std;

AbstractTask::AbstractTask() :  
	m_bStarted(false), m_bShouldCancel(false), m_bCancelled(false), 
	m_bShouldPause(false), m_bPaused(false), m_bFinished(false),
	m_dElapsedSeconds(0.),
	m_bTimerRunning(false),
	m_MessageType(MSG_TYPE_INFO)
{
}

AbstractTask::~AbstractTask() 
{

}

double AbstractTask::GetRunningTimeSeconds() const
{
	double dCurrentSeconds = 0.;

	if ( m_bTimerRunning )
	{
		timeval tv_end;
		gettimeofday(&tv_end,NULL);

		double starttime = (double)m_tv_start.tv_sec + ((double)m_tv_start.tv_usec)/1000000;
		double endtime = (double)tv_end.tv_sec + ((double)tv_end.tv_usec)/1000000;
		dCurrentSeconds = endtime - starttime;
	}
	
	return m_dElapsedSeconds + dCurrentSeconds;
}

bool AbstractTask::CanCancel() const
{
   	return true;
}
bool AbstractTask::CanPause() const
{
   	return true;
}

// stop means cancel the work in progress
// (means not running and finished)
void AbstractTask::Cancel()
{
	if (CanCancel())
	{
		if (!ShouldCancel() && !IsRunning() && !IsFinished())
		{
			m_bShouldCancel = true;
			m_bFinished = true;
			EmitTaskCancelledEvent();	
		}
		m_bShouldCancel = true;
	}
}

// paused means not running and not finished
void AbstractTask::Pause ()
{ 
	if (CanPause())
	{
		if (!ShouldPause() && !IsRunning() && !IsFinished())
		{
			m_bShouldPause = true;
			m_bPaused = true;
			EmitTaskPausedEvent();	
		}
		m_bShouldPause = true; 
	}
}

void AbstractTask::Resume()
{
	if (IsPaused())
	{
		m_bShouldPause = false; 
		EmitTaskUnpausedEvent();
	}
}


// running is started and not paused and not finished
bool AbstractTask::IsRunning() const
{
   	return m_bStarted && !m_bPaused && !m_bFinished; 
}

bool AbstractTask::IsStarted() const
{
   	return m_bStarted;
}

bool AbstractTask::IsFinished() const
{
   	return m_bFinished;
}

bool AbstractTask::IsPaused() const
{
	return m_bPaused && !m_bFinished; 
}


AbstractTask::MessageType AbstractTask::GetMessageType()  const
{
   	return m_MessageType;
}

std::string AbstractTask::GetMessage() const
{
   	return m_strMsg;
}


// called by the task manager
// internally calls the virtual Run() method
void AbstractTask::RunTask()
{
	if (IsFinished())
	{
		return;
	}

	StartTimer();

	if (!IsStarted())
	{
		m_bStarted = true;
		EmitTaskStartedEvent();
	}

	if (IsPaused())
	{
		m_bPaused = false;
		EmitTaskResumedEvent();
	}

	if (!ShouldCancel() && !ShouldPause())
	{
		// run the task
		Run();
	}

	StopTimer();

	if (ShouldPause())
	{
		m_bPaused = true;
		EmitTaskPausedEvent();
	}
	else 
	{
		m_bFinished = true;
		if (ShouldCancel())
		{
			m_bCancelled = true;
			EmitTaskCancelledEvent();
		}
		else
		{
			EmitTaskFinishedEvent();
		}
	}
}

std::string AbstractTask::GetProgressText() const
{
	return m_strProgressText;
}

void AbstractTask::SetProgressText(std::string strText)
{
	m_strProgressText = strText;
}

void AbstractTask::SetMessage(MessageType msgType, std::string strMsg)
{
	m_MessageType = msgType;
	m_strMsg= strMsg;
	EmitTaskMessageEvent();
}

bool AbstractTask::ShouldCancel()  const
{
	return m_bShouldCancel;
}

bool AbstractTask::ShouldPause()  const
{
	return m_bShouldPause;
}

void AbstractTask::StartTimer()
{
	gettimeofday(&m_tv_start,NULL);
	m_bTimerRunning = true;
}

void AbstractTask::StopTimer()
{
	timeval tv_end;
	gettimeofday(&tv_end,NULL);

	double starttime = (double)m_tv_start.tv_sec + ((double)m_tv_start.tv_usec)/1000000;
	double endtime = (double)tv_end.tv_sec + ((double)tv_end.tv_usec)/1000000;

	m_dElapsedSeconds += endtime - starttime;
	m_bTimerRunning = false;
}

