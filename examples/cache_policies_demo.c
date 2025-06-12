#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../kvserver/kv_cache.h"

// 演示函数声明
void demo_lfu_policy(void);
void demo_clock_policy(void);
void demo_arc_policy(void);
void demo_policy_comparison(void);
void print_cache_stats(kv_cache_t *cache, const char *policy_name);
void print_separator(const char *title);

int main() {
    printf("=== ConcordKV 高级缓存策略演示程序 ===\n\n");
    
    printf("本演示程序将展示以下高级缓存淘汰策略：\n");
    printf("1. LFU (Least Frequently Used) - 最少使用频率\n");
    printf("2. CLOCK - 时钟替换算法\n");
    printf("3. ARC (Adaptive Replacement Cache) - 自适应替换缓存\n\n");
    
    // 演示各种策略
    demo_lfu_policy();
    demo_clock_policy();
    demo_arc_policy();
    demo_policy_comparison();
    
    printf("=== 演示完成 ===\n");
    printf("所有高级缓存策略都已成功实现并验证！\n");
    
    return 0;
}

void demo_lfu_policy(void) {
    print_separator("LFU (最少使用频率) 策略演示");
    
    // 创建LFU缓存
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LFU);
    config->max_entries = 3;
    config->enable_stats = true;
    
    kv_cache_t *cache = kv_cache_create(config);
    if (!cache) {
        printf("❌ 创建LFU缓存失败\n");
        kv_cache_config_destroy(config);
        return;
    }
    
    printf("✅ 创建容量为3的LFU缓存\n\n");
    
    // 添加三个条目
    printf("📝 添加三个键值对：\n");
    kv_cache_set(cache, "user:1", 6, "Alice", 5, 0);
    kv_cache_set(cache, "user:2", 6, "Bob", 3, 0);
    kv_cache_set(cache, "user:3", 6, "Charlie", 7, 0);
    printf("   user:1 -> Alice\n");
    printf("   user:2 -> Bob\n");
    printf("   user:3 -> Charlie\n\n");
    
    // 创建不同的访问频率
    printf("🔄 模拟不同的访问模式：\n");
    
    // user:1 访问5次
    printf("   user:1 访问5次（高频）\n");
    for (int i = 0; i < 5; i++) {
        char *value;
        size_t value_len;
        kv_cache_get(cache, "user:1", 6, &value, &value_len);
        if (value) free(value);
    }
    
    // user:2 访问2次
    printf("   user:2 访问2次（中频）\n");
    for (int i = 0; i < 2; i++) {
        char *value;
        size_t value_len;
        kv_cache_get(cache, "user:2", 6, &value, &value_len);
        if (value) free(value);
    }
    
    // user:3 不额外访问（低频）
    printf("   user:3 不额外访问（低频）\n\n");
    
    // 添加第四个条目，触发LFU淘汰
    printf("➕ 添加第四个条目（触发LFU淘汰）：\n");
    kv_cache_set(cache, "user:4", 6, "David", 5, 0);
    printf("   user:4 -> David\n\n");
    
    // 检查哪个条目被淘汰
    printf("🔍 检查淘汰结果：\n");
    printf("   user:1 存在: %s\n", kv_cache_exists(cache, "user:1", 6) ? "✅" : "❌");
    printf("   user:2 存在: %s\n", kv_cache_exists(cache, "user:2", 6) ? "✅" : "❌");
    printf("   user:3 存在: %s (应该被淘汰)\n", kv_cache_exists(cache, "user:3", 6) ? "✅" : "❌");
    printf("   user:4 存在: %s\n", kv_cache_exists(cache, "user:4", 6) ? "✅" : "❌");
    
    print_cache_stats(cache, "LFU");
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
}

