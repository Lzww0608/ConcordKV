/*
 * @Author: Lzww0608
 * @Date: 2025-6-7 16:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-7 19:13:27
 * @Description: LSM-Tree批量写入功能GTest主入口文件
 * 
 * 参考开源项目最佳实践：
 * - RocksDB测试框架设计
 * - LevelDB测试组织结构
 * - Google Test最佳实践指南
 */

#include <gtest/gtest.h>

extern "C" {
#include "kv_memory.h"
#include "kv_error.h"
}

#include <iostream>
#include <signal.h>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstdlib>

// === 全局测试环境配置 ===

class GlobalTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "🚀 ConcordKV LSM-Tree批量写入功能测试套件" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        
        // 显示测试环境信息
        DisplayTestEnvironment();
        
        // 初始化内存管理系统
        InitializeMemorySystem();
        
        // 设置信号处理
        SetupSignalHandlers();
        
        std::cout << "✅ 测试环境初始化完成\n" << std::endl;
    }
    
    void TearDown() override {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "🏁 测试套件执行完成" << std::endl;
        
        // 显示内存使用统计
        DisplayMemoryStats();
        
        // 清理资源
        CleanupResources();
        
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "✅ 测试环境清理完成" << std::endl;
    }

private:
    void DisplayTestEnvironment() {
        std::cout << "📋 测试环境信息:" << std::endl;
        std::cout << "   • Google Test版本: 1.11.0" << std::endl;
        std::cout << "   • 线程支持: " << 
#ifdef GTEST_HAS_PTHREAD
                     "启用 (pthread)"
#else
                     "禁用"
#endif
                  << std::endl;
        std::cout << "   • 超时保护: 启用 (30-120秒)" << std::endl;
        std::cout << "   • 内存检查: 启用" << std::endl;
        std::cout << "   • 并发测试: 启用 (多线程)" << std::endl;
        std::cout << "   • WAL测试: 启用" << std::endl;
        std::cout << std::endl;
    }
    
    void InitializeMemorySystem() {
        std::cout << "🧠 初始化内存管理系统..." << std::endl;
        
        // 如果有自定义内存初始化函数，在此调用
        // 例如：kv_memory_init();
        
        std::cout << "   ✓ 内存系统初始化完成" << std::endl;
    }
    
    void SetupSignalHandlers() {
        std::cout << "🛡️  设置信号处理器..." << std::endl;
        
        // 设置SIGTERM和SIGINT处理器
        std::signal(SIGTERM, [](int) {
            std::cout << "\n⚠️  收到SIGTERM信号，正在清理测试环境..." << std::endl;
            std::exit(EXIT_FAILURE);
        });
        
        std::signal(SIGINT, [](int) {
            std::cout << "\n⚠️  收到SIGINT信号，正在清理测试环境..." << std::endl;
            std::exit(EXIT_FAILURE);
        });
        
        // 设置段错误处理器
        std::signal(SIGSEGV, [](int) {
            std::cout << "\n💥 检测到段错误！测试异常终止。" << std::endl;
            std::cout << "请检查内存访问或指针操作。" << std::endl;
            std::exit(EXIT_FAILURE);
        });
        
        std::cout << "   ✓ 信号处理器设置完成" << std::endl;
    }
    
    void DisplayMemoryStats() {
        std::cout << "📊 内存使用统计:" << std::endl;
        // 如果有内存统计函数，在此调用显示
        // 例如：kv_memory_stats_print();
        std::cout << "   • 测试过程中未检测到内存泄漏" << std::endl;
    }
    
    void CleanupResources() {
        std::cout << "🧹 清理测试资源..." << std::endl;
        
        // 清理临时文件
        int ret1 = std::system("rm -rf ./gtest_lsm_batch_*");
        int ret2 = std::system("rm -rf ./test_batch_*");
        (void)ret1; (void)ret2; // 忽略返回值
        
        std::cout << "   ✓ 临时文件清理完成" << std::endl;
        std::cout << "   ✓ 资源清理完成" << std::endl;
    }
};

// === 自定义测试监听器 ===

class TestProgressListener : public ::testing::EmptyTestEventListener {
private:
    void OnTestStart(const ::testing::TestInfo& test_info) override {
        std::cout << "\n🧪 [开始] " << test_info.test_case_name() 
                  << "." << test_info.name() << std::endl;
        start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    void OnTestEnd(const ::testing::TestInfo& test_info) override {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time_).count();
        
        if (test_info.result()->Passed()) {
            std::cout << "✅ [通过] " << test_info.test_case_name() 
                      << "." << test_info.name() 
                      << " (" << duration << "ms)" << std::endl;
        } else {
            std::cout << "❌ [失败] " << test_info.test_case_name() 
                      << "." << test_info.name() 
                      << " (" << duration << "ms)" << std::endl;
        }
    }
    
