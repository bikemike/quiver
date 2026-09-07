#ifndef FILE_EXTERNAL_TOOL_TASK_H
#define FILE_EXTERNAL_TOOL_TASK_H

#include "AbstractTask.h"
#include <string>
#include <vector>
#include <mutex>
#include <gio/gio.h>
#include <boost/shared_ptr.hpp>

class ExternalToolTask;
typedef boost::shared_ptr<ExternalToolTask> ExternalToolTaskPtr;

class ExternalToolTask : public AbstractTask
{
public:
	ExternalToolTask(const std::string& strToolName, const std::vector<std::string>& vectCommands);
	virtual ~ExternalToolTask();

	virtual std::string GetDescription() const override;
	virtual std::string GetIterationTypeName(bool shortname = false, bool plural = true) const override;
	virtual int GetTotalIterations() const override;
	virtual int GetCurrentIteration() const override;
	virtual double GetProgress() const override;
	virtual std::string GetProgressText() const override;

	virtual bool CanCancel() const override;
	virtual bool CanPause() const override;
	virtual void Cancel() override;

	virtual bool HasDetails() const override;
	virtual std::string GetDetails() const override;

protected:
	virtual void Run() override;
	virtual void Cancelled() override;

private:
	struct CommandResult {
		std::string cmd;
		int exitCode;
		bool running;
		bool finished;
		bool cancelled;
		std::string stdoutStr;
		std::string stderrStr;
	};

	std::string m_strToolName;
	std::vector<std::string> m_vectCommands;
	std::vector<CommandResult> m_vectResults;
	size_t m_iCurrentCommand;

	mutable std::mutex m_Mutex;
	GSubprocess* m_pCurrentProc;
	GCancellable* m_pCancellable;
};

#endif // FILE_EXTERNAL_TOOL_TASK_H
