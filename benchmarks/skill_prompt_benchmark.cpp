// ClawLite — A 模块 benchmark
// 对比：前缀和 + 二分搜索 vs 线性扫描
//
// 输出 CSV：skill_count,linear_median_us,linear_p95_us,binary_median_us,binary_p95_us
// 用法：
//   ./skill_prompt_benchmark > benchmark.csv
// 或：
//   ./skill_prompt_benchmark --max 2000 --step 100 --iters 50 --budget 5000 > benchmark.csv

#include "skill/skill_registry.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace clawlite;

namespace {

struct Options {
    int maxCount = 2000;
    int step = 100;
    int iters = 50;
    int budget = -1; // 修改：默认设为 -1，用于后续判断是否启用动态 Budget
};

static std::string escapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

static size_t formatSkillXmlLengthLocal(const SkillEntry& entry, bool compact) {
    std::string xml = "  <skill>\n";
    xml += "    <name>" + escapeXml(entry.definition.name) + "</name>\n";
    if (!compact) {
        xml += "    <description>" + escapeXml(entry.definition.description) + "</description>\n";
        if (!entry.definition.body.empty()) {
            xml += "    <instructions>" + escapeXml(entry.definition.body) + "</instructions>\n";
        }
    }
    xml += "    <location>" + escapeXml(entry.definition.filePath) + "</location>\n";
    xml += "  </skill>\n";
    return xml.size();
}

static std::vector<SkillEntry> makeSkills(int count) {
    std::vector<SkillEntry> skills;
    skills.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        SkillEntry entry;
        entry.definition.name = "skill_" + std::to_string(i);
        entry.definition.description = std::string(80 + (i % 7) * 20, 'x');
        entry.definition.filePath = "skills/skill_" + std::to_string(i) + "/SKILL.md";
        entry.definition.body = std::string(120 + (i % 5) * 30, 'y');
        entry.source = SkillSource::Builtin;
        entry.sourceDir = "skills";
        skills.push_back(std::move(entry));
    }
    return skills;
}

static int buildLinearKeepCount(const std::vector<size_t>& itemLengths, int budget) {
    const size_t wrapperLength = std::string("<available_skills>\n").size() +
                                 std::string("</available_skills>\n").size();
    const size_t limit = static_cast<size_t>(std::max(0, budget));
    size_t used = wrapperLength;
    int keep = 0;
    for (size_t len : itemLengths) {
        if (used + len > limit) {
            break;
        }
        used += len;
        ++keep;
    }
    return keep;
}

static int buildBinaryKeepCount(const std::vector<size_t>& prefixLengths, int budget) {
    const size_t wrapperLength = std::string("<available_skills>\n").size() +
                                 std::string("</available_skills>\n").size();
    const size_t limit = static_cast<size_t>(std::max(0, budget));
    int lo = 0;
    int hi = static_cast<int>(prefixLengths.size()) - 1;
    while (lo < hi) {
        const int mid = lo + (hi - lo + 1) / 2;
        if (wrapperLength + prefixLengths[static_cast<size_t>(mid)] <= limit) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

// 修改：引入 inner_loops，通过多轮循环抹平高精度定时器误差
static std::vector<double> measureUs(const std::function<int()>& fn, int iters, int inner_loops = 10000) {
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iters));

    // warmup
    for (int i = 0; i < 5; ++i) {
        volatile int sink = fn();
        (void)sink;
    }

    for (int i = 0; i < iters; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        int loop_sink = 0;
        for (int j = 0; j < inner_loops; ++j) {
            loop_sink += fn();
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        // 防止编译器把整个循环优化掉
        volatile int sink = loop_sink;
        (void)sink;

        double total_us = std::chrono::duration<double, std::micro>(end - start).count();
        samples.push_back(total_us / inner_loops); // 计算单次平均耗时
    }
    return samples;
}

static double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 0) {
        return (values[mid - 1] + values[mid]) / 2.0;
    }
    return values[mid];
}

static double p95(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t idx = static_cast<size_t>(std::ceil(values.size() * 0.95)) - 1;
    return values[std::min(idx, values.size() - 1)];
}

static Options parseArgs(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto nextInt = [&](int& target) {
            if (i + 1 < argc) {
                target = std::max(1, std::atoi(argv[++i]));
            }
        };

        if (arg == "--max") nextInt(opt.maxCount);
        else if (arg == "--step") nextInt(opt.step);
        else if (arg == "--iters") nextInt(opt.iters);
        else if (arg == "--budget") nextInt(opt.budget);
    }
    return opt;
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);

    std::cout << "skill_count,linear_median_us,linear_p95_us,binary_median_us,binary_p95_us\n";

    for (int count = opt.step; count <= opt.maxCount; count += opt.step) {
        auto skills = makeSkills(count);

        std::vector<size_t> compactLengths;
        compactLengths.reserve(skills.size());
        for (const auto& skill : skills) {
            compactLengths.push_back(formatSkillXmlLengthLocal(skill, true));
        }

        std::vector<size_t> prefixLengths;
        prefixLengths.reserve(compactLengths.size() + 1);
        prefixLengths.push_back(0);
        for (size_t len : compactLengths) {
            prefixLengths.push_back(prefixLengths.back() + len);
        }

        // 修改：根据当前的技能总长度，动态调整 budget 为总长度的 80%
        // 这样可以强制算法搜索到数组深处，真正对比出 O(N) 和 O(log N) 的区别
        int current_budget = opt.budget;
        if (current_budget < 0) {
            current_budget = static_cast<int>(prefixLengths.back() * 0.8);
        }

        auto linearSamples = measureUs([&]() {
            return buildLinearKeepCount(compactLengths, current_budget);
        }, opt.iters);

        auto binarySamples = measureUs([&]() {
            return buildBinaryKeepCount(prefixLengths, current_budget);
        }, opt.iters);

        std::cout << count << ','
                  << std::fixed << std::setprecision(3)
                  << median(linearSamples) << ','
                  << p95(linearSamples) << ','
                  << median(binarySamples) << ','
                  << p95(binarySamples) << '\n';
    }

    return 0;
}
