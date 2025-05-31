#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

extern "C" {
#include "kv_persist.h"
}

// 用于清理测试目录的辅助函数
void removeDirectory(const char* dir) {
    DIR* d = opendir(dir);
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            std::string path = std::string(dir) + "/" + entry->d_name;
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    removeDirectory(path.c_str());
                } else {
                    unlink(path.c_str());
                }
            }
        }
        closedir(d);
        rmdir(dir);
    }
}

// 模拟存储引擎和回调函数
typedef struct {
    std::string key;
    std::string value;
    uint8_t op_type;
} mock_op_t;

#define MAX_OPS 100
struct MockEngine {
    mock_op_t ops[MAX_OPS];
    int op_count;
};

int mockApplyLog(void* engine, uint8_t op_type, const char* key, const char* value) {
    MockEngine* mock = static_cast<MockEngine*>(engine);
    if (mock->op_count < MAX_OPS) {
        mock->ops[mock->op_count].op_type = op_type;
        mock->ops[mock->op_count].key = key;
        mock->ops[mock->op_count].value = value ? value : "";
        mock->op_count++;
        return 0;
    }
    return -1;
}

int mockSaveData(void* engine, FILE* fp) {
    MockEngine* mock = static_cast<MockEngine*>(engine);
    fwrite(&mock->op_count, sizeof(int), 1, fp);
    for (int i = 0; i < mock->op_count; i++) {
        uint8_t op_type = mock->ops[i].op_type;
        size_t key_size = mock->ops[i].key.size() + 1;
        size_t value_size = mock->ops[i].value.size() + 1;
        
        fwrite(&op_type, sizeof(uint8_t), 1, fp);
        fwrite(&key_size, sizeof(size_t), 1, fp);
        fwrite(mock->ops[i].key.c_str(), 1, key_size, fp);
        fwrite(&value_size, sizeof(size_t), 1, fp);
        fwrite(mock->ops[i].value.c_str(), 1, value_size, fp);
    }
    return 0;
}

int mockLoadData(void* engine, FILE* fp) {
    MockEngine* mock = static_cast<MockEngine*>(engine);
    mock->op_count = 0;
    
    int count;
    if (fread(&count, sizeof(int), 1, fp) != 1) {
        return -1;
    }
    
    for (int i = 0; i < count && i < MAX_OPS; i++) {
        uint8_t op_type;
        size_t key_size, value_size;
        
        if (fread(&op_type, sizeof(uint8_t), 1, fp) != 1) break;
        if (fread(&key_size, sizeof(size_t), 1, fp) != 1) break;
        
        char* key = new char[key_size];
        if (fread(key, 1, key_size, fp) != key_size) {
            delete[] key;
            break;
        }
        
        if (fread(&value_size, sizeof(size_t), 1, fp) != 1) {
            delete[] key;
            break;
        }
        
        char* value = new char[value_size];
        if (fread(value, 1, value_size, fp) != value_size) {
            delete[] key;
            delete[] value;
            break;
        }
        
        mock->ops[i].op_type = op_type;
        mock->ops[i].key = key;
        mock->ops[i].value = value;
        mock->op_count++;
        
        delete[] key;
        delete[] value;
    }
    
    return 0;
}

class KvPersistTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建测试目录
        mkdir("test_data", 0755);
        
        // 初始化模拟引擎
        mock_engine.op_count = 0;
    }
    
    void TearDown() override {
        // 清理测试目录
        removeDirectory("test_data");
    }
    
    MockEngine mock_engine;
};

// 测试WAL初始化和销毁
TEST_F(KvPersistTest, WalInitAndDestroy) {
    wal_t wal;
    ASSERT_EQ(0, wal_init(&wal, "test_data/wal", 1));
    
    // 检查WAL文件是否已创建
    struct stat st;
    ASSERT_EQ(0, stat("test_data/wal", &st));
    ASSERT_TRUE(S_ISDIR(st.st_mode));
    
    wal_destroy(&wal);
}

// 测试WAL写入和恢复
TEST_F(KvPersistTest, WalAppendAndRecover) {
    wal_t wal;
    ASSERT_EQ(0, wal_init(&wal, "test_data/wal", 1));
    
    // 写入一些日志
    ASSERT_EQ(0, wal_append(&wal, WAL_OP_SET, "key1", "value1"));
    ASSERT_EQ(0, wal_append(&wal, WAL_OP_SET, "key2", "value2"));
    ASSERT_EQ(0, wal_append(&wal, WAL_OP_DEL, "key3", NULL));
    
    // 模拟引擎恢复
    ASSERT_EQ(0, wal_recover(&wal, &mock_engine, mockApplyLog));
    
    // 验证恢复的操作
    ASSERT_EQ(3, mock_engine.op_count);
    ASSERT_EQ(WAL_OP_SET, mock_engine.ops[0].op_type);
    ASSERT_EQ("key1", mock_engine.ops[0].key);
    ASSERT_EQ("value1", mock_engine.ops[0].value);
    
    ASSERT_EQ(WAL_OP_SET, mock_engine.ops[1].op_type);
    ASSERT_EQ("key2", mock_engine.ops[1].key);
    ASSERT_EQ("value2", mock_engine.ops[1].value);
    
    ASSERT_EQ(WAL_OP_DEL, mock_engine.ops[2].op_type);
    ASSERT_EQ("key3", mock_engine.ops[2].key);
    ASSERT_EQ("", mock_engine.ops[2].value);
    
    wal_destroy(&wal);
}