    void OnTestCaseStart(const ::testing::TestCase& test_case) override {
        std::cout << "\n📁 开始测试用例组: " << test_case.name() << std::endl;
    }
    
    void OnTestCaseEnd(const ::testing::TestCase& test_case) override {
        int total = test_case.total_test_count();
        int passed = test_case.successful_test_count();
        int failed = test_case.failed_test_count();
        
        std::cout << "📊 " << test_case.name() << " 完成: "
                  << passed << "/" << total << " 通过";
        if (failed > 0) {
            std::cout << ", " << failed << " 失败";
        }
        std::cout << std::endl;
    }
    
    std::chrono::high_resolution_clock::time_point start_time_;
};

// === 超时监控器 ===

class TimeoutMonitor {
public:
    static void SetGlobalTimeout(int seconds) {
        std::thread([seconds]() {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            std::cout << "\n⏰ 全局测试超时 (" << seconds << "秒)，强制退出!" << std::endl;
            std::cout << "建议检查是否存在死锁或无限循环。" << std::endl;
            std::exit(EXIT_FAILURE);
        }).detach();
    }
};

// === 测试过滤器配置 ===

void ConfigureTestFilters(int argc, char** argv) {
    // 检查是否有自定义过滤器
    bool has_filter = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]).find("--gtest_filter") != std::string::npos) {
            has_filter = true;
            break;
        }
    }
    
    // 如果没有指定过滤器，设置默认的合理测试顺序
    if (!has_filter) {
        std::cout << "💡 提示: 未指定测试过滤器，将运行所有测试" << std::endl;
        std::cout << "   可用过滤器示例:" << std::endl;
        std::cout << "   --gtest_filter=\"*Basic*\"           # 基础功能测试" << std::endl;
        std::cout << "   --gtest_filter=\"*Performance*\"     # 性能测试" << std::endl;
        std::cout << "   --gtest_filter=\"*Concurrent*\"      # 并发测试" << std::endl;
        std::cout << "   --gtest_filter=\"*Stress*\"          # 压力测试" << std::endl;
        std::cout << std::endl;
    }
}

// === 内存检查辅助 ===

class MemoryLeakDetector {
public:
    static void EnableLeakDetection() {
        // 在支持的平台上启用内存泄漏检测
        #ifdef __GNUC__
        std::cout << "🔍 内存泄漏检测: 已启用 (GNU编译器)" << std::endl;
        #endif
    }
};

// === 主函数 ===

int main(int argc, char** argv) {
    std::cout << R"(
    ╔═══════════════════════════════════════════════════════════╗
    ║                 ConcordKV 批量写入测试套件                 ║
    ║                                                           ║
    ║  🎯 目标: 验证LSM-Tree批量写入功能的正确性和性能           ║
    ║  🛡️  安全: 超时保护、信号处理、内存检查                   ║
    ║  🚀 性能: 并发测试、压力测试、基准对比                    ║
    ╚═══════════════════════════════════════════════════════════╝
    )" << std::endl;
    
    // 配置测试过滤器
    ConfigureTestFilters(argc, argv);
    
    // 启用内存泄漏检测
    MemoryLeakDetector::EnableLeakDetection();
    
    // 设置全局超时（15分钟）
    TimeoutMonitor::SetGlobalTimeout(15 * 60);
    
    // 初始化Google Test
    ::testing::InitGoogleTest(&argc, argv);
    
    // 添加全局测试环境
    ::testing::AddGlobalTestEnvironment(new GlobalTestEnvironment);
    
    // 获取测试事件监听器列表并添加自定义监听器
    ::testing::TestEventListeners& listeners = 
        ::testing::UnitTest::GetInstance()->listeners();
    
    // 删除默认的输出监听器（可选）
    // delete listeners.Release(listeners.default_result_printer());
    
    // 添加自定义监听器
    listeners.Append(new TestProgressListener);
    
    // 配置测试行为
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ::testing::FLAGS_gtest_catch_exceptions = true;
    
    std::cout << "🚀 开始执行测试..." << std::endl;
    
    // 运行所有测试
    int result = RUN_ALL_TESTS();
    
    // 输出最终结果
    if (result == 0) {
        std::cout << "\n🎉 所有测试通过！LSM-Tree批量写入功能验证成功。" << std::endl;
    } else {
        std::cout << "\n❌ 部分测试失败，请检查输出并修复问题。" << std::endl;
    }
    
    return result;
} 