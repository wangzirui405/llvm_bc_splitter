// main.cpp
#include "splitter.h"
#include "logging.h"
#include "verifier.h"
#include "common.h"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "用法: " << argv[0] << " <输入.bc> <输出前缀> [--clone]" << std::endl;
        std::cerr << "选项:" << std::endl;
        std::cerr << "  --clone    使用LLVM Clone模式（默认使用手动模式）" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputPrefix = argv[2];
    bool useCloneMode = false;

    if (argc == 4) {
        std::string option = argv[3];
        if (option == "--clone") {
            useCloneMode = true;
        }
    }

    std::cout << "BC文件拆分工具启动..." << std::endl;
    std::cout << "输入文件: " << inputFile << std::endl;
    std::cout << "输出前缀: " << outputPrefix << std::endl;
    std::cout << "模式: " << (useCloneMode ? "CLONE_MODE" : "MANUAL_MODE") << std::endl;

    try {
        BCModuleSplitter splitter;
        Logger logger;

        splitter.setCloneMode(useCloneMode);

        if (!splitter.loadBCFile(inputFile)) {
            std::cerr << "无法加载BC文件: " << inputFile << std::endl;
            return 1;
        }

        splitter.analyzeFunctions();
        //splitter.analyzeInternalConstants();
        splitter.printFunctionInfo();
        splitter.splitBCFiles(outputPrefix);

        // 批量验证
        splitter.validateAllBCFiles(outputPrefix);

        // 生成报告
        splitter.generateGroupReport(outputPrefix);

        logger.log("程序执行完成");
    } catch (const std::exception& e) {
        std::cerr << "程序执行过程中发生异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}