// 测试快照创建和加载
TEST_F(KvPersistTest, SnapshotCreateAndLoad) {
    snapshot_t snap;
    ASSERT_EQ(0, snapshot_init(&snap, "test_data/snapshot"));
    
    // 准备一些测试数据
    mock_engine.op_count = 0;
    mock_engine.ops[0].op_type = WAL_OP_SET;
    mock_engine.ops[0].key = "key1";
    mock_engine.ops[0].value = "value1";
    mock_engine.ops[1].op_type = WAL_OP_SET;
    mock_engine.ops[1].key = "key2";
    mock_engine.ops[1].value = "value2";
    mock_engine.op_count = 2;
    
    // 创建快照
    ASSERT_EQ(0, snapshot_create(&snap, &mock_engine, mockSaveData));
    
    // 清空引擎数据
    mock_engine.op_count = 0;
    
    // 加载快照
    ASSERT_EQ(0, snapshot_load(&snap, &mock_engine, mockLoadData));
    
    // 验证加载的数据
    ASSERT_EQ(2, mock_engine.op_count);
    ASSERT_EQ(WAL_OP_SET, mock_engine.ops[0].op_type);
    ASSERT_EQ("key1", mock_engine.ops[0].key);
    ASSERT_EQ("value1", mock_engine.ops[0].value);
    
    ASSERT_EQ(WAL_OP_SET, mock_engine.ops[1].op_type);
    ASSERT_EQ("key2", mock_engine.ops[1].key);
    ASSERT_EQ("value2", mock_engine.ops[1].value);
    
    snapshot_destroy(&snap);
}

// 测试持久化状态管理
TEST_F(KvPersistTest, PersistStateManagement) {
    persist_state_t ps;
    ASSERT_EQ(0, persist_init(&ps, "test_data", 1));
    
    // 检查目录是否已创建
    struct stat st;
    ASSERT_EQ(0, stat("test_data/wal", &st));
    ASSERT_TRUE(S_ISDIR(st.st_mode));
    
    ASSERT_EQ(0, stat("test_data/snapshot", &st));
    ASSERT_TRUE(S_ISDIR(st.st_mode));
    
    persist_destroy(&ps);
}

// 测试WAL参数有效性检查
TEST_F(KvPersistTest, WalParameterValidation) {
    wal_t wal;
    
    // 测试NULL参数
    ASSERT_NE(0, wal_init(NULL, "test_data/wal", 1));
    ASSERT_NE(0, wal_init(&wal, NULL, 1));
    
    // 正确初始化
    ASSERT_EQ(0, wal_init(&wal, "test_data/wal", 1));
    
    // 测试NULL参数的append
    ASSERT_NE(0, wal_append(NULL, WAL_OP_SET, "key", "value"));
    ASSERT_NE(0, wal_append(&wal, WAL_OP_SET, NULL, "value"));
    ASSERT_NE(0, wal_append(&wal, WAL_OP_SET, "key", NULL));
    
    // 删除操作可以有NULL值
    ASSERT_EQ(0, wal_append(&wal, WAL_OP_DEL, "key", NULL));
    
    wal_destroy(&wal);
}

// 测试快照操作的边界情况
TEST_F(KvPersistTest, SnapshotEdgeCases) {
    snapshot_t snap;
    
    // 测试NULL参数
    ASSERT_NE(0, snapshot_init(NULL, "test_data/snapshot"));
    ASSERT_NE(0, snapshot_init(&snap, NULL));
    
    // 正确初始化
    ASSERT_EQ(0, snapshot_init(&snap, "test_data/snapshot"));
    
    // 测试NULL参数的创建和加载
    ASSERT_NE(0, snapshot_create(NULL, &mock_engine, mockSaveData));
    ASSERT_NE(0, snapshot_create(&snap, NULL, mockSaveData));
    ASSERT_NE(0, snapshot_create(&snap, &mock_engine, NULL));
    
    ASSERT_NE(0, snapshot_load(NULL, &mock_engine, mockLoadData));
    ASSERT_NE(0, snapshot_load(&snap, NULL, mockLoadData));
    ASSERT_NE(0, snapshot_load(&snap, &mock_engine, NULL));
    
    snapshot_destroy(&snap);
} 