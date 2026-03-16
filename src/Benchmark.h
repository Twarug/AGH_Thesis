#pragma once

#include <chrono>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <format>
#include <print>

class Benchmark
{
public:
    struct Stats
    {
        double minMs;
        double maxMs;
        double avgMs;
        double medianMs;
        double stdDevMs;
        double percentile1Ms;
        double percentile99Ms;
        double avgFps;
        size_t frameCount;
    };

    struct SyncStats
    {
        double avgFenceWaitMs;
        double maxFenceWaitMs;
        double avgMemcpyMs;
        double maxMemcpyMs;
        double avgTotalSyncMs;
        double syncOverheadPercent;
        size_t frameCount;
    };

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    std::string rendererName;
    std::string sceneName;
    std::vector<double> frameTimes;
    TimePoint frameStart;
    TimePoint benchmarkStart;
    size_t warmupFrames;
    bool isWarmedUp = false;
    size_t framesRecorded = 0;

    std::vector<double> fenceWaitTimes;
    std::vector<double> memcpyTimes;
    double currentFrameFenceWait = 0.0;
    double currentFrameMemcpy = 0.0;

public:
    explicit Benchmark(std::string renderer = "Unknown", std::string scene = "Unknown", size_t warmup = 100)
        : rendererName(std::move(renderer))
          , sceneName(std::move(scene))
          , warmupFrames(warmup)
    {
        frameTimes.reserve(10000);
    }

    void setRendererName(const std::string& name) { rendererName = name; }
    void setSceneName(const std::string& name) { sceneName = name; }

    void startFrame()
    {
        frameStart = Clock::now();
        currentFrameFenceWait = 0.0;
        currentFrameMemcpy = 0.0;
        if (!isWarmedUp && framesRecorded == 0)
        {
            benchmarkStart = frameStart;
        }
    }

    void endFrame()
    {
        auto frameEnd = Clock::now();
        double frameTimeMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();

        framesRecorded++;

        if (!isWarmedUp)
        {
            if (framesRecorded >= warmupFrames)
            {
                isWarmedUp = true;
                benchmarkStart = Clock::now();
            }
            return;
        }

        frameTimes.push_back(frameTimeMs);
        fenceWaitTimes.push_back(currentFrameFenceWait);
        memcpyTimes.push_back(currentFrameMemcpy);
    }

    void addFenceWaitTime(double ms) { currentFrameFenceWait += ms; }
    void addMemcpyTime(double ms) { currentFrameMemcpy += ms; }

    static double measureMs(const TimePoint& start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    TimePoint now() const { return Clock::now(); }

    bool hasData() const { return !frameTimes.empty(); }
    size_t getFrameCount() const { return frameTimes.size(); }
    size_t getTotalFrames() const { return framesRecorded; }
    bool hasSyncData() const { return !fenceWaitTimes.empty(); }

    double getTotalBenchmarkTimeSeconds() const
    {
        if (frameTimes.empty()) return 0.0;
        return std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0) / 1000.0;
    }

    SyncStats calculateSyncStats() const
    {
        SyncStats stats{};
        if (fenceWaitTimes.empty() || frameTimes.empty())
        {
            return stats;
        }

        stats.frameCount = fenceWaitTimes.size();

        stats.avgFenceWaitMs = std::accumulate(fenceWaitTimes.begin(), fenceWaitTimes.end(), 0.0) / fenceWaitTimes.
            size();
        stats.maxFenceWaitMs = *std::max_element(fenceWaitTimes.begin(), fenceWaitTimes.end());

        stats.avgMemcpyMs = std::accumulate(memcpyTimes.begin(), memcpyTimes.end(), 0.0) / memcpyTimes.size();
        stats.maxMemcpyMs = *std::max_element(memcpyTimes.begin(), memcpyTimes.end());

        stats.avgTotalSyncMs = stats.avgFenceWaitMs + stats.avgMemcpyMs;

        double avgFrameTime = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0) / frameTimes.size();
        stats.syncOverheadPercent = (stats.avgTotalSyncMs / avgFrameTime) * 100.0;

