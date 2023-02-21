#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <stdint.h>
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
#include <time.h>
#endif

// Helper class for animation and simulation timing.
class StepTimer
{
public:
    StepTimer() :
        m_elapsedTicks(0),
        m_totalTicks(0),
        m_leftOverTicks(0),
        m_frameCount(0),
        m_framesPerSecond(0),
        m_framesThisSecond(0),
        m_qpcSecondCounter(0),
        m_isFixedTimeStep(false),
        m_targetElapsedTicks(TicksPerSecond / 60)
    {
#if defined(_WIN32)
        QueryPerformanceFrequency(&m_qpcFrequency);
        QueryPerformanceCounter(&m_qpcLastTime);

        // Initialize max delta to 1/10 of a second.
        m_qpcMaxDelta = m_qpcFrequency.QuadPart / 10;
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        m_qpcFrequency = performanceFrequencyEX();
        m_qpcLastTime = performanceCounterEX();

        // Initialize max delta to 1/10 of a second.
        m_qpcMaxDelta = static_cast<uint64_t>(performanceFrequencyEX() / 10);
#endif
    }

    // Get elapsed time since the previous Update call.
    uint64_t GetElapsedTicks() const                        { return m_elapsedTicks; }
    double GetElapsedSeconds() const                    { return TicksToSeconds(m_elapsedTicks); }

    // Get total time since the start of the program.
    uint64_t GetTotalTicks() const                        { return m_totalTicks; }
    double GetTotalSeconds() const                        { return TicksToSeconds(m_totalTicks); }

    // Get total number of updates since start of the program.
    uint32_t GetFrameCount() const                        { return m_frameCount; }

    // Get the current framerate.
    uint32_t GetFramesPerSecond() const                    { return m_framesPerSecond; }

    // Set whether to use fixed or variable timestep mode.
    void SetFixedTimeStep(bool isFixedTimestep)            { m_isFixedTimeStep = isFixedTimestep; }

    // Set how often to call Update when in fixed timestep mode.
    void SetTargetElapsedTicks(uint64_t targetElapsed)    { m_targetElapsedTicks = targetElapsed; }
    void SetTargetElapsedSeconds(double targetElapsed)    { m_targetElapsedTicks = SecondsToTicks(targetElapsed); }

    // Integer format represents time using 10,000,000 ticks per second.
    static const uint64_t TicksPerSecond = 10000000;

    static double TicksToSeconds(uint64_t ticks)            { return static_cast<double>(ticks) / TicksPerSecond; }
    static uint64_t SecondsToTicks(double seconds)        { return static_cast<uint64_t>(seconds * TicksPerSecond); }

    // After an intentional timing discontinuity (for instance a blocking IO operation)
    // call this to avoid having the fixed timestep logic attempt a set of catch-up 
    // Update calls.

    void ResetElapsedTime()
    {
#if defined(_WIN32)
        QueryPerformanceCounter(&m_qpcLastTime);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        m_qpcLastTime = performanceCounterEX();
#endif

        m_leftOverTicks = 0;
        m_framesPerSecond = 0;
        m_framesThisSecond = 0;
        m_qpcSecondCounter = 0;
    }

    typedef void(*LPUPDATEFUNC) (void);

