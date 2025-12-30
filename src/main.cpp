// main.cpp
#include "splitter.h"
#include "linker.h"
#include "workdirectory.h"
#include "logging.h"
#include "verifier.h"
#include "common.h"

#include <iostream>
#include <filesystem>
#include <regex>

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "用法: " << argv[0] << " <输入.bc> <输出前缀> [--clone/clear]" << std::endl;
        std::cerr << "选项:" << std::endl;
        std::cerr << "  --clone    使用LLVM Clone模式（默认使用手动模式）" << std::endl;
        std::cerr << "  --clear    清理构建环境" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputPrefix = argv[2];
    bool useCloneMode = false;
    Config config;
    BCWorkDir worker;

    if (!worker.checkAllPaths()) {
        std::cerr << "请检查conifg,目录需要‘/’结尾" << std::endl;
        return 1;
    }

    if (inputFile.find("/") != llvm::StringRef::npos) {
        std::cerr << "输入文件需要和二进制相同目录,且不带路径形式" << std::endl;
        return 1;
    }

    if (argc == 4) {
        std::string option = argv[3];
        if (option == "--clone") {
            useCloneMode = true;
        } else if (option == "--clear") {
            std::cout << "清理构建环境..." << std::endl;
            worker.cleanupConfigFiles(outputPrefix);
            return 0;
        }
    }

    std::cout << "BC文件拆分工具启动..." << std::endl;
    std::cout << "输入文件: " << inputFile << std::endl;
    std::cout << "输出前缀: " << outputPrefix << std::endl;
    std::cout << "模式: " << (useCloneMode ? "CLONE_MODE" : "MANUAL_MODE") << std::endl;

    worker.createWorkDirectoryStructure();
    worker.copyFileToWorkspace(inputFile);

    try {
        BCCommon common;
        BCModuleSplitter splitter(common);
        BCLinker linker(common);
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

        //linker.printFileMapDetails();
        //linker.readResponseFile();
        linker.generateInputFiles(outputPrefix);
        linker.enterInWorkDir();
        linker.initphase1();
        if (linker.executeAllGroups()) {
            logger.log("编译成功");
        } else {
            logger.logError("编译失败");
        }
        linker.returnCurrenPath();
        linker.copySoFilesToOutput();

        logger.log("程序执行完成");
    } catch (const std::exception& e) {
        std::cerr << "程序执行过程中发生异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}