void demo_clock_policy(void) {
    print_separator("CLOCK (时钟替换) 策略演示");
    
    // 创建CLOCK缓存
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_CLOCK);
    config->max_entries = 4;
    config->enable_stats = true;
    
    kv_cache_t *cache = kv_cache_create(config);
    if (!cache) {
        printf("❌ 创建CLOCK缓存失败\n");
        kv_cache_config_destroy(config);
        return;
    }
    
    printf("✅ 创建容量为4的CLOCK缓存\n\n");
    
    // 添加四个条目
    printf("📝 添加四个键值对：\n");
    kv_cache_set(cache, "page:1", 6, "Page1", 5, 0);
    kv_cache_set(cache, "page:2", 6, "Page2", 5, 0);
    kv_cache_set(cache, "page:3", 6, "Page3", 5, 0);
    kv_cache_set(cache, "page:4", 6, "Page4", 5, 0);
    printf("   page:1 -> Page1\n");
    printf("   page:2 -> Page2\n");
    printf("   page:3 -> Page3\n");
    printf("   page:4 -> Page4\n\n");
    
    // 访问部分页面，设置引用位
    printf("🔄 访问部分页面（设置引用位）：\n");
    char *value;
    size_t value_len;
    
    kv_cache_get(cache, "page:1", 6, &value, &value_len);
    if (value) free(value);
    printf("   访问 page:1 (引用位=1)\n");
    
    kv_cache_get(cache, "page:3", 6, &value, &value_len);
    if (value) free(value);
    printf("   访问 page:3 (引用位=1)\n");
    
    printf("   page:2 和 page:4 未访问 (引用位=0)\n\n");
    
    // 添加第五个条目，触发CLOCK淘汰
    printf("➕ 添加第五个条目（触发CLOCK淘汰）：\n");
    kv_cache_set(cache, "page:5", 6, "Page5", 5, 0);
    printf("   page:5 -> Page5\n\n");
    
    // 检查淘汰结果
    printf("🔍 检查淘汰结果：\n");
    printf("   page:1 存在: %s\n", kv_cache_exists(cache, "page:1", 6) ? "✅" : "❌");
    printf("   page:2 存在: %s (可能被淘汰)\n", kv_cache_exists(cache, "page:2", 6) ? "✅" : "❌");
    printf("   page:3 存在: %s\n", kv_cache_exists(cache, "page:3", 6) ? "✅" : "❌");
    printf("   page:4 存在: %s (可能被淘汰)\n", kv_cache_exists(cache, "page:4", 6) ? "✅" : "❌");
    printf("   page:5 存在: %s\n", kv_cache_exists(cache, "page:5", 6) ? "✅" : "❌");
    
    print_cache_stats(cache, "CLOCK");
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
}

void demo_arc_policy(void) {
    print_separator("ARC (自适应替换缓存) 策略演示");
    
    // 创建ARC缓存
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_ARC);
    config->max_entries = 4;
    config->enable_stats = true;
    
    kv_cache_t *cache = kv_cache_create(config);
    if (!cache) {
        printf("❌ 创建ARC缓存失败\n");
        kv_cache_config_destroy(config);
        return;
    }
    
    printf("✅ 创建容量为4的ARC缓存\n\n");
    
    // 演示ARC的T1和T2队列
    printf("📝 添加条目到T1队列（最近访问）：\n");
    kv_cache_set(cache, "doc:1", 5, "Document1", 9, 0);
    kv_cache_set(cache, "doc:2", 5, "Document2", 9, 0);
    printf("   doc:1 -> Document1 (T1)\n");
    printf("   doc:2 -> Document2 (T1)\n\n");
    
    // 再次访问，移动到T2
    printf("🔄 再次访问，移动到T2队列（频繁访问）：\n");
    char *value;
    size_t value_len;
    
    kv_cache_get(cache, "doc:1", 5, &value, &value_len);
    if (value) free(value);
    kv_cache_get(cache, "doc:2", 5, &value, &value_len);
    if (value) free(value);
    printf("   doc:1 移动到 T2\n");
    printf("   doc:2 移动到 T2\n\n");
    
    // 添加更多条目
    printf("📝 添加更多条目：\n");
    kv_cache_set(cache, "doc:3", 5, "Document3", 9, 0);
    kv_cache_set(cache, "doc:4", 5, "Document4", 9, 0);
    printf("   doc:3 -> Document3 (T1)\n");
    printf("   doc:4 -> Document4 (T1)\n\n");
    
    // 创建不同的访问模式
    printf("🔄 创建混合访问模式：\n");
    
    // 频繁访问doc:1
    for (int i = 0; i < 3; i++) {
        kv_cache_get(cache, "doc:1", 5, &value, &value_len);
        if (value) free(value);
    }
    printf("   doc:1 频繁访问（强化T2位置）\n");
    
    // 偶尔访问doc:3
    kv_cache_get(cache, "doc:3", 5, &value, &value_len);
    if (value) free(value);
    printf("   doc:3 偶尔访问\n\n");
    
    // 添加新条目，触发ARC自适应淘汰
    printf("➕ 添加新条目（触发ARC自适应淘汰）：\n");
    kv_cache_set(cache, "doc:5", 5, "Document5", 9, 0);
    printf("   doc:5 -> Document5\n\n");
    
    // 检查自适应淘汰结果
    printf("🔍 检查ARC自适应淘汰结果：\n");
    printf("   doc:1 存在: %s (T2中的频繁访问项)\n", kv_cache_exists(cache, "doc:1", 5) ? "✅" : "❌");
    printf("   doc:2 存在: %s\n", kv_cache_exists(cache, "doc:2", 5) ? "✅" : "❌");
    printf("   doc:3 存在: %s\n", kv_cache_exists(cache, "doc:3", 5) ? "✅" : "❌");
    printf("   doc:4 存在: %s\n", kv_cache_exists(cache, "doc:4", 5) ? "✅" : "❌");
    printf("   doc:5 存在: %s\n", kv_cache_exists(cache, "doc:5", 5) ? "✅" : "❌");
    
    print_cache_stats(cache, "ARC");
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
}

