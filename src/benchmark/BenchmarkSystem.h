#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// ============================================
// BENCHMARK SYSTEM
// Automated performance testing for Voxel Engine
// ============================================

namespace Benchmark {

// Individual frame sample
struct FrameSample {
    double frameTimeMs;
    double cpuUsagePercent;
    double gpuUsagePercent;  // If available
    size_t ramUsageMB;
    size_t vramUsageMB;
};

// Results for a single test section
struct TestResults {
    std::string testName;
    std::string description;

    // Frame time statistics
    double avgFrameTimeMs = 0.0;
    double minFrameTimeMs = 999999.0;
    double maxFrameTimeMs = 0.0;
    double avgFps = 0.0;
    double onePercentLowFps = 0.0;
    double pointOnePercentLowFps = 0.0;

    // Resource usage (averages)
    double avgCpuPercent = 0.0;
    double avgGpuPercent = 0.0;
    size_t avgRamMB = 0;
    size_t peakRamMB = 0;
    size_t avgVramMB = 0;
    size_t peakVramMB = 0;

    // Raw samples for detailed analysis
    std::vector<FrameSample> samples;

    // Duration
    double durationSeconds = 0.0;
    int totalFrames = 0;
};

// Camera waypoint for automated flight
struct CameraWaypoint {
    glm::vec3 position;
    glm::vec3 lookAt;
    float duration;  // Time to reach this waypoint from previous
};

// Benchmark test scenario
struct BenchmarkScenario {
    std::string name;
    std::string description;
    std::vector<CameraWaypoint> cameraPath;
    bool enableWater = true;
    bool enableClouds = true;
    bool enableShadows = true;
    bool enableSSAO = true;
};

// Main benchmark system
class BenchmarkSystem {
public:
    bool isRunning = false;
    bool isComplete = false;
    std::string currentTestName;
    std::string renderMode;  // "Deferred" or "Forward"

    // All test results
    std::vector<TestResults> allResults;

    // Current scenario state
    int currentScenarioIndex = 0;
    int currentWaypointIndex = 0;
    float waypointProgress = 0.0f;

    // Scenarios to run
    std::vector<BenchmarkScenario> scenarios;

    // Current test samples
    std::vector<FrameSample> currentSamples;
    std::chrono::steady_clock::time_point testStartTime;

    // CPU usage tracking (Windows)
    #ifdef _WIN32
    ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    int numProcessors = 1;
    HANDLE self;
    #endif

    void init() {
        #ifdef _WIN32
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;

        FILETIME ftime, fsys, fuser;
        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));

        self = GetCurrentProcess();
        GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
        #endif

