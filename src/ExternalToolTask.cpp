#include "ExternalToolTask.h"
#include <sstream>

ExternalToolTask::ExternalToolTask(const std::string& strToolName, const std::vector<std::string>& vectCommands)
	: m_strToolName(strToolName),
	  m_vectCommands(vectCommands),
	  m_iCurrentCommand(0),
	  m_pCurrentProc(NULL),
	  m_pCancellable(g_cancellable_new())
{
	m_vectResults.resize(m_vectCommands.size());
	for (size_t i = 0; i < m_vectCommands.size(); ++i)
	{
		m_vectResults[i].cmd = m_vectCommands[i];
		m_vectResults[i].exitCode = -1;
		m_vectResults[i].running = false;
		m_vectResults[i].finished = false;
		m_vectResults[i].cancelled = false;
	}
	SetProgressText("Pending");
}

ExternalToolTask::~ExternalToolTask()
{
	if (m_pCancellable)
	{
		g_object_unref(m_pCancellable);
		m_pCancellable = NULL;
	}
}

std::string ExternalToolTask::GetDescription() const
{
	return "External Tool: " + m_strToolName;
}

std::string ExternalToolTask::GetIterationTypeName(bool shortname, bool plural) const
{
	if (shortname)
	{
		return "cmd";
	}
	return plural ? "commands" : "command";
}

int ExternalToolTask::GetTotalIterations() const
{
	return static_cast<int>(m_vectCommands.size());
}

int ExternalToolTask::GetCurrentIteration() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return static_cast<int>(m_iCurrentCommand);
}

double ExternalToolTask::GetProgress() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	if (m_vectCommands.empty())
	{
		return 1.0;
	}
	if (IsFinished())
	{
		return 1.0;
	}
	return static_cast<double>(m_iCurrentCommand) / static_cast<double>(m_vectCommands.size());
}

std::string ExternalToolTask::GetProgressText() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_strProgressText;
}

bool ExternalToolTask::CanCancel() const
{
	return true;
}

bool ExternalToolTask::CanPause() const
{
	return false;
}

void ExternalToolTask::Cancel()
{
	AbstractTask::Cancel();
	std::lock_guard<std::mutex> lock(m_Mutex);
	if (m_pCancellable)
	{
		g_cancellable_cancel(m_pCancellable);
	}
	if (m_pCurrentProc)
	{
		g_subprocess_force_exit(m_pCurrentProc);
	}
}

void ExternalToolTask::Cancelled()
{
	Cancel();
}

bool ExternalToolTask::HasDetails() const
{
	return true;
}

std::string ExternalToolTask::GetDetails() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	std::ostringstream oss;

	for (size_t i = 0; i < m_vectResults.size(); ++i)
	{
		if (m_vectResults.size() > 1)
		{
			oss << "=== Command " << (i + 1) << " of " << m_vectResults.size() << " ===\n";
		}
		oss << "Command: " << m_vectResults[i].cmd << "\n";
		if (m_vectResults[i].running)
		{
			oss << "Status: Running...\n";
		}
		else if (m_vectResults[i].cancelled)
		{
			oss << "Status: Cancelled\n";
		}
		else if (m_vectResults[i].finished)
		{
			if (m_vectResults[i].exitCode == 0)
			{
				oss << "Status: Completed (exit code 0)\n";
			}
			else
			{
				oss << "Status: Failed (exit code " << m_vectResults[i].exitCode << ")\n";
			}
		}
		else
		{
			oss << "Status: Pending\n";
		}

		if (!m_vectResults[i].stdoutStr.empty())
		{
			oss << "\nOutput (stdout):\n" << m_vectResults[i].stdoutStr;
			if (m_vectResults[i].stdoutStr.back() != '\n')
				oss << "\n";
		}

		if (!m_vectResults[i].stderrStr.empty())
		{
			oss << "\nError output (stderr):\n" << m_vectResults[i].stderrStr;
			if (m_vectResults[i].stderrStr.back() != '\n')
				oss << "\n";
		}

		if (m_vectResults[i].finished && m_vectResults[i].stdoutStr.empty() && m_vectResults[i].stderrStr.empty())
		{
			oss << "(No output produced)\n";
		}

		if (i + 1 < m_vectResults.size())
		{
			oss << "\n";
		}
	}

	return oss.str();
}

void ExternalToolTask::Run()
{
	bool bHadError = false;

	for (size_t i = 0; i < m_vectCommands.size(); ++i)
	{
		if (ShouldCancel())
		{
			break;
		}

		std::string cmd = m_vectCommands[i];

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_iCurrentCommand = i;
			m_vectResults[i].running = true;
			SetProgressText("Running: " + cmd);
		}
		EmitTaskProgressUpdatedEvent();

		const char* argv[] = { "/bin/sh", "-c", cmd.c_str(), NULL };
		GError* error = NULL;
		GSubprocess* proc = g_subprocess_newv(
			argv,
			static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
			&error);

		if (!proc)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_vectResults[i].running = false;
			m_vectResults[i].finished = true;
			m_vectResults[i].exitCode = 127;
			if (error)
			{
				m_vectResults[i].stderrStr = error->message;
				g_error_free(error);
				error = NULL;
			}
			bHadError = true;
			SetMessage(MSG_TYPE_ERROR, "Failed to spawn command");
			SetProgressText("Failed to spawn: " + cmd);
			EmitTaskProgressUpdatedEvent();
			continue;
		}

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_pCurrentProc = proc;
		}

		gchar* stdout_buf = NULL;
		gchar* stderr_buf = NULL;

		g_subprocess_communicate_utf8(
			proc,
			NULL,
			m_pCancellable,
			&stdout_buf,
			&stderr_buf,
			&error);

		int exitCode = -1;
		if (g_subprocess_get_if_exited(proc))
		{
			exitCode = g_subprocess_get_exit_status(proc);
		}
		else if (g_subprocess_get_if_signaled(proc))
		{
			exitCode = 128 + g_subprocess_get_term_sig(proc);
		}

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_pCurrentProc = NULL;
			m_vectResults[i].running = false;
			m_vectResults[i].finished = true;
			m_vectResults[i].exitCode = exitCode;
			if (stdout_buf)
			{
				m_vectResults[i].stdoutStr = stdout_buf;
			}
			if (stderr_buf)
			{
				m_vectResults[i].stderrStr = stderr_buf;
			}
			if (error)
			{
				if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
				{
					m_vectResults[i].cancelled = true;
				}
				else if (m_vectResults[i].stderrStr.empty())
				{
					m_vectResults[i].stderrStr = error->message;
				}
				g_error_free(error);
				error = NULL;
			}
			if (exitCode != 0 && !m_vectResults[i].cancelled)
			{
				bHadError = true;
			}
		}

		if (stdout_buf) g_free(stdout_buf);
		if (stderr_buf) g_free(stderr_buf);
		g_object_unref(proc);

		if (ShouldCancel())
		{
			break;
		}
	}

	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_iCurrentCommand = m_vectCommands.size();
		if (ShouldCancel())
		{
			SetProgressText("Cancelled");
			SetMessage(MSG_TYPE_WARNING, "Task was cancelled");
		}
		else if (bHadError)
		{
			SetProgressText("Completed with errors");
			SetMessage(MSG_TYPE_ERROR, "External tool exited with errors");
		}
		else
		{
			SetProgressText("Completed successfully");
			SetMessage(MSG_TYPE_INFO, "Completed");
		}
	}
	EmitTaskProgressUpdatedEvent();
}