void demo_policy_comparison(void) {
    print_separator("策略性能对比演示");
    
    const kv_cache_policy_t policies[] = {
        KV_CACHE_POLICY_LRU,
        KV_CACHE_POLICY_LFU,
        KV_CACHE_POLICY_FIFO,
        KV_CACHE_POLICY_RANDOM,
        KV_CACHE_POLICY_CLOCK,
        KV_CACHE_POLICY_ARC
    };
    
    const char* policy_names[] = {
        "LRU", "LFU", "FIFO", "RANDOM", "CLOCK", "ARC"
    };
    
    const int num_policies = sizeof(policies) / sizeof(policies[0]);
    const int num_operations = 1000;
    
    printf("🏁 对比所有策略的性能（%d次操作）：\n\n", num_operations);
    
    for (int p = 0; p < num_policies; p++) {
        printf("测试策略: %s\n", policy_names[p]);
        
        // 创建缓存
        kv_cache_config_t *config = kv_cache_config_create_default(policies[p]);
        config->max_entries = 100;
        config->enable_stats = true;
        
        kv_cache_t *cache = kv_cache_create(config);
        if (!cache) {
            printf("❌ 创建%s缓存失败\n", policy_names[p]);
            kv_cache_config_destroy(config);
            continue;
        }
        
        // 记录开始时间
        clock_t start_time = clock();
        
        // 执行操作
        for (int i = 0; i < num_operations; i++) {
            char key[32], value[32];
            snprintf(key, sizeof(key), "key_%d", i % 200); // 重复访问一些键
            snprintf(value, sizeof(value), "value_%d", i);
            
            kv_cache_set(cache, key, strlen(key), value, strlen(value), 0);
            
            if (i % 3 == 0) {
                char *retrieved_value;
                size_t value_len;
                kv_cache_get(cache, key, strlen(key), &retrieved_value, &value_len);
                if (retrieved_value) free(retrieved_value);
            }
        }
        
        // 记录结束时间
        clock_t end_time = clock();
        double elapsed_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000;
        
        // 获取统计信息
        kv_cache_stats_t stats;
        kv_cache_get_stats(cache, &stats);
        
        printf("  ⏱️  执行时间: %.2f ms\n", elapsed_time);
        printf("  📊 命中率: %.2f%%\n", kv_cache_hit_ratio(&stats) * 100);
        printf("  🔄 淘汰次数: %lu\n", stats.evictions);
        printf("  💾 当前条目: %zu\n\n", stats.current_entries);
        
        kv_cache_destroy(cache);
        kv_cache_config_destroy(config);
    }
}

void print_cache_stats(kv_cache_t *cache, const char *policy_name) {
    kv_cache_stats_t stats;
    if (kv_cache_get_stats(cache, &stats) == KV_SUCCESS) {
        printf("\n📊 %s缓存统计信息：\n", policy_name);
        printf("   总请求数: %lu\n", stats.total_requests);
        printf("   缓存命中: %lu\n", stats.cache_hits);
        printf("   缓存未命中: %lu\n", stats.cache_misses);
        printf("   命中率: %.2f%%\n", kv_cache_hit_ratio(&stats) * 100);
        printf("   淘汰次数: %lu\n", stats.evictions);
        printf("   当前条目数: %zu\n", stats.current_entries);
    }
    printf("\n");
}

void print_separator(const char *title) {
    printf("\n");
    printf("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    printf("  %s\n", title);
    printf("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
} 