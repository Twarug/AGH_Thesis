#include "BenchmarkCase.h"
#include "VulkanBaseRenderer.h"
#include "SingleGPURenderer.h"
#include "SFRRenderer.h"
#include "AFRRenderer.h"

#include <filesystem>
#include <print>

Benchmark BenchmarkRunner::runRenderer(const BenchmarkCase& benchmarkCase,
                                       std::unique_ptr<VulkanBaseRenderer> renderer,
                                       const char* rendererName)
{
    std::println("\n----------------------------------------");
    std::println("Running: {}", rendererName);
    std::println("Case: {}", benchmarkCase.getName());
    std::println("----------------------------------------");

    size_t totalFrames = benchmarkCase.getFrameCount() + benchmarkCase.getWarmupFrames();
    renderer->enableBenchmark(totalFrames, benchmarkCase.getWarmupFrames());

    auto scene = benchmarkCase.createScene();
    if (!scene)
    {
        throw std::runtime_error("Failed to create scene for case: " + benchmarkCase.getName());
    }
    renderer->setScene(std::move(scene));

    renderer->getBenchmark()->setRendererName(rendererName);
    renderer->getBenchmark()->setSceneName(benchmarkCase.getName());

    renderer->run();

    return *renderer->getBenchmark();
}

BenchmarkSuite BenchmarkRunner::runCase(const BenchmarkCase& benchmarkCase)
{
    std::println("\n========================================");
    std::println("Benchmark Case: {}", benchmarkCase.getName());
    std::println("Description: {}", benchmarkCase.getDescription());
    std::println("Frames: {}", benchmarkCase.getFrameCount());
    std::println("Warmup: {}", benchmarkCase.getWarmupFrames());
    std::println("========================================");

    std::string caseOutputDir = config.outputDir + "/" + benchmarkCase.getName();
    std::filesystem::create_directories(caseOutputDir);

    BenchmarkSuite suite(caseOutputDir);

    size_t gpuCount = VulkanBaseRenderer::countAvailableGPUs();

    if (config.runSingleGPU && benchmarkCase.runSingleGPU())
    {
        std::println("Running SingleGPU benchmarks on {} GPU(s)", gpuCount);

        for (size_t gpuIndex = 0; gpuIndex < gpuCount; ++gpuIndex)
        {
            auto renderer = std::make_unique<VulkanSingleGPURenderer>(static_cast<int>(gpuIndex));
            std::string rendererName = "SingleGPU_" + std::to_string(gpuIndex);
            suite.addBenchmark(runRenderer(benchmarkCase, std::move(renderer), rendererName.c_str()));
        }
    }

    if (config.runSFR && benchmarkCase.runSFR() && gpuCount > 1)
    {
        auto renderer = std::make_unique<VulkanSFRRenderer>();
        suite.addBenchmark(runRenderer(benchmarkCase, std::move(renderer), "SFR"));
    }

    if (config.runAFR && benchmarkCase.runAFR() && gpuCount > 1)
    {
        auto renderer = std::make_unique<VulkanAFRRenderer>();
        suite.addBenchmark(runRenderer(benchmarkCase, std::move(renderer), "AFR"));
    }

    suite.printAllStats();

    if (!suite.exportAll())
    {
        throw std::runtime_error("Failed to export all cases");
    }

    return suite;
}

std::vector<BenchmarkSuite> BenchmarkRunner::runCases(const std::vector<std::string>& caseNames)
{
    std::vector<BenchmarkSuite> results;
    results.reserve(caseNames.size());

    auto& registry = BenchmarkCaseRegistry::instance();

    for (const auto& name : caseNames)
    {
        auto benchmarkCase = registry.createCase(name);
        if (!benchmarkCase)
        {
            std::println(stderr, "Unknown benchmark case: {}", name);
            continue;
        }

        results.push_back(runCase(*benchmarkCase));
    }

    return results;
}

std::vector<BenchmarkSuite> BenchmarkRunner::runAllCases()
{
    return runCases(BenchmarkCaseRegistry::instance().getCaseNames());
}
