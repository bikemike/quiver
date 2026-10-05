#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ExternalToolTask.h"
#include "TaskManager.h"
#include "ITaskEventHandler.h"
#include <boost/make_shared.hpp>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

using Catch::Matchers::WithinAbs;

TEST_CASE("ExternalToolTask Line Parsing", "[unit][tools][task][fast]")
{
    std::vector<std::string> cmds = { "dummy" };
    ExternalToolTaskPtr task(new ExternalToolTask("TestTool", cmds, false));

    SECTION("Zenity progress and status parsing")
    {
        task->ParseOutputLine("25", false);
        REQUIRE_THAT(task->GetProgress(), WithinAbs(0.25, 0.001));

        task->ParseOutputLine("50%", false);
        REQUIRE_THAT(task->GetProgress(), WithinAbs(0.50, 0.001));

        task->ParseOutputLine("# Converting image 3 of 10", false);
        REQUIRE(task->GetProgressText() == "Converting image 3 of 10");

        task->ParseOutputLine("100", false);
        REQUIRE_THAT(task->GetProgress(), WithinAbs(1.0, 0.001));
    }

    SECTION("Prefix directives parsing")
    {
        task->ParseOutputLine("PROGRESS: 35", false);
        REQUIRE_THAT(task->GetProgress(), WithinAbs(0.35, 0.001));

        task->ParseOutputLine("PROGRESS: 70%", false);
        REQUIRE_THAT(task->GetProgress(), WithinAbs(0.70, 0.001));

        task->ParseOutputLine("STATUS: Resizing photo", false);
        REQUIRE(task->GetProgressText() == "Resizing photo");

        task->ParseOutputLine("PROGRESS: 85 | Almost finished", false);
        REQUIRE_THAT(task->GetProgress(), WithinAbs(0.85, 0.001));
        REQUIRE(task->GetProgressText() == "Almost finished");
    }

    SECTION("Generic CLI percentage heuristic")
    {
        task->ParseOutputLine("Transcoding frame 120 [ 42%] speed=1.5x", false);
        REQUIRE_THAT(task->GetProgress(), WithinAbs(0.42, 0.001));

        task->ParseOutputLine("Download: 95% completed", false);
        REQUIRE_THAT(task->GetProgress(), WithinAbs(0.95, 0.001));
    }

    SECTION("Stderr streaming into details")
    {
        task->ParseOutputLine("warning: deprecated feature used", true);
        std::string details = task->GetDetails();
        REQUIRE(details.find("warning: deprecated feature used") != std::string::npos);
    }
}

TEST_CASE("ExternalToolTask Multi-command Progress Weighting", "[unit][tools][task][fast]")
{
    std::vector<std::string> cmds = { "cmd1", "cmd2" };
    ExternalToolTaskPtr task(new ExternalToolTask("BatchTool", cmds, false));

    // Initial state: command 0, 0 progress
    REQUIRE_THAT(task->GetProgress(), WithinAbs(0.0, 0.001));

    // Command 1 at 50% -> overall progress = (0 + 0.5) / 2 = 0.25
    task->ParseOutputLine("PROGRESS: 50", false);
    REQUIRE_THAT(task->GetProgress(), WithinAbs(0.25, 0.001));
}

TEST_CASE("ExternalToolTask Execution and Hidden State", "[unit][tools][task][fast]")
{
    SECTION("Successful command with show_only_on_error stays hidden")
    {
        std::vector<std::string> cmds = { "echo 'Hello from external tool'" };
        ExternalToolTaskPtr task(new ExternalToolTask("EchoTool", cmds, true)); // bShowOnlyOnError = true

        REQUIRE(task->IsHidden() == true);

        task->RunTask();

        REQUIRE(task->IsFinished() == true);
        REQUIRE(task->IsHidden() == true); // Remains hidden because exit code was 0

        std::string details = task->GetDetails();
        REQUIRE(details.find("Hello from external tool") != std::string::npos);
        REQUIRE(details.find("Status: Completed (exit code 0)") != std::string::npos);
    }

    SECTION("Failing command with show_only_on_error unhides on error")
    {
        std::vector<std::string> cmds = { "sh -c 'echo \"Fatal error\" >&2; exit 42'" };
        ExternalToolTaskPtr task(new ExternalToolTask("FailTool", cmds, true)); // bShowOnlyOnError = true

        REQUIRE(task->IsHidden() == true);

        task->RunTask();

        REQUIRE(task->IsFinished() == true);
        REQUIRE(task->IsHidden() == false); // Unhides on error!

        std::string details = task->GetDetails();
        REQUIRE(details.find("Fatal error") != std::string::npos);
        REQUIRE(details.find("exit code 42") != std::string::npos);
    }
}

TEST_CASE("ExternalToolTask Live Streaming Script Execution", "[unit][tools][task][fast]")
{
    std::vector<std::string> cmds = {
        "sh -c 'echo \"PROGRESS: 50\"; echo \"# Testing status\"; echo \"Output line\"'"
    };
    ExternalToolTaskPtr task(new ExternalToolTask("ScriptTool", cmds, false));

    task->RunTask();

    REQUIRE(task->IsFinished() == true);
    REQUIRE_THAT(task->GetProgress(), WithinAbs(1.0, 0.001)); // Finished -> 1.0

    std::string details = task->GetDetails();
    REQUIRE(details.find("Output line") != std::string::npos);
    REQUIRE(details.find("Status: Completed (exit code 0)") != std::string::npos);
}

class DummyTaskHandler : public ITaskEventHandler
{
public:
    void HandleTaskStarted(TaskEventPtr) override {}
    void HandleTaskResumed(TaskEventPtr) override {}
    void HandleTaskMessage(TaskEventPtr) override {}
    void HandleTaskPaused(TaskEventPtr) override {}
    void HandleTaskUnpaused(TaskEventPtr) override {}
    void HandleTaskFinished(TaskEventPtr) override {}
    void HandleTaskCancelled(TaskEventPtr) override {}
    void HandleTaskProgressUpdated(TaskEventPtr) override {}
};

TEST_CASE("AbstractEventSource Thread-Safety Stress", "[unit][tools][task][fast]")
{
    std::vector<std::string> cmds = { "true" };
    ExternalToolTaskPtr task(new ExternalToolTask("StressTool", cmds, false));

    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([task, &stop]() {
            std::vector<boost::shared_ptr<DummyTaskHandler>> handlers;
            for (int i = 0; i < 10; ++i)
            {
                handlers.push_back(boost::make_shared<DummyTaskHandler>());
            }
            while (!stop.load())
            {
                for (auto& h : handlers)
                {
                    task->AddEventHandler(h);
                }
                for (auto& h : handlers)
                {
                    task->RemoveEventHandler(h);
                }
            }
        });
    }

    for (int i = 0; i < 100; ++i)
    {
        task->ParseOutputLine(std::to_string(i % 100), false);
    }
    stop.store(true);

    for (auto& th : threads)
    {
        th.join();
    }
}

TEST_CASE("ExternalToolTask Concurrent Execution via TaskManager", "[unit][tools][task][fast]")
{
    std::vector<std::string> cmds = { "true" };
    ExternalToolTaskPtr task(new ExternalToolTask("QuietTool", cmds, true));

    auto taskMgr = TaskManager::GetInstance();
    taskMgr->AddTask(task);

    int maxWaitMs = 2000;
    while (!task->IsFinished() && maxWaitMs > 0)
    {
        g_usleep(10000);
        maxWaitMs -= 10;
    }

    REQUIRE(task->IsFinished() == true);
    REQUIRE(task->IsHidden() == true);
}
