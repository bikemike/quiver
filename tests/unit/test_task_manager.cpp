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
