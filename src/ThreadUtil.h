#ifndef FILE_THREAD_UTIL_H
#define FILE_THREAD_UTIL_H
namespace ThreadUtil
{
	// call init from GUI thread
void Init();
bool IsGUIThread();

}

#endif// FILE_THREAD_UTIL_H
