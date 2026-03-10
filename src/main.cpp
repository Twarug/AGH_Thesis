#include "BenchmarkCase.h"

#include <filesystem>
#include <print>

struct Config
{
    std::string outputDir = "benchmarks";
    std::vector<std::string> casesToRun;
    bool runAll = false;
    bool listCases = false;
    bool runSingleGPU = true;
    bool runSFR = true;
    bool runAFR = true;
};

void printUsage(const char* programName)
{
    std::println("Usage: {} [options] [case1 case2 ...]", programName);
    std::println("\nOptions:");
    std::println("  --list              List available benchmark cases");
    std::println("  --all               Run all registered benchmark cases");
    std::println("  --output <dir>      Output directory for results (default: benchmarks)");
    std::println("  --only <renderer>   Run only specified renderer (single, sfr, afr)");
    std::println("  --help              Show this help message");
    std::println("\nExamples:");
    std::println("  {} --list", programName);
    std::println("  {} --all", programName);
    std::println("  {} CaseA CaseB", programName);
    std::println("  {} --only single CaseA", programName);
}

Config parseArgs(int argc, char* argv[])
{
    Config config;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            printUsage(argv[0]);
            exit(0);
        }

        if (arg == "--list" || arg == "-l")
        {
            config.listCases = true;
        }
        else if (arg == "--all" || arg == "-a")
        {
            config.runAll = true;
        }
        else if (arg == "--output" && i + 1 < argc)
        {
            config.outputDir = argv[++i];
        }
        else if (arg == "--only" && i + 1 < argc)
        {
            std::string renderer = argv[++i];
            config.runSingleGPU = (renderer == "single");
            config.runSFR = (renderer == "sfr");
            config.runAFR = (renderer == "afr");
        }
        else if (arg[0] != '-')
        {
            config.casesToRun.push_back(arg);
        }
    }

    return config;
}

int main(int argc, char* argv[])
{
    Config config = parseArgs(argc, argv);

    std::println("\n========================================");
    std::println("Vulkan Multi-GPU Renderer Benchmark");
    std::println("========================================\n");

    auto& registry = BenchmarkCaseRegistry::instance();

    if (config.listCases)
    {
        registry.printAvailableCases();
        return EXIT_SUCCESS;
    }

    if (registry.getCaseNames().empty())
    {
        std::println(stderr, "No benchmark cases registered.");
        std::println(stderr, "Create benchmark cases by implementing BenchmarkCase and using REGISTER_BENCHMARK_CASE macro.");
        return EXIT_FAILURE;
    }

    std::vector<std::string> casesToRun;
    if (config.runAll)
    {
        casesToRun = registry.getCaseNames();
    }
    else if (!config.casesToRun.empty())
    {
        casesToRun = config.casesToRun;
    }
    else
    {
        std::println(stderr, "No benchmark cases specified.");
        std::println(stderr, "Use --all to run all cases, or specify case names.");
        std::println(stderr, "Use --list to see available cases.");
        return EXIT_FAILURE;
    }

    std::filesystem::create_directories(config.outputDir);

    BenchmarkRunner::Config runnerConfig;
    runnerConfig.outputDir = config.outputDir;
    runnerConfig.runSingleGPU = config.runSingleGPU;
    runnerConfig.runSFR = config.runSFR;
    runnerConfig.runAFR = config.runAFR;

    BenchmarkRunner runner(runnerConfig);

    try
    {
        auto results = runner.runCases(casesToRun);

        std::println("\n========================================");
        std::println("Benchmark complete!");
        std::println("Results saved to: {}/", config.outputDir);
        std::println("========================================\n");
    }
    catch (const std::exception& e)
    {
        std::println(stderr, "Error: {}", e.what());
        system("pause");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
