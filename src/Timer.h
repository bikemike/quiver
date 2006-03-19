#ifndef FILE_TIMER_H
#define FILE_TIMER_H

#include <sys/time.h>
#include <string>
#include <iostream>

class Timer
{
public:
	Timer();
	Timer(bool);
	Timer(std::string strFunctionName);
	double GetRunningTimeInSeconds();
	
	~Timer();
	

private:
	bool m_bQuiet;
	void StartTimer();
	timeval m_tv_start;
	std::string m_strFunctionName;
};

#endif
