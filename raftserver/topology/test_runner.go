package main

import (
	"fmt"
	"os"
)

func main() {
	fmt.Println("🚀 ConcordKV 5.3.1 中央拓扑服务测试套件")
	fmt.Println("==========================================")
	
	if len(os.Args) > 1 && os.Args[1] == "unit" {
		// 运行单元测试
		runUnitTests()
	} else {
		// 运行完整演示
		fmt.Println("📋 选择测试模式:")
		fmt.Println("  1. 完整功能演示 (默认)")
		fmt.Println("  2. 单元测试: go run test_runner.go unit")
		fmt.Println()
		
		main() // 调用原来的演示主函数
	}
}