    // Update timer state, calling the specified Update function the appropriate number of times.
    void Tick(LPUPDATEFUNC update = nullptr)
    {
        // Query the current time.
#if defined(_WIN32)
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        unsigned long currentTime;
        currentTime = performanceCounterEX();
#endif

        uint64_t timeDelta;
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        timeDelta = static_cast<uint64_t>(currentTime - m_qpcLastTime);
        m_qpcLastTime = currentTime;
#elif defined(_WIN32)
        timeDelta = static_cast<uint64_t>(currentTime.QuadPart - m_qpcLastTime.QuadPart);
        m_qpcLastTime = currentTime;
#endif

        m_qpcSecondCounter += timeDelta;

        // Clamp excessively large time deltas (e.g. after paused in the debugger).
        if (timeDelta > m_qpcMaxDelta)
        {
            timeDelta = m_qpcMaxDelta;
        }

        // We now have the elapsed number of ticks, along with the number of ticks-per-second. 
        // We use these values to convert to the number of elapsed microseconds.
        // To guard against loss-of-precision, we convert to microseconds *before* dividing by ticks-per-second.
        // This cannot overflow due to the previous clamp.
        timeDelta *= TicksPerSecond;

#if defined(_WIN32)
        timeDelta /= static_cast<uint64_t>(m_qpcFrequency.QuadPart);
#elif defined (VK_USE_PLATFORM_XLIB_KHR)
        timeDelta /= static_cast<uint64_t>(m_qpcFrequency);
#endif

        uint32_t lastFrameCount = m_frameCount;

        if (m_isFixedTimeStep)
        {
            // Fixed timestep update logic

            // If the app is running very close to the target elapsed time (within 1/4 of a millisecond) just clamp
            // the clock to exactly match the target value. This prevents tiny and irrelevant errors
            // from accumulating over time. Without this clamping, a game that requested a 60 fps
            // fixed update, running with vsync enabled on a 59.94 NTSC display, would eventually
            // accumulate enough tiny errors that it would drop a frame. It is better to just round 
            // small deviations down to zero to leave things running smoothly.

            if (abs(static_cast<int>(timeDelta - m_targetElapsedTicks)) < TicksPerSecond / 4000)
            {
                timeDelta = m_targetElapsedTicks;
            }

            m_leftOverTicks += timeDelta;

            while (m_leftOverTicks >= m_targetElapsedTicks)
            {
                m_elapsedTicks = m_targetElapsedTicks;
                m_totalTicks += m_targetElapsedTicks;
                m_leftOverTicks -= m_targetElapsedTicks;
                m_frameCount++;

                if (update)
                {
                    update();
                }
            }
        }
        else
        {
            // Variable timestep update logic.
            m_elapsedTicks = timeDelta;
            m_totalTicks += timeDelta;
            m_leftOverTicks = 0;
            m_frameCount++;

            if (update)
            {
                update();
            }
        }

        // Track the current framerate.
        if (m_frameCount != lastFrameCount)
        {
            m_framesThisSecond++;
        }

#if defined(_WIN32)
        if (m_qpcSecondCounter >= static_cast<uint64_t>(m_qpcFrequency.QuadPart))
        {
            m_framesPerSecond = m_framesThisSecond;
            m_framesThisSecond = 0;
            m_qpcSecondCounter %= static_cast<uint64_t>(m_qpcFrequency.QuadPart);
        }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        if (m_qpcSecondCounter >= static_cast<uint64_t>(m_qpcFrequency))
        {
            m_framesPerSecond = m_framesThisSecond;
            m_framesThisSecond = 0;
            m_qpcSecondCounter %= static_cast<uint64_t>(m_qpcFrequency);
        }
#endif
    }

private:
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    // Return the nanoseconds since some unspecified starting point
    uint64_t performanceCounterEX()
    {
        uint64_t result = 0;
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        result = (uint64_t)ts.tv_sec * 1000000000LL + (uint64_t)ts.tv_nsec;
        return result;
    }

    // Return 10^9 since the unit of the couter is the nanosecond, and we have
    // 10^9 ns in a second.
    uint64_t performanceFrequencyEX()
    {
        uint64_t result = 1;
        result = 1000000000LL;
        return result;
    }
#endif

    // Source timing data uses QPC units.
#if defined(_WIN32)
    LARGE_INTEGER m_qpcFrequency;
    LARGE_INTEGER m_qpcLastTime;
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        // Source timing data uses QPC units.
    unsigned long  m_qpcFrequency;
    unsigned long  m_qpcLastTime;
#endif
    uint64_t m_qpcMaxDelta;

    // Derived timing data uses a canonical tick format.
    uint64_t m_elapsedTicks;
    uint64_t m_totalTicks;
    uint64_t m_leftOverTicks;

    // Members for tracking the framerate.
    uint32_t m_frameCount;
    uint32_t m_framesPerSecond;
    uint32_t m_framesThisSecond;
    uint64_t m_qpcSecondCounter;

    // Members for configuring fixed timestep mode.
    bool m_isFixedTimeStep;
    uint64_t m_targetElapsedTicks;
};
