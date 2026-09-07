#include <catch2/catch_test_macros.hpp>
#include "TaskManager.h"
#include "AbstractTask.h"
#include <string>
#include <chrono>
#include <thread>

class MockTask : public AbstractTask
{
public:
    int m_iterations = 10;
    int m_current = 0;
    bool m_wasRun = false;

    MockTask(int iterations = 10) : m_iterations(iterations) {}

    std::string GetDescription() const override { return "Mock Task"; }
    std::string GetIterationTypeName(bool, bool) const override { return "items"; }
    int GetTotalIterations() const override { return m_iterations; }
    int GetCurrentIteration() const override { return m_current; }
    double GetProgress() const override { return m_iterations > 0 ? (double)m_current / m_iterations : 0.0; }

    void Run() override {
        m_wasRun = true;
        while (m_current < m_iterations) {
            if (ShouldCancel())
                break;
            m_current++;
        }
    }
};

TEST_CASE("AbstractTask Lifecycle and Execution", "[unit][task][fast]")
{
    boost::shared_ptr<MockTask> task(new MockTask(10));

    SECTION("Initial task state")
    {
        REQUIRE_FALSE(task->IsStarted());
        REQUIRE_FALSE(task->IsFinished());
        REQUIRE_FALSE(task->IsRunning());
        REQUIRE_FALSE(task->IsPaused());
        REQUIRE(task->CanCancel());
        REQUIRE(task->GetProgress() == 0.0);
    }

    SECTION("Direct task execution")
    {
        task->RunTask();
        REQUIRE(task->m_wasRun);
        REQUIRE(task->IsFinished());
        REQUIRE(task->GetCurrentIteration() == 10);
        REQUIRE(task->GetProgress() == 1.0);
    }

    SECTION("Cancellation before execution")
    {
        task->Cancel();
        task->RunTask();
        // Cancelled before execution: Run() should not be called
        REQUIRE_FALSE(task->m_wasRun);
        REQUIRE(task->GetCurrentIteration() == 0);
        REQUIRE(task->IsFinished());
    }
}

TEST_CASE("TaskManager Thread Pool Execution", "[unit][task]")
{
    TaskManager::Reset();
    TaskManagerPtr tm = TaskManager::GetInstance();
    REQUIRE(tm != nullptr);

    boost::shared_ptr<MockTask> task(new MockTask(5));
    tm->AddTask(task);

    // Wait up to 1 second for thread pool execution
    for (int i = 0; i < 50; ++i) {
        if (task->IsFinished())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    REQUIRE(task->IsFinished());
    REQUIRE(task->m_wasRun);
    REQUIRE(task->GetCurrentIteration() == 5);

    TaskManager::Reset();
}

#include "ExternalToolTask.h"

TEST_CASE("ExternalToolTask Execution and Details", "[unit][task][external_tool]")
{
    SECTION("Successful execution with stdout")
    {
        std::vector<std::string> cmds = { "echo 'Hello from tool'" };
        boost::shared_ptr<ExternalToolTask> task(new ExternalToolTask("EchoTool", cmds));

        REQUIRE(task->GetDescription() == "External Tool: EchoTool");
        REQUIRE(task->GetTotalIterations() == 1);
        REQUIRE(task->HasDetails());
        REQUIRE(task->CanCancel());
        REQUIRE_FALSE(task->CanPause());

        task->RunTask();

        REQUIRE(task->IsFinished());
        REQUIRE(task->GetMessageType() == AbstractTask::MSG_TYPE_INFO);
        std::string details = task->GetDetails();
        REQUIRE(details.find("echo 'Hello from tool'") != std::string::npos);
        REQUIRE(details.find("Status: Completed (exit code 0)") != std::string::npos);
        REQUIRE(details.find("Hello from tool") != std::string::npos);
    }

    SECTION("Failed command with stderr")
    {
        std::vector<std::string> cmds = { "sh -c 'echo \"failed task error\" >&2; exit 42'" };
        boost::shared_ptr<ExternalToolTask> task(new ExternalToolTask("FailTool", cmds));

        task->RunTask();

        REQUIRE(task->IsFinished());
        REQUIRE(task->GetMessageType() == AbstractTask::MSG_TYPE_ERROR);
        std::string details = task->GetDetails();
        REQUIRE(details.find("Status: Failed (exit code 42)") != std::string::npos);
        REQUIRE(details.find("failed task error") != std::string::npos);
    }

    SECTION("Multiple commands in sequence")
    {
        std::vector<std::string> cmds = { "echo 'Step 1'", "echo 'Step 2'" };
        boost::shared_ptr<ExternalToolTask> task(new ExternalToolTask("MultiTool", cmds));

        REQUIRE(task->GetTotalIterations() == 2);

        task->RunTask();

        REQUIRE(task->IsFinished());
        REQUIRE(task->GetMessageType() == AbstractTask::MSG_TYPE_INFO);
        std::string details = task->GetDetails();
        REQUIRE(details.find("Step 1") != std::string::npos);
        REQUIRE(details.find("Step 2") != std::string::npos);
        REQUIRE(details.find("=== Command 1 of 2 ===") != std::string::npos);
        REQUIRE(details.find("=== Command 2 of 2 ===") != std::string::npos);
    }

    SECTION("Cancellation of external tool")
    {
        std::vector<std::string> cmds = { "sleep 5" };
        boost::shared_ptr<ExternalToolTask> task(new ExternalToolTask("SleepTool", cmds));

        TaskManager::Reset();
        TaskManagerPtr tm = TaskManager::GetInstance();
        tm->AddTask(task);

        // Wait a brief moment for the command to start running
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        task->Cancel();

        // Wait for cancellation to complete
        for (int i = 0; i < 50; ++i) {
            if (task->IsFinished())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        REQUIRE(task->IsFinished());
        TaskManager::Reset();
    }
}

