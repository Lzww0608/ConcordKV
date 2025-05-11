#include <gtest/gtest.h>
#include <iostream>

// 为了解决链接问题，添加缺失的函数
extern "C" {
    int ntyco_entry() {
        // Mock implementation, does nothing
        return 0;
    }
}

int main(int argc, char **argv) {
    std::cout << "Running ConcordKV kvserver tests..." << std::endl;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 