        setupDefaultScenarios();
    }

    void setupDefaultScenarios() {
        scenarios.clear();

        // Scenario 1: Chunk Generation Stress Test
        // Fly in a large spiral pattern to force chunk loading
        {
            BenchmarkScenario scenario;
            scenario.name = "Chunk_Generation";
            scenario.description = "Flying through terrain to stress chunk generation and meshing";
            scenario.enableWater = true;
            scenario.enableClouds = true;
            scenario.enableShadows = true;
            scenario.enableSSAO = true;

            // Large spiral path
            float radius = 200.0f;
            float height = 100.0f;
            int segments = 16;
            for (int i = 0; i <= segments; i++) {
                float angle = (float(i) / segments) * glm::two_pi<float>() * 2.0f;  // 2 full rotations
                float r = radius * (1.0f + float(i) / segments);  // Expanding spiral
                float h = height + 20.0f * sin(angle * 0.5f);  // Undulating height

                CameraWaypoint wp;
                wp.position = glm::vec3(cos(angle) * r, h, sin(angle) * r);
                wp.lookAt = glm::vec3(cos(angle + 0.3f) * r * 0.5f, h - 10.0f, sin(angle + 0.3f) * r * 0.5f);
                wp.duration = 1.5f;
                scenario.cameraPath.push_back(wp);
            }
            scenarios.push_back(scenario);
        }

        // Scenario 2: Water Rendering Test
        // Circle around and through water bodies
        {
            BenchmarkScenario scenario;
            scenario.name = "Water_Rendering";
            scenario.description = "Testing water shader performance with reflections and animations";
            scenario.enableWater = true;
            scenario.enableClouds = true;
            scenario.enableShadows = true;
            scenario.enableSSAO = true;

            // Find sea level and fly around it
            float seaLevel = 62.0f;
            float radius = 100.0f;
            int segments = 12;

            // Fly above water looking down
            for (int i = 0; i <= segments / 2; i++) {
                float angle = (float(i) / (segments / 2)) * glm::pi<float>();
                CameraWaypoint wp;
                wp.position = glm::vec3(cos(angle) * radius, seaLevel + 30.0f, sin(angle) * radius);
                wp.lookAt = glm::vec3(0, seaLevel, 0);
                wp.duration = 1.0f;
                scenario.cameraPath.push_back(wp);
            }

            // Fly at water level (partially submerged view)
            for (int i = 0; i <= segments / 2; i++) {
                float angle = glm::pi<float>() + (float(i) / (segments / 2)) * glm::pi<float>();
                CameraWaypoint wp;
                wp.position = glm::vec3(cos(angle) * radius * 0.7f, seaLevel + 2.0f, sin(angle) * radius * 0.7f);
                wp.lookAt = glm::vec3(cos(angle + 0.5f) * 50.0f, seaLevel, sin(angle + 0.5f) * 50.0f);
                wp.duration = 1.0f;
                scenario.cameraPath.push_back(wp);
            }
            scenarios.push_back(scenario);
        }

        // Scenario 3: Shadow/Lighting Stress Test
        // Move through areas with complex shadow casting
        {
            BenchmarkScenario scenario;
            scenario.name = "Shadow_Lighting";
            scenario.description = "Testing shadow mapping and lighting calculations";
            scenario.enableWater = false;  // Disable water to isolate shadows
            scenario.enableClouds = false;
            scenario.enableShadows = true;
            scenario.enableSSAO = true;

            // Fly through forest/complex geometry at different angles to sun
            float radius = 80.0f;
            int segments = 16;
            for (int i = 0; i <= segments; i++) {
                float angle = (float(i) / segments) * glm::two_pi<float>();
                float height = 70.0f + 30.0f * sin(angle * 2.0f);

                CameraWaypoint wp;
                wp.position = glm::vec3(cos(angle) * radius, height, sin(angle) * radius);
                wp.lookAt = glm::vec3(0, 64, 0);
                wp.duration = 1.0f;
                scenario.cameraPath.push_back(wp);
            }
            scenarios.push_back(scenario);
        }

        // Scenario 4: SSAO Stress Test
        // Close-up views of complex geometry
        {
            BenchmarkScenario scenario;
            scenario.name = "SSAO_AmbientOcclusion";
            scenario.description = "Testing Screen Space Ambient Occlusion performance";
            scenario.enableWater = false;
            scenario.enableClouds = false;
            scenario.enableShadows = false;  // Disable shadows to isolate SSAO
            scenario.enableSSAO = true;

            // Close flyby of terrain
            float radius = 40.0f;
            int segments = 12;
            for (int i = 0; i <= segments; i++) {
                float angle = (float(i) / segments) * glm::two_pi<float>();

                CameraWaypoint wp;
                wp.position = glm::vec3(cos(angle) * radius, 75.0f, sin(angle) * radius);
                wp.lookAt = glm::vec3(cos(angle) * (radius - 20.0f), 65.0f, sin(angle) * (radius - 20.0f));
                wp.duration = 1.2f;
                scenario.cameraPath.push_back(wp);
            }
            scenarios.push_back(scenario);
        }

        // Scenario 5: Cloud Rendering Test
        // Looking up at sky with volumetric clouds
        {
            BenchmarkScenario scenario;
            scenario.name = "Cloud_Rendering";
            scenario.description = "Testing cloud shader performance";
            scenario.enableWater = false;
            scenario.enableClouds = true;
            scenario.enableShadows = false;
            scenario.enableSSAO = false;

            // Fly while looking at sky
            float radius = 60.0f;
            int segments = 10;
            for (int i = 0; i <= segments; i++) {
                float angle = (float(i) / segments) * glm::two_pi<float>();

                CameraWaypoint wp;
                wp.position = glm::vec3(cos(angle) * radius, 90.0f, sin(angle) * radius);
                wp.lookAt = glm::vec3(cos(angle + 0.2f) * 30.0f, 200.0f, sin(angle + 0.2f) * 30.0f);  // Look up at clouds
                wp.duration = 1.5f;
                scenario.cameraPath.push_back(wp);
            }
            scenarios.push_back(scenario);
        }

        // Scenario 6: Full Scene Stress Test
        // Everything enabled, maximum complexity
        {
            BenchmarkScenario scenario;
            scenario.name = "Full_Scene_Stress";
            scenario.description = "Maximum load with all features enabled";
            scenario.enableWater = true;
            scenario.enableClouds = true;
            scenario.enableShadows = true;
            scenario.enableSSAO = true;

            // Complex path touching all elements
            std::vector<glm::vec3> keyPositions = {
                glm::vec3(0, 100, 0),
                glm::vec3(100, 80, 100),
                glm::vec3(150, 65, 0),      // Near water level
                glm::vec3(100, 90, -100),
                glm::vec3(0, 120, -150),    // High up looking at clouds
                glm::vec3(-100, 70, -100),
                glm::vec3(-150, 65, 0),     // Near water again
                glm::vec3(-100, 85, 100),
                glm::vec3(0, 100, 0),       // Back to start
            };

            for (size_t i = 0; i < keyPositions.size(); i++) {
                CameraWaypoint wp;
                wp.position = keyPositions[i];
                wp.lookAt = keyPositions[(i + 1) % keyPositions.size()];
                wp.duration = 2.0f;
                scenario.cameraPath.push_back(wp);
            }
            scenarios.push_back(scenario);
        }
    }

    void startBenchmark(const std::string& mode) {
        renderMode = mode;
        isRunning = true;
        isComplete = false;
        currentScenarioIndex = 0;
        currentWaypointIndex = 0;
        waypointProgress = 0.0f;
        allResults.clear();
        currentSamples.clear();

        if (!scenarios.empty()) {
            currentTestName = scenarios[0].name;
            testStartTime = std::chrono::steady_clock::now();
        }

        std::cout << "\n========================================" << std::endl;
        std::cout << "BENCHMARK STARTED - " << renderMode << " Rendering" << std::endl;
        std::cout << "========================================" << std::endl;
    }

    void stopBenchmark() {
        if (isRunning) {
            finalizeCurrentTest();
        }
        isRunning = false;
        isComplete = true;
    }

    // Get current CPU usage (Windows)
    double getCpuUsage() {
        #ifdef _WIN32
        FILETIME ftime, fsys, fuser;
        ULARGE_INTEGER now, sys, user;

        GetSystemTimeAsFileTime(&ftime);
        memcpy(&now, &ftime, sizeof(FILETIME));

        GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
        memcpy(&sys, &fsys, sizeof(FILETIME));
        memcpy(&user, &fuser, sizeof(FILETIME));

        double percent = 0.0;
        if (now.QuadPart - lastCPU.QuadPart > 0) {
            percent = (sys.QuadPart - lastSysCPU.QuadPart) + (user.QuadPart - lastUserCPU.QuadPart);
            percent /= (now.QuadPart - lastCPU.QuadPart);
            percent /= numProcessors;
            percent *= 100.0;
        }

        lastCPU = now;
        lastSysCPU = sys;
        lastUserCPU = user;

        return percent;
        #else
        return 0.0;
        #endif
    }

    // Get current RAM usage
    size_t getRamUsageMB() {
        #ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize / (1024 * 1024);
        }
        #endif
        return 0;
    }

    // Get VRAM usage (NVIDIA)
    size_t getVramUsageMB() {
        GLint totalMemKB = 0;
        GLint availMemKB = 0;

        // Try NVIDIA extension
        glGetIntegerv(0x9048, &totalMemKB);  // GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
        glGetIntegerv(0x9049, &availMemKB);  // GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX

        if (totalMemKB > 0 && availMemKB > 0) {
            return (totalMemKB - availMemKB) / 1024;
        }

        // Try ATI extension
        glGetIntegerv(0x87FB, &availMemKB);  // GL_TEXTURE_FREE_MEMORY_ATI
        if (availMemKB > 0) {
            // Can't get total, estimate based on typical VRAM sizes
            return 0;  // Can't accurately determine
        }

        return 0;
    }

    // Record a frame sample
    void recordFrame(double frameTimeMs, double gpuTimeMs = 0.0) {
        if (!isRunning) return;

        FrameSample sample;
        sample.frameTimeMs = frameTimeMs;
        sample.cpuUsagePercent = getCpuUsage();
        sample.gpuUsagePercent = 0.0;  // GPU usage % requires vendor-specific queries
        sample.ramUsageMB = getRamUsageMB();
        sample.vramUsageMB = getVramUsageMB();

        currentSamples.push_back(sample);
    }

    // Update camera position along path
    // Returns true if benchmark should continue, false if complete
    bool updateCamera(float deltaTime, glm::vec3& outPosition, glm::vec3& outLookAt) {
        if (!isRunning || scenarios.empty()) return false;

        BenchmarkScenario& scenario = scenarios[currentScenarioIndex];
        if (scenario.cameraPath.empty()) return false;

        // Get current and next waypoint
        int nextWaypoint = (currentWaypointIndex + 1) % scenario.cameraPath.size();
        CameraWaypoint& current = scenario.cameraPath[currentWaypointIndex];
        CameraWaypoint& next = scenario.cameraPath[nextWaypoint];

        // Interpolate position and look-at
        float t = waypointProgress;
        // Smooth step for more natural camera motion
        t = t * t * (3.0f - 2.0f * t);

        outPosition = glm::mix(current.position, next.position, t);
        outLookAt = glm::mix(current.lookAt, next.lookAt, t);

        // Advance progress
        waypointProgress += deltaTime / next.duration;

        // Check if reached waypoint
        if (waypointProgress >= 1.0f) {
            waypointProgress = 0.0f;
            currentWaypointIndex = nextWaypoint;

            // Check if completed full path
            if (currentWaypointIndex == 0) {
                // Finalize this test
                finalizeCurrentTest();

                // Move to next scenario
                currentScenarioIndex++;
                if (currentScenarioIndex >= (int)scenarios.size()) {
                    // All scenarios complete
                    stopBenchmark();
                    return false;
                }

                // Start next scenario
                currentTestName = scenarios[currentScenarioIndex].name;
                currentSamples.clear();
                testStartTime = std::chrono::steady_clock::now();

                std::cout << "\n--- Starting test: " << currentTestName << " ---" << std::endl;
            }
        }

        return true;
    }

    // Get current scenario settings
    BenchmarkScenario* getCurrentScenario() {
        if (currentScenarioIndex < (int)scenarios.size()) {
            return &scenarios[currentScenarioIndex];
        }
        return nullptr;
    }

    void finalizeCurrentTest() {
        if (currentSamples.empty()) return;

        TestResults results;
        results.testName = currentTestName;
        results.samples = currentSamples;
        results.totalFrames = (int)currentSamples.size();

        auto endTime = std::chrono::steady_clock::now();
        results.durationSeconds = std::chrono::duration<double>(endTime - testStartTime).count();

        // Calculate frame time statistics
        std::vector<double> frameTimes;
        double totalFrameTime = 0.0;
        double totalCpu = 0.0;
        size_t totalRam = 0;
        size_t peakRam = 0;
        size_t totalVram = 0;
        size_t peakVram = 0;

        for (const auto& sample : currentSamples) {
            frameTimes.push_back(sample.frameTimeMs);
            totalFrameTime += sample.frameTimeMs;
            totalCpu += sample.cpuUsagePercent;
            totalRam += sample.ramUsageMB;
            peakRam = std::max(peakRam, sample.ramUsageMB);
            totalVram += sample.vramUsageMB;
            peakVram = std::max(peakVram, sample.vramUsageMB);

            results.minFrameTimeMs = std::min(results.minFrameTimeMs, sample.frameTimeMs);
            results.maxFrameTimeMs = std::max(results.maxFrameTimeMs, sample.frameTimeMs);
        }

        int n = (int)currentSamples.size();
        results.avgFrameTimeMs = totalFrameTime / n;
        results.avgFps = 1000.0 / results.avgFrameTimeMs;
        results.avgCpuPercent = totalCpu / n;
        results.avgRamMB = totalRam / n;
        results.peakRamMB = peakRam;
        results.avgVramMB = totalVram / n;
        results.peakVramMB = peakVram;

        // Calculate 1% and 0.1% lows
        std::sort(frameTimes.begin(), frameTimes.end(), std::greater<double>());  // Sort descending (worst first)

        int onePercentCount = std::max(1, n / 100);
        int pointOnePercentCount = std::max(1, n / 1000);

        double onePercentTotal = 0.0;
        for (int i = 0; i < onePercentCount; i++) {
            onePercentTotal += frameTimes[i];
        }
        results.onePercentLowFps = 1000.0 / (onePercentTotal / onePercentCount);

        double pointOnePercentTotal = 0.0;
        for (int i = 0; i < pointOnePercentCount; i++) {
            pointOnePercentTotal += frameTimes[i];
        }
        results.pointOnePercentLowFps = 1000.0 / (pointOnePercentTotal / pointOnePercentCount);

        // Find description from scenario
        for (const auto& scenario : scenarios) {
            if (scenario.name == currentTestName) {
                results.description = scenario.description;
                break;
            }
        }

        allResults.push_back(results);

        std::cout << "Test complete: " << currentTestName << std::endl;
        std::cout << "  Avg FPS: " << std::fixed << std::setprecision(1) << results.avgFps
                  << " | 1% Low: " << results.onePercentLowFps << std::endl;
    }

    // Save results to file
    void saveResults(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open benchmark results file: " << filename << std::endl;
            return;
        }

        // Get current time for header
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

        file << "================================================================================\n";
        file << "VOXEL ENGINE BENCHMARK RESULTS\n";
        file << "================================================================================\n";
        file << "Date: " << std::ctime(&nowTime);
        file << "Rendering API: OpenGL 4.6\n";
        file << "Rendering Mode: " << renderMode << "\n";
        file << "Total Tests: " << allResults.size() << "\n";
        file << "================================================================================\n\n";

        // Summary table
        file << "SUMMARY\n";
        file << "--------------------------------------------------------------------------------\n";
        file << std::left << std::setw(25) << "Test Name"
             << std::right << std::setw(10) << "Avg FPS"
             << std::setw(12) << "1% Low"
             << std::setw(12) << "0.1% Low"
             << std::setw(10) << "CPU %"
             << std::setw(10) << "RAM MB"
             << std::setw(10) << "VRAM MB"
             << "\n";
        file << "--------------------------------------------------------------------------------\n";

        for (const auto& result : allResults) {
            file << std::left << std::setw(25) << result.testName
                 << std::right << std::fixed << std::setprecision(1)
                 << std::setw(10) << result.avgFps
                 << std::setw(12) << result.onePercentLowFps
                 << std::setw(12) << result.pointOnePercentLowFps
                 << std::setw(10) << result.avgCpuPercent
                 << std::setw(10) << result.avgRamMB
                 << std::setw(10) << result.avgVramMB
                 << "\n";
        }
        file << "--------------------------------------------------------------------------------\n\n";

        // Detailed results for each test
        for (const auto& result : allResults) {
            file << "================================================================================\n";
            file << "TEST: " << result.testName << "\n";
            file << "================================================================================\n";
            file << "Description: " << result.description << "\n\n";

            file << "FRAME TIMING\n";
            file << "  Duration:        " << std::fixed << std::setprecision(2) << result.durationSeconds << " seconds\n";
            file << "  Total Frames:    " << result.totalFrames << "\n";
            file << "  Average FPS:     " << std::setprecision(1) << result.avgFps << "\n";
            file << "  1% Low FPS:      " << result.onePercentLowFps << "\n";
            file << "  0.1% Low FPS:    " << result.pointOnePercentLowFps << "\n";
            file << "  Avg Frame Time:  " << std::setprecision(2) << result.avgFrameTimeMs << " ms\n";
            file << "  Min Frame Time:  " << result.minFrameTimeMs << " ms\n";
            file << "  Max Frame Time:  " << result.maxFrameTimeMs << " ms\n\n";

            file << "RESOURCE USAGE\n";
            file << "  Avg CPU:         " << std::setprecision(1) << result.avgCpuPercent << "%\n";
            file << "  Avg RAM:         " << result.avgRamMB << " MB\n";
            file << "  Peak RAM:        " << result.peakRamMB << " MB\n";
            file << "  Avg VRAM:        " << result.avgVramMB << " MB\n";
            file << "  Peak VRAM:       " << result.peakVramMB << " MB\n\n";
        }

        // Overall statistics
        if (!allResults.empty()) {
            double overallAvgFps = 0.0;
            double overallAvg1Percent = 0.0;
            for (const auto& result : allResults) {
                overallAvgFps += result.avgFps;
                overallAvg1Percent += result.onePercentLowFps;
            }
            overallAvgFps /= allResults.size();
            overallAvg1Percent /= allResults.size();

            file << "================================================================================\n";
            file << "OVERALL STATISTICS\n";
            file << "================================================================================\n";
            file << "Average FPS (all tests):     " << std::fixed << std::setprecision(1) << overallAvgFps << "\n";
            file << "Average 1% Low (all tests):  " << overallAvg1Percent << "\n";
            file << "================================================================================\n";
        }

        file.close();
        std::cout << "Benchmark results saved to: " << filename << std::endl;
    }

    // Get progress percentage (0-100)
    float getProgress() const {
        if (scenarios.empty()) return 100.0f;

        float scenarioProgress = float(currentScenarioIndex) / float(scenarios.size());
        float waypointContrib = 0.0f;

        if (currentScenarioIndex < (int)scenarios.size()) {
            const auto& scenario = scenarios[currentScenarioIndex];
            if (!scenario.cameraPath.empty()) {
                waypointContrib = (float(currentWaypointIndex) + waypointProgress) /
                                  float(scenario.cameraPath.size()) / float(scenarios.size());
            }
        }

        return (scenarioProgress + waypointContrib) * 100.0f;
    }

    std::string getStatusText() const {
        if (!isRunning && !isComplete) return "Ready";
        if (isComplete) return "Complete - " + renderMode;

        std::stringstream ss;
        ss << renderMode << " - " << currentTestName;
        ss << " (" << std::fixed << std::setprecision(0) << getProgress() << "%)";
        return ss.str();
    }
};

}  // namespace Benchmark
