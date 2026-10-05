#include "ExternalToolTask.h"
#include <sstream>
#include <thread>
#include <regex>
#include <algorithm>
#include <cctype>
#include <boost/algorithm/string.hpp>

ExternalToolTask::ExternalToolTask(const std::string& strToolName, const std::vector<std::string>& vectCommands, bool bShowOnlyOnError)
	: m_strToolName(strToolName),
	  m_vectCommands(vectCommands),
	  m_iCurrentCommand(0),
	  m_bShowOnlyOnError(bShowOnlyOnError),
	  m_bHidden(bShowOnlyOnError),
	  m_dCurrentCmdProgress(0.0),
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
	if (m_vectCommands.empty() || IsFinished())
	{
		return 1.0;
	}
	double total = static_cast<double>(m_vectCommands.size());
	double current = static_cast<double>(m_iCurrentCommand) + m_dCurrentCmdProgress;
	return std::clamp(current / total, 0.0, 1.0);
}

bool ExternalToolTask::IsHidden() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_bHidden;
}

void ExternalToolTask::SetHidden(bool bHidden)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_bHidden = bHidden;
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

void ExternalToolTask::ParseOutputLine(const std::string& line, bool isStderr)
{
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (isStderr)
		{
			m_vectResults[m_iCurrentCommand].stderrStr += line + "\n";
		}
		else
		{
			m_vectResults[m_iCurrentCommand].stdoutStr += line + "\n";

			std::string trimmed = boost::algorithm::trim_copy(line);
			if (!trimmed.empty())
			{
				// 1. Zenity status message: line starts with '#'
				if (trimmed.front() == '#')
				{
					std::string msg = trimmed.substr(1);
					boost::algorithm::trim(msg);
					if (!msg.empty())
					{
						SetProgressText(msg);
					}
				}
				// 2. Prefix directives: "PROGRESS: <num>" or "PROGRESS: <num> | <status>"
				else if (boost::algorithm::istarts_with(trimmed, "PROGRESS:"))
				{
					std::string rest = trimmed.substr(9);
					size_t barPos = rest.find('|');
					std::string numStr = (barPos != std::string::npos) ? rest.substr(0, barPos) : rest;
					boost::algorithm::trim(numStr);
					if (!numStr.empty() && numStr.back() == '%')
					{
						numStr.pop_back();
					}
					boost::algorithm::trim(numStr);
					try
					{
						double val = std::stod(numStr);
						if (val > 1.0)
						{
							val /= 100.0;
						}
						m_dCurrentCmdProgress = std::clamp(val, 0.0, 1.0);
					}
					catch (...) {}

					if (barPos != std::string::npos)
					{
						std::string msg = rest.substr(barPos + 1);
						boost::algorithm::trim(msg);
						if (!msg.empty())
						{
							SetProgressText(msg);
						}
					}
				}
				// 3. Prefix directive: "STATUS: <message>"
				else if (boost::algorithm::istarts_with(trimmed, "STATUS:"))
				{
					std::string msg = trimmed.substr(7);
					boost::algorithm::trim(msg);
					if (!msg.empty())
					{
						SetProgressText(msg);
					}
				}
				// 4. Zenity pure progress number: line contains only digits (0-100), optional trailing '%'
				else
				{
					bool isPureZenityNumber = false;
					std::string numStr = trimmed;
					if (!numStr.empty() && numStr.back() == '%')
					{
						numStr.pop_back();
						boost::algorithm::trim(numStr);
					}
					if (!numStr.empty() && std::all_of(numStr.begin(), numStr.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; }))
					{
						try
						{
							double val = std::stod(numStr);
							if (val >= 0.0 && val <= 100.0)
							{
								val /= 100.0;
								m_dCurrentCmdProgress = std::clamp(val, 0.0, 1.0);
								isPureZenityNumber = true;
							}
						}
						catch (...) {}
					}

					// 5. Generic CLI percentage heuristic: e.g. "[ 42%]" or "42% completed"
					if (!isPureZenityNumber)
					{
						static const std::regex pctRegex(R"((\d{1,3}(?:\.\d+)?)%)");
						std::smatch match;
						if (std::regex_search(trimmed, match, pctRegex))
						{
							try
							{
								double val = std::stod(match[1].str()) / 100.0;
								m_dCurrentCmdProgress = std::clamp(val, 0.0, 1.0);
							}
							catch (...) {}
						}
					}
				}
			}
		}
	}

	EmitTaskProgressUpdatedEvent();
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
			m_dCurrentCmdProgress = 0.0;
			m_vectResults[i].running = true;
			SetProgressText("Running: " + cmd);
		}
		EmitTaskProgressUpdatedEvent();

		const char* argv[] = { "/bin/sh", "-c", cmd.c_str(), NULL };
		GError* error = NULL;
		GSubprocessLauncher* launcher = g_subprocess_launcher_new(
			static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE));
		g_subprocess_launcher_setenv(launcher, "PYTHONUNBUFFERED", "1", TRUE);
		GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, argv, &error);
		g_object_unref(launcher);

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
			m_bHidden = false; // Unhide so Task Manager reveals failure
			SetMessage(MSG_TYPE_ERROR, "Failed to spawn command");
			SetProgressText("Failed to spawn: " + cmd);
			EmitTaskProgressUpdatedEvent();
			continue;
		}

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_pCurrentProc = proc;
		}

		GInputStream* stdout_stream = g_subprocess_get_stdout_pipe(proc);
		GInputStream* stderr_stream = g_subprocess_get_stderr_pipe(proc);
		GDataInputStream* data_stdout = g_data_input_stream_new(stdout_stream);
		GDataInputStream* data_stderr = g_data_input_stream_new(stderr_stream);

		std::thread stdoutThread([this, data_stdout]() {
			gsize len = 0;
			GError* err = NULL;
			while (true)
			{
				gchar* line = g_data_input_stream_read_line(data_stdout, &len, m_pCancellable, &err);
				if (!line)
				{
					break;
				}
				std::string str(line, len);
				g_free(line);
				ParseOutputLine(str, false);
			}
			if (err)
			{
				g_error_free(err);
			}
			g_object_unref(data_stdout);
		});

		std::thread stderrThread([this, data_stderr]() {
			gsize len = 0;
			GError* err = NULL;
			while (true)
			{
				gchar* line = g_data_input_stream_read_line(data_stderr, &len, m_pCancellable, &err);
				if (!line)
				{
					break;
				}
				std::string str(line, len);
				g_free(line);
				ParseOutputLine(str, true);
			}
			if (err)
			{
				g_error_free(err);
			}
			g_object_unref(data_stderr);
		});

		g_subprocess_wait(proc, m_pCancellable, &error);
		stdoutThread.join();
		stderrThread.join();

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
			m_dCurrentCmdProgress = 1.0;

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
				m_bHidden = false; // Unhide so Task Manager reveals failure
			}
		}

		g_object_unref(proc);

		if (ShouldCancel())
		{
			break;
		}
	}

	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_iCurrentCommand = m_vectCommands.size();
		m_dCurrentCmdProgress = 0.0;
		if (ShouldCancel())
		{
			SetProgressText("Cancelled");
			SetMessage(MSG_TYPE_WARNING, "Task was cancelled");
		}
		else if (bHadError)
		{
			m_bHidden = false;
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