        return stats;
    }

    Stats calculateStats() const
    {
        Stats stats{};
        if (frameTimes.empty())
        {
            return stats;
        }

        stats.frameCount = frameTimes.size();

        std::vector<double> sorted = frameTimes;
        std::sort(sorted.begin(), sorted.end());

        stats.minMs = sorted.front();
        stats.maxMs = sorted.back();
        stats.avgMs = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
        stats.avgFps = 1000.0 / stats.avgMs;

        size_t mid = sorted.size() / 2;
        if (sorted.size() % 2 == 0)
        {
            stats.medianMs = (sorted[mid - 1] + sorted[mid]) / 2.0;
        }
        else
        {
            stats.medianMs = sorted[mid];
        }

        double sumSquaredDiff = 0.0;
        for (double t : sorted)
        {
            double diff = t - stats.avgMs;
            sumSquaredDiff += diff * diff;
        }
        stats.stdDevMs = std::sqrt(sumSquaredDiff / sorted.size());

        stats.percentile1Ms = sorted[static_cast<size_t>(sorted.size() * 0.01)];
        stats.percentile99Ms = sorted[static_cast<size_t>(sorted.size() * 0.99)];

        return stats;
    }

    void printStats() const
    {
        if (frameTimes.empty())
        {
            std::println("No benchmark data recorded for {}", rendererName);
            return;
        }

        Stats stats = calculateStats();

        std::println("\n--- Benchmark Results: {} ---", rendererName);
        std::println("Scene: {}", sceneName);
        std::println("Frames recorded: {} (+{} warmup)", stats.frameCount, warmupFrames);
        std::println("Frame time (ms):");
        std::println("  Min:    {:.3f}", stats.minMs);
        std::println("  Max:    {:.3f}", stats.maxMs);
        std::println("  Avg:    {:.3f}", stats.avgMs);
        std::println("  Median: {:.3f}", stats.medianMs);
        std::println("  StdDev: {:.3f}", stats.stdDevMs);
        std::println("  1%:     {:.3f}", stats.percentile1Ms);
        std::println("  99%:    {:.3f}", stats.percentile99Ms);
        std::println("Average FPS: {:.1f}", stats.avgFps);
        std::println("Total time: {:.1f}s", getTotalBenchmarkTimeSeconds());

        if (hasSyncData())
        {
            SyncStats syncStats = calculateSyncStats();
            std::println("GPU Synchronization Overhead:");
            std::println("  Fence wait (avg): {:.3f} ms", syncStats.avgFenceWaitMs);
            std::println("  Fence wait (max): {:.3f} ms", syncStats.maxFenceWaitMs);
            if (syncStats.avgMemcpyMs > 0.001)
            {
                std::println("  Memcpy (avg):     {:.3f} ms", syncStats.avgMemcpyMs);
                std::println("  Memcpy (max):     {:.3f} ms", syncStats.maxMemcpyMs);
            }
            std::println("  Total sync (avg): {:.3f} ms", syncStats.avgTotalSyncMs);
            std::println("  Sync overhead:    {:.1f}% of frame time", syncStats.syncOverheadPercent);
        }
    }

    bool exportFrameTimes(const std::string& filename) const
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            std::println(stderr, "Failed to open file for writing: {}", filename);
            return false;
        }

        file << "frame;frame_time_ms;fps;fence_wait_ms;memcpy_ms;total_sync_ms\n";
        for (size_t i = 0; i < frameTimes.size(); i++)
        {
            double fenceWait = (i < fenceWaitTimes.size()) ? fenceWaitTimes[i] : 0.0;
            double memcpyTime = (i < memcpyTimes.size()) ? memcpyTimes[i] : 0.0;
            auto toComma = [](std::string s) { std::ranges::replace(s, '.', ','); return s; };
            file << std::format("{};{};{};{};{};{}\n",
                                i,
                                toComma(std::format("{:.4f}", frameTimes[i])),
                                toComma(std::format("{:.2f}", 1000.0 / frameTimes[i])),
                                toComma(std::format("{:.4f}", fenceWait)),
                                toComma(std::format("{:.4f}", memcpyTime)),
                                toComma(std::format("{:.4f}", fenceWait + memcpyTime)));
        }

        std::println("Exported frame times to: {}", filename);
        return true;
    }

    bool exportStats(const std::string& filename, bool append = false) const
    {
        std::ofstream file(filename, append ? std::ios::app : std::ios::out);
        if (!file.is_open())
        {
            std::println(stderr, "Failed to open file for writing: {}", filename);
            return false;
        }

        // Write header if not appending or file is empty
        if (!append)
        {
            file << "renderer;scene;frames;min_ms;max_ms;avg_ms;median_ms;stddev_ms;p1_ms;p99_ms;avg_fps;"
                << "sync_fence_avg_ms;sync_fence_max_ms;sync_memcpy_avg_ms;sync_memcpy_max_ms;sync_total_avg_ms;sync_overhead_pct\n";
        }

        Stats stats = calculateStats();
        SyncStats syncStats = calculateSyncStats();

        auto toComma = [](std::string s) { std::ranges::replace(s, '.', ','); return s; };
        file << std::format("{};{};{};{};{};{};{};{};{};{};{};{};{};{};{};{};{}\n",
                            rendererName, sceneName, stats.frameCount,
                            toComma(std::format("{:.4f}", stats.minMs)),
                            toComma(std::format("{:.4f}", stats.maxMs)),
                            toComma(std::format("{:.4f}", stats.avgMs)),
                            toComma(std::format("{:.4f}", stats.medianMs)),
                            toComma(std::format("{:.4f}", stats.stdDevMs)),
                            toComma(std::format("{:.4f}", stats.percentile1Ms)),
                            toComma(std::format("{:.4f}", stats.percentile99Ms)),
                            toComma(std::format("{:.2f}", stats.avgFps)),
                            toComma(std::format("{:.4f}", syncStats.avgFenceWaitMs)),
                            toComma(std::format("{:.4f}", syncStats.maxFenceWaitMs)),
                            toComma(std::format("{:.4f}", syncStats.avgMemcpyMs)),
                            toComma(std::format("{:.4f}", syncStats.maxMemcpyMs)),
                            toComma(std::format("{:.4f}", syncStats.avgTotalSyncMs)),
                            toComma(std::format("{:.2f}", syncStats.syncOverheadPercent)));

        return true;
    }

    const std::vector<double>& getFrameTimes() const { return frameTimes; }
    const std::string& getRendererName() const { return rendererName; }
    const std::string& getSceneName() const { return sceneName; }

    void reset()
    {
        frameTimes.clear();
        fenceWaitTimes.clear();
        memcpyTimes.clear();
        currentFrameFenceWait = 0.0;
        currentFrameMemcpy = 0.0;
        isWarmedUp = false;
        framesRecorded = 0;
    }

    const std::vector<double>& getFenceWaitTimes() const { return fenceWaitTimes; }
    const std::vector<double>& getMemcpyTimes() const { return memcpyTimes; }
};

