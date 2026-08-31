// Stand-alone system performance test.
//
// Build examples:
//   Linux:   c++ -O2 -std=c++11 -o perf perf.cpp
//   Windows: cl /O2 /EHsc /std:c++14 perf.cpp
//
// The benchmark itself uses only the C++ standard library.  The Windows
// frequency probe uses the Windows SDK (loaded dynamically, so no import
// library is required).  Linux frequency probing uses procfs/sysfs files.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601
#  endif
#  include <windows.h>
#endif

namespace {

volatile std::uint64_t g_integer_sink = 0;
volatile double g_float_sink = 0.0;

struct Options {
    std::size_t memory_mib = 512;
    std::size_t passes = 2;
    std::uint64_t arithmetic_iterations = 80000000ULL;
};

struct FrequencySample {
    bool valid = false;
    double mhz = 0.0;
    int cpu = -1;
    std::string source;
};

struct BenchmarkResult {
    double seconds = 0.0;
    double gib_per_second = 0.0;
    std::uint64_t checksum = 0;
};

std::string architecture_name() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "ARM64";
#elif defined(__arm__) || defined(_M_ARM)
    return "ARM";
#else
    return "unknown";
#endif
}

std::string operating_system_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown OS";
#endif
}

bool parse_unsigned(const std::string& text, std::uint64_t& value) {
    if (text.empty() || text[0] == '-') {
        return false;
    }
    try {
        std::size_t parsed = 0;
        const unsigned long long result = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return false;
        }
        value = static_cast<std::uint64_t>(result);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool option_value(int argc, char** argv, int& index, const std::string& name,
                  std::string& value) {
    const std::string argument(argv[index]);
    const std::string prefix = name + "=";
    if (argument.compare(0, prefix.size(), prefix) == 0) {
        value = argument.substr(prefix.size());
        return true;
    }
    if (argument == name && index + 1 < argc) {
        value = argv[++index];
        return true;
    }
    return false;
}

void print_usage(const char* executable) {
    std::cout << "Usage: " << executable << " [options]\n"
              << "  --memory-mb N       Large memory buffer in MiB (16..2048, default 512)\n"
              << "  --passes N          Large-buffer passes (1..100, default 2)\n"
              << "  --arith-iters N     Integer/floating iterations (1M..2B, default 80M)\n"
              << "  --help              Show this help\n";
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            return false;
        }

        std::string value;
        std::uint64_t number = 0;
        if (option_value(argc, argv, i, "--memory-mb", value)) {
            if (!parse_unsigned(value, number) || number < 16 || number > 2048) {
                std::cerr << "Invalid --memory-mb: " << value << "\n";
                return false;
            }
            options.memory_mib = static_cast<std::size_t>(number);
        } else if (option_value(argc, argv, i, "--passes", value)) {
            if (!parse_unsigned(value, number) || number < 1 || number > 100) {
                std::cerr << "Invalid --passes: " << value << "\n";
                return false;
            }
            options.passes = static_cast<std::size_t>(number);
        } else if (option_value(argc, argv, i, "--arith-iters", value)) {
            if (!parse_unsigned(value, number) || number < 1000000ULL ||
                number > 2000000000ULL) {
                std::cerr << "Invalid --arith-iters: " << value << "\n";
                return false;
            }
            options.arithmetic_iterations = number;
        } else {
            std::cerr << "Unknown option: " << argument << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

#if defined(__linux__)

bool read_integer_file(const std::string& path, std::uint64_t& value) {
    std::ifstream input(path.c_str());
    if (!input) {
        return false;
    }
    input >> value;
    return static_cast<bool>(input);
}

int current_linux_cpu() {
    std::ifstream input("/proc/self/stat");
    if (!input) {
        return -1;
    }

    std::string line;
    std::getline(input, line);
    const std::size_t closing_parenthesis = line.rfind(')');
    if (closing_parenthesis == std::string::npos ||
        closing_parenthesis + 2 >= line.size()) {
        return -1;
    }

    // After the command name, the first token is field 3 (state).  The
    // processor number is field 39, hence token 36 in this suffix.
    std::istringstream fields(line.substr(closing_parenthesis + 2));
    std::string token;
    for (int field = 3; field <= 39; ++field) {
        if (!(fields >> token)) {
            return -1;
        }
    }
    std::uint64_t cpu = 0;
    if (!parse_unsigned(token, cpu) || cpu > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return -1;
    }
    return static_cast<int>(cpu);
}

bool cpuinfo_frequency(int cpu, double& mhz) {
    std::ifstream input("/proc/cpuinfo");
    if (!input) {
        return false;
    }

    std::string line;
    int processor = -1;
    while (std::getline(input, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            if (line.empty()) {
                processor = -1;
            }
            continue;
        }

        const std::string key = line.substr(0, colon);
        const std::string value = trim(line.substr(colon + 1));
        if (key.find("processor") != std::string::npos) {
            std::uint64_t parsed = 0;
            if (parse_unsigned(value, parsed)) {
                processor = static_cast<int>(parsed);
            }
        } else if (processor == cpu && key.find("cpu MHz") != std::string::npos) {
            try {
                mhz = std::stod(value);
                return mhz > 0.0;
            } catch (const std::exception&) {
                return false;
            }
        }
    }
    return false;
}

FrequencySample read_frequency() {
    FrequencySample sample;
    sample.cpu = current_linux_cpu();

    if (sample.cpu >= 0) {
        const std::string cpu_path = "/sys/devices/system/cpu/cpu" +
                                     std::to_string(sample.cpu) + "/cpufreq/";
        const char* files[] = {"scaling_cur_freq", "cpuinfo_cur_freq"};
        for (const char* file : files) {
            std::uint64_t khz = 0;
            if (read_integer_file(cpu_path + file, khz) && khz > 0) {
                sample.valid = true;
                sample.mhz = static_cast<double>(khz) / 1000.0;
                sample.source = std::string("sysfs/") + file;
                return sample;
            }
        }

        double mhz = 0.0;
        if (cpuinfo_frequency(sample.cpu, mhz)) {
            sample.valid = true;
            sample.mhz = mhz;
            sample.source = "/proc/cpuinfo";
        }
    }
    return sample;
}

#elif defined(_WIN32)

// This is the documented layout used by CallNtPowerInformation with the
// ProcessorInformation information level.  Defining it here avoids a link
// dependency on PowrProf.lib.
struct ProcessorPowerInformation {
    unsigned long number;
    unsigned long max_mhz;
    unsigned long current_mhz;
    unsigned long mhz_limit;
    unsigned long max_idle_state;
    unsigned long current_idle_state;
};

typedef LONG (WINAPI* CallNtPowerInformationFunction)(ULONG, PVOID, ULONG,
                                                       PVOID, ULONG);

FrequencySample read_frequency() {
    FrequencySample sample;
    sample.cpu = static_cast<int>(GetCurrentProcessorNumber());

    HMODULE power_profile = LoadLibraryW(L"PowrProf.dll");
    if (!power_profile) {
        return sample;
    }
    const CallNtPowerInformationFunction call =
        reinterpret_cast<CallNtPowerInformationFunction>(
            GetProcAddress(power_profile, "CallNtPowerInformation"));
    if (!call) {
        FreeLibrary(power_profile);
        return sample;
    }

    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    const DWORD processor_count = system_info.dwNumberOfProcessors;
    std::vector<ProcessorPowerInformation> information(processor_count);
    const LONG status = call(11, nullptr, 0, information.data(),
                             static_cast<ULONG>(information.size() * sizeof(ProcessorPowerInformation)));
    if (status == 0 && sample.cpu >= 0 &&
        static_cast<std::size_t>(sample.cpu) < information.size() &&
        information[static_cast<std::size_t>(sample.cpu)].current_mhz > 0) {
        sample.valid = true;
        sample.mhz = static_cast<double>(information[static_cast<std::size_t>(sample.cpu)].current_mhz);
        sample.source = "CallNtPowerInformation";
    }
    FreeLibrary(power_profile);
    return sample;
}

#else

FrequencySample read_frequency() {
    return FrequencySample();
}

#endif

void print_frequency(const FrequencySample& sample) {
    if (sample.valid) {
        std::cout << std::fixed << std::setprecision(0) << sample.mhz << " MHz"
                  << " (CPU " << sample.cpu << ", " << sample.source << ")";
    } else {
        std::cout << "unavailable";
        if (sample.cpu >= 0) {
            std::cout << " (CPU " << sample.cpu << ")";
        }
    }
}

double seconds_since(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

template <typename Function>
BenchmarkResult run_benchmark(const std::string& name, std::uint64_t logical_bytes,
                              Function function) {
    const FrequencySample before = read_frequency();
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const std::uint64_t checksum = function();
    const double seconds = seconds_since(start);
    const FrequencySample after = read_frequency();

    BenchmarkResult result;
    result.seconds = seconds;
    result.gib_per_second = seconds > 0.0
        ? static_cast<double>(logical_bytes) / seconds / (1024.0 * 1024.0 * 1024.0)
        : 0.0;
    result.checksum = checksum;

    std::cout << "\n" << name << "\n"
              << "  time: " << std::fixed << std::setprecision(3) << result.seconds << " s"
              << ", logical throughput: " << std::setprecision(3) << result.gib_per_second << " GiB/s"
              << ", checksum: 0x" << std::hex << result.checksum << std::dec << "\n"
              << "  frequency before/after: ";
    print_frequency(before);
    std::cout << " / ";
    print_frequency(after);
    std::cout << "\n";
    return result;
}

std::size_t random_index(std::uint64_t& state, std::size_t count) {
    // xorshift64 is deterministic, fast, and has no library dependency.
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    if ((count & (count - 1)) == 0) {
        return static_cast<std::size_t>(state) & (count - 1);
    }
    return static_cast<std::size_t>(state % count);
}

std::uint64_t checksum_double(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")
            ? 0 : 2;
    }

    const std::uint64_t bytes64 = static_cast<std::uint64_t>(options.memory_mib) *
                                  1024ULL * 1024ULL;
    if (bytes64 / sizeof(std::uint64_t) > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "The requested buffer is too large for this process.\n";
        return 2;
    }
    const std::size_t bytes = static_cast<std::size_t>(bytes64);
    const std::size_t elements = bytes / sizeof(std::uint64_t);

    std::cout << "Standalone system performance test\n"
              << "OS/architecture: " << operating_system_name() << " / " << architecture_name() << "\n"
              << "Hardware threads: " << std::thread::hardware_concurrency() << "\n"
              << "Large buffer: " << options.memory_mib << " MiB (" << elements << " uint64 values)\n"
              << "The frequency is sampled immediately before and after each test.\n"
              << "A process can migrate between CPUs; pin it externally when per-core repeatability is needed.\n";

    const FrequencySample initial_frequency = read_frequency();
    std::cout << "Initial real-time CPU frequency: ";
    print_frequency(initial_frequency);
    std::cout << "\n";

    std::vector<std::uint64_t> data;
    try {
        data.resize(elements);
    } catch (const std::bad_alloc&) {
        std::cerr << "Unable to allocate " << options.memory_mib << " MiB.\n";
        return 3;
    }

    // Touch every page before timing the memory tests and make the initial
    // contents non-trivial, so lazy allocation and zero-page optimizations do
    // not become part of the measured result.
    for (std::size_t i = 0; i < elements; ++i) {
        data[i] = static_cast<std::uint64_t>(i) * 0x9e3779b97f4a7c15ULL + 0x123456789abcdef0ULL;
    }

    std::cout << "\nArithmetic tests (approximately 16 operations per iteration)\n";
    run_benchmark("Integer arithmetic", 0, [&]() -> std::uint64_t {
        std::uint64_t a = 0x123456789abcdef0ULL;
        std::uint64_t b = 0x0fedcba987654321ULL;
        std::uint64_t c = 0x3141592653589793ULL;
        std::uint64_t d = 0x2718281828459045ULL;
        std::uint64_t e = 0xaaaaaaaa55555555ULL;
        std::uint64_t f = 0x13579bdf2468ace0ULL;
        std::uint64_t g = 0x55aa55aa33cc33ccULL;
        std::uint64_t h = 0xdeadbeefcafebabeULL;
        for (std::uint64_t i = 0; i < options.arithmetic_iterations; ++i) {
            a = a * 2862933555777941757ULL + b;
            b = b * 3202034522624059733ULL + c;
            c = c * 3935559000370003845ULL + d;
            d = d * 2691343689449507681ULL + e;
            e = e * 1442695040888963407ULL + f;
            f = f * 6364136223846793005ULL + g;
            g = g * 7046029254386353131ULL + h;
            h = h * 1181783497276652981ULL + a;
        }
        const std::uint64_t result = a ^ b ^ c ^ d ^ e ^ f ^ g ^ h;
        g_integer_sink = result;
        return result;
    });

    run_benchmark("Floating-point arithmetic", 0, [&]() -> std::uint64_t {
        double a = 1.001;
        double b = 1.003;
        double c = 1.005;
        double d = 1.007;
        double e = 1.009;
        double f = 1.011;
        double g = 1.013;
        double h = 1.015;
        for (std::uint64_t i = 0; i < options.arithmetic_iterations; ++i) {
            a = a * 1.000000001 + b * 0.000000003;
            b = b * 1.000000002 + c * 0.000000005;
            c = c * 1.000000003 + d * 0.000000007;
            d = d * 1.000000004 + e * 0.000000011;
            e = e * 1.000000005 + f * 0.000000013;
            f = f * 1.000000006 + g * 0.000000017;
            g = g * 1.000000007 + h * 0.000000019;
            h = h * 1.000000008 + a * 0.000000023;
        }
        const double result = a + b + c + d + e + f + g + h;
        g_float_sink = result;
        return checksum_double(result);
    });

    const std::uint64_t large_pass_bytes = bytes64 * options.passes;
    std::cout << "\nMemory tests (large buffer; throughput is logical bytes processed)\n";
    run_benchmark("Sequential read (cache/stream friendly)", large_pass_bytes,
                  [&]() -> std::uint64_t {
        std::uint64_t sum = 0;
        for (std::size_t pass = 0; pass < options.passes; ++pass) {
            for (std::size_t i = 0; i < elements; ++i) {
                sum += data[i];
            }
        }
        g_integer_sink = sum;
        return sum;
    });

    run_benchmark("Sequential read + write (cache/stream friendly)", large_pass_bytes * 2ULL,
                  [&]() -> std::uint64_t {
        std::uint64_t sum = 0;
        for (std::size_t pass = 0; pass < options.passes; ++pass) {
            for (std::size_t i = 0; i < elements; ++i) {
                data[i] = data[i] * 6364136223846793005ULL + 1442695040888963407ULL;
                sum ^= data[i] + static_cast<std::uint64_t>(i);
            }
        }
        g_integer_sink = sum;
        return sum;
    });

    run_benchmark("Random read (cache-miss oriented)", large_pass_bytes,
                  [&]() -> std::uint64_t {
        std::uint64_t sum = 0;
        std::uint64_t random_state = 0x8f3c2d1e0a9b7c6dULL;
        for (std::size_t pass = 0; pass < options.passes; ++pass) {
            for (std::size_t i = 0; i < elements; ++i) {
                sum += data[random_index(random_state, elements)];
            }
        }
        g_integer_sink = sum;
        return sum;
    });

    run_benchmark("Random read + write (cache-miss oriented)", large_pass_bytes * 2ULL,
                  [&]() -> std::uint64_t {
        std::uint64_t sum = 0;
        std::uint64_t random_state = 0x1234abcd5678ef90ULL;
        for (std::size_t pass = 0; pass < options.passes; ++pass) {
            for (std::size_t i = 0; i < elements; ++i) {
                const std::size_t index = random_index(random_state, elements);
                data[index] = data[index] + random_state + 0x9e3779b97f4a7c15ULL;
                sum ^= data[index];
            }
        }
        g_integer_sink = sum;
        return sum;
    });

    // A small working set is revisited many times.  It demonstrates the
    // cache-friendly case directly, independently of the large DDR buffer.
    const std::size_t hot_elements = 64 * 1024;
    std::vector<std::uint64_t> hot_data(hot_elements);
    for (std::size_t i = 0; i < hot_elements; ++i) {
        hot_data[i] = static_cast<std::uint64_t>(i) * 17ULL + 3ULL;
    }
    const std::uint64_t hot_accesses = std::max<std::uint64_t>(
        100000000ULL, options.arithmetic_iterations);
    run_benchmark("Hot-cache cyclic read", hot_accesses * sizeof(std::uint64_t),
                  [&]() -> std::uint64_t {
        std::uint64_t sum = 0;
        for (std::uint64_t i = 0; i < hot_accesses; ++i) {
            sum += hot_data[static_cast<std::size_t>(i) & (hot_elements - 1)];
        }
        g_integer_sink = sum;
        return sum;
    });

    std::cout << "\nAll tests completed. The buffer allocation is capped at 2048 MiB "
                 "(plus a 512 KiB hot-cache buffer).\n";
    return 0;
}
