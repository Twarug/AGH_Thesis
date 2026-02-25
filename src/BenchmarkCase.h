#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <print>
#include <map>
#include <ranges>

#include "Scene.h"
#include "Benchmark.h"

// Forward declarations
class VulkanBaseRenderer;

// ============================================================================
// Benchmark Case - Abstract base for defining benchmark scenarios
// ============================================================================

class BenchmarkCase
{
public:
    virtual ~BenchmarkCase() = default;

    // Override to return case name (used in output files)
    virtual std::string getName() const = 0;

    // Override to return case description
    virtual std::string getDescription() const = 0;

    // Override to create the scene for this benchmark
    virtual std::unique_ptr<Scene> createScene() const = 0;

    // Configuration (can be overridden)
    virtual size_t getFrameCount() const { return 10'000; }
    virtual size_t getWarmupFrames() const { return 100; }

    // Which renderers to run (can be overridden to skip some)
    virtual bool runSingleGPU() const { return true; }
    virtual bool runSFR() const { return true; }
    virtual bool runAFR() const { return true; }
};

// ============================================================================
// Benchmark Case Registry - Collects and runs benchmark cases
// ============================================================================

class BenchmarkCaseRegistry
{
public:
    using CaseFactory = std::function<std::unique_ptr<BenchmarkCase>()>;

    static BenchmarkCaseRegistry& instance()
    {
        static BenchmarkCaseRegistry registry;
        return registry;
    }

    // Register a benchmark case factory
    void registerCase(const std::string& name, CaseFactory factory)
    {
        cases[name] = std::move(factory);
    }

    // Get all registered case names
    std::vector<std::string> getCaseNames() const
    {
        std::vector<std::string> names;
        names.reserve(cases.size());
        for (const auto& name : cases | std::views::keys)
        {
            names.push_back(name);
        }
        return names;
    }

    // Create a case by name
    std::unique_ptr<BenchmarkCase> createCase(const std::string& name) const
    {
        auto it = cases.find(name);
        if (it == cases.end())
        {
            return nullptr;
        }
        return it->second();
    }

    // Check if a case exists
    bool hasCase(const std::string& name) const
    {
        return cases.find(name) != cases.end();
    }

    // Print all available cases
    void printAvailableCases() const
    {
        std::println("Available benchmark cases:");
        for (const auto& [name, factory] : cases)
        {
            auto benchmarkCase = factory();
            std::println("  {} - {}", name, benchmarkCase->getDescription());
        }
    }

private:
    BenchmarkCaseRegistry() = default;
    std::map<std::string, CaseFactory> cases;
};

// ============================================================================
// Registration Helper Macro
// ============================================================================

#define REGISTER_BENCHMARK_CASE(CaseClass) \
    namespace { \
        struct CaseClass##Registrar { \
            CaseClass##Registrar() { \
                BenchmarkCaseRegistry::instance().registerCase( \
                    #CaseClass, \
                    []() { return std::make_unique<CaseClass>(); } \
                ); \
            } \
        }; \
        static CaseClass##Registrar g_##CaseClass##Registrar; \
    }

// ============================================================================
// Benchmark Runner - Executes benchmark cases
// ============================================================================

class BenchmarkRunner
{
public:
    struct Config
    {
        std::string outputDir = "benchmarks";
        bool runSingleGPU = true;
        bool runSFR = true;
        bool runAFR = true;
    };

    explicit BenchmarkRunner(Config config = {})
        : config(std::move(config))
    {}

    // Run a single benchmark case, returns collected benchmarks
    BenchmarkSuite runCase(const BenchmarkCase& benchmarkCase);

    // Run multiple cases by name
    std::vector<BenchmarkSuite> runCases(const std::vector<std::string>& caseNames);

    // Run all registered cases
    std::vector<BenchmarkSuite> runAllCases();

    [[nodiscard]] const Config& getConfig() const { return config; }

private:
    Config config;

    // Internal helper to run a renderer
    Benchmark runRenderer(const BenchmarkCase& benchmarkCase,
                          std::unique_ptr<VulkanBaseRenderer> renderer,
                          const char* rendererName);
};
