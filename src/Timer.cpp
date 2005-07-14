#include <config.h>
#include "Timer.h"

using namespace std;

Timer::Timer()
{
	StartTimer();
}

Timer::Timer(string strFunctionName)
{
	m_strFunctionName = strFunctionName;
	StartTimer();
}

void Timer::StartTimer()
{
	cout << "Starting timer for " << m_strFunctionName << endl;
	gettimeofday(&m_tv_start,NULL);
}

Timer::~Timer()
{
	timeval tv_end;
	gettimeofday(&tv_end,NULL);

	double starttime = (double)m_tv_start.tv_sec + ((double)m_tv_start.tv_usec)/1000000;
	double endtime = (double)tv_end.tv_sec + ((double)tv_end.tv_usec)/1000000;

  	cout << m_strFunctionName << ": " << endtime - starttime << " seconds" <<  endl;
	
}