class BenchmarkSuite
{
    std::vector<Benchmark> benchmarks;
    std::string outputDir;

public:
    explicit BenchmarkSuite(std::string dir = "benchmarks")
        : outputDir(std::move(dir))
    {
    }

    void addBenchmark(Benchmark benchmark)
    {
        benchmarks.push_back(std::move(benchmark));
    }

    void printAllStats() const
    {
        std::println("\n========================================");
        std::println("         BENCHMARK SUMMARY");
        std::println("========================================");

        for (const auto& b : benchmarks)
        {
            b.printStats();
        }

        if (benchmarks.size() > 1)
        {
            printComparisonTable();
        }
    }

    void printComparisonTable() const
    {
        std::println("\n--- Performance Comparison ---");
        std::println("{:<15}{:>12}{:>12}{:>12}{:>12}{:>14}{:>10}",
                     "Renderer", "Avg (ms)", "99% (ms)", "FPS", "vs Base", "Sync (ms)", "Sync %");
        std::println("{:-<87}", "");

        double baseFps = 0.0;
        for (size_t i = 0; i < benchmarks.size(); i++)
        {
            auto stats = benchmarks[i].calculateStats();
            auto syncStats = benchmarks[i].calculateSyncStats();
            if (i == 0) baseFps = stats.avgFps;

            double speedup = baseFps > 0 ? stats.avgFps / baseFps : 0.0;

            std::println("{:<15}{:>12.3f}{:>12.3f}{:>12.1f}{:>11.2f}x{:>14.3f}{:>9.1f}%",
                         benchmarks[i].getRendererName(),
                         stats.avgMs, stats.percentile99Ms, stats.avgFps, speedup,
                         syncStats.avgTotalSyncMs, syncStats.syncOverheadPercent);
        }
    }

    bool exportAll() const
    {
        bool success = true;

        for (const auto& b : benchmarks)
        {
            std::string filename = outputDir + "/" + b.getRendererName() + "_" + b.getSceneName() + "_frames.csv";
            success &= b.exportFrameTimes(filename);
        }

        std::string summaryFile = outputDir + "/benchmark_summary.csv";
        bool first = true;
        for (const auto& b : benchmarks)
        {
            success &= b.exportStats(summaryFile, !first);
            first = false;
        }

        if (success)
        {
            std::println("\nExported benchmark summary to: {}", summaryFile);
        }

        return success;
    }

    const std::vector<Benchmark>& getBenchmarks() const { return benchmarks; }
};
