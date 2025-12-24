//linker.c
#include "linker.h"

#include "common.h"
#include "logging.h"
#include "core.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <thread>
#include <future>
#include <regex>
#include <algorithm>
#include <mutex>
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Verifier.h"

BCLinker::BCLinker(BCCommon& commonRef) : common(commonRef) {}

std::vector<std::string> BCLinker::readResponseFile() {
    logger.log("读取原response文件...");

    std::vector<std::string> lines;
    std::string filename = config.responseFile;
    std::ifstream file(filename);
    if (!file.is_open()) {
        logger.logError("无法打开response文件: " + filename);
        return lines;
    }

    std::string line;
    //logger.logToFile("############################################################");
    while (std::getline(file, line)) {
        lines.push_back(line);
        //logger.logToFile(line);
    }
    file.close();
    return lines;
}

// 打印vector中所有GroupInfo的详细信息
void BCLinker::printFileMapDetails() {
    const auto& fileMap = common.getFileMap();
    std::stringstream ss;
    ss << "\n==================== File Map Details ====================";
    logger.log(ss.str());

    ss.str("");
    ss << "Total groups: " << fileMap.size();
    logger.log(ss.str());

    logger.log("-----------------------------------------------------------");

    if (fileMap.empty()) {
        logger.log("File map is empty!");
        logger.log("=====================================================");
        return;
    }

    // 创建表格头
    ss.str("");
    ss << std::left
       << std::setw(10) << "Group ID"
       << std::setw(25) << "BC File"
       << std::setw(20) << "Has Demangle"
       << "Dependencies";
    logger.log(ss.str());

    logger.log(std::string(80, '-'));

    // 打印每个GroupInfo的信息
    for (size_t i = 0; i < fileMap.size(); ++i) {
        const GroupInfo* info = fileMap[i];
        if (!info) {
            ss.str("");
            ss << "[" << i << "] NULL pointer";
            logger.log(ss.str());
            continue;
        }

        // 格式化输出
        ss.str("");
        ss << std::left
           << std::setw(10) << info->groupId
           << std::setw(25) << (info->bcFile.length() > 23 ?
                               info->bcFile.substr(0, 20) + "..." : info->bcFile)
           << std::setw(20) << (info->hasKonanCxaDemangle ? "Yes" : "No");

        // 打印依赖项
        if (info->dependencies.empty()) {
            ss << "None";
        } else {
            bool first = true;
            for (int dep : info->dependencies) {
                if (!first) ss << ", ";
                ss << dep;
                first = false;
            }
        }
        logger.log(ss.str());
    }

    logger.log("=====================================================");

    // 可选：打印每个GroupInfo的完整详细信息
    logger.log("\nDetailed Information for each group:");
    for (size_t i = 0; i < fileMap.size(); ++i) {
        const GroupInfo* info = fileMap[i];
        if (info) {
            ss.str("");
            ss << "\n[Group " << i << "]: ";
            logger.log(ss.str());
            info->printDetails();
        } else {
            ss.str("");
            ss << "\n[Group " << i << "]: NULL pointer";
            logger.log(ss.str());
        }
    }
}

void BCLinker::generateInputFiles(const std::string& outputPrefix) {
    logger.log("补全入参涉及的输入文件...");

    const auto& fileMap = common.getFileMap();
    // 读取原始response文件
    auto originalLines = readResponseFile();
    if (originalLines.empty()) {
        logger.logError("response文件为空或读取失败");
        return;
    }

    // 为每个组生成两个版本
    for (int groupId = 0; groupId < fileMap.size(); groupId++) {
        const auto& deps = fileMap[groupId]->dependencies;

        // 版本1: 无依赖
        std::ofstream fileNoDep(std::filesystem::path(config.workDir) /
                               ("response_group_" + std::to_string(groupId) + "_no_dep.txt"));

        // 版本2: 有依赖
        std::ofstream fileWithDep(std::filesystem::path(config.workDir) /
                                 ("response_group_" + std::to_string(groupId) + "_with_dep.txt"));

        for (const auto& line : originalLines) {
            std::string modifiedLine = line;

            // 修改soname
            if (line.find("-o libkn.so") != std::string::npos) {
                modifiedLine = "-o libkn_" + std::to_string(groupId) + ".so";

                // 写入无依赖版本
                fileNoDep << modifiedLine << std::endl;

                // 写入有依赖版本
                fileWithDep << modifiedLine << std::endl;

                // 在有依赖版本中添加依赖的so
                std::string depLine = "";
                if (!deps.empty()) {
                    for (int depId : deps) {
                        depLine += "libkn_" + std::to_string(depId) + ".so ";
                    }
                }
                // 添加组0的依赖（总是所有组依赖组0）
                if (groupId != 0) {
                    depLine += "libkn_0.so";
                }
                fileWithDep << depLine << std::endl;
                continue;
            }
            // 修改依赖的bc
            if (line.find(config.relativeDir + "out.bc") != std::string::npos) {
                modifiedLine = config.relativeDir + fileMap[groupId]->bcFile;

                // 写入无依赖版本
                fileNoDep << modifiedLine << std::endl;

                // 写入有依赖版本
                fileWithDep << modifiedLine << std::endl;

                continue;
            }

            // 处理--defsym行
            if (line.find("--defsym __cxa_demangle=Konan_cxa_demangle") != std::string::npos) {
                // 只有包含Konan_cxa_demangle的组才保留该行
                if (fileMap[groupId]->hasKonanCxaDemangle) {
                    fileNoDep << line << std::endl;
                    fileWithDep << line << std::endl;
                }
                continue;
            }

            // 其他行直接写入
            fileNoDep << line << std::endl;
            fileWithDep << line << std::endl;
        }

        fileNoDep.close();
        fileWithDep.close();
    }
    //复制bc文件至工作目录
    if (!common.copyByPattern(outputPrefix)) {
        logger.logError("复制失败");
    }

}

bool BCLinker::executeLdLld(const std::string& responseFilePath, const std::string& extralCommand) {
    Logger logger;
    Config config;
    std::string command = "ld.lld @" + responseFilePath;
    if (!extralCommand.empty()) command += " " + extralCommand;
    logger.log("---- 执行命令: " + command);

    // 创建日志文件路径
    std::filesystem::path responsePath(responseFilePath);
    std::string logFilePath = config.workSpace + "logs/" + responsePath.stem().string() + "_output.log";

    // 修改命令以重定向输出到文件
    command += " > " + logFilePath + " 2>&1";

    int result = std::system(command.c_str());

    // 读取并记录日志文件内容
    std::ifstream logFile(logFilePath);
    if (logFile.is_open()) {
        std::stringstream ss;
        std::string line;
        bool hasError = false;
        while (std::getline(logFile, line)) {
            // 只记录错误和警告信息到主日志
            if (line.find("error:") != std::string::npos ||
                line.find("Error:") != std::string::npos ||
                line.find("warning:") != std::string::npos) {
                if (!hasError) {
                    ss << "========== " << responseFilePath << " 的错误/警告 ==========" << std::endl;
                    hasError = true;
                }
                ss << line << std::endl;
            }
        }
        if (hasError) {
            ss << "=========================================" << std::endl;
            logger.logToFile(ss.str());
        }
        logFile.close();
    }

    if (result != 0) {
        logger.logError("命令执行失败: " + command + " (返回码: " + std::to_string(result) + ")");
        return false;
    }
    return true;
}

// 处理单个组的两阶段任务
void BCLinker::processGroupTask(int groupId, std::promise<bool>& promise) {
    std::string responseFileNoDep = (std::filesystem::path(config.workDir) /
                                   ("response_group_" + std::to_string(groupId) + "_no_dep.txt")).string();
    std::string responseFileWithDep = (std::filesystem::path(config.workDir) /
                                     ("response_group_" + std::to_string(groupId) + "_with_dep.txt")).string();

    bool success = true;

    // 第一阶段：执行无依赖版本
    {
        std::lock_guard<std::mutex> lock(logMutex);
        logger.log("-- 组 " + std::to_string(groupId) + ": 开始第一阶段 (无依赖版本)");
    }

    if (!executeLdLld(responseFileNoDep, "")) {
        success = false;
        std::lock_guard<std::mutex> lock(logMutex);
        logger.logWarning("-- 组 " + std::to_string(groupId) + " 第一阶段失败");
    } else {
        std::lock_guard<std::mutex> lock(logMutex);
        logger.log("-- 组 " + std::to_string(groupId) + ": 第一阶段完成");
    }

    setPhase1Promise(groupId);
    // 等待依赖组的第一阶段完成
    const auto& groups = common.getFileMap();
    const auto& deps = groups[groupId]->dependencies;
    if (!deps.empty()) {
        {
            std::lock_guard<std::mutex> lock(logMutex);
            logger.log("-- 组 " + std::to_string(groupId) + ": 等待依赖组第一阶段完成");
        }

        for (int depId : deps) {
            try {
                // 安全地获取并等待依赖组的第一阶段完成
                auto depFuture = getPhase1Future(depId);
                depFuture.wait();
            } catch (...) {
                // 如果等待失败，记录错误但继续
                std::lock_guard<std::mutex> lock(logMutex);
                logger.logWarning("-- 组 " + std::to_string(groupId) + ": 等待组 " +
                                  std::to_string(depId) + " 第一阶段时发生异常");
            }
        }
    }
    // 第二阶段：执行有依赖版本（即使第一阶段失败也尝试执行）
    {
        std::lock_guard<std::mutex> lock(logMutex);
        logger.log("-- 组 " + std::to_string(groupId) + ": 开始第二阶段 (有依赖版本)");
    }

    if (!executeLdLld(responseFileWithDep, "--no-defined")) {
        success = false;
        std::lock_guard<std::mutex> lock(logMutex);
        logger.logWarning("-- 组 " + std::to_string(groupId) + " 第二阶段失败");
    } else {
        std::lock_guard<std::mutex> lock(logMutex);
        logger.log("-- 组 " + std::to_string(groupId) + ": 第二阶段完成");
    }

    promise.set_value(success);
}

// 并发执行所有组的两阶段任务
bool BCLinker::executeAllGroups() {
    logger.log("并发执行所有组的两阶段任务...");

    auto& groups = common.getFileMap();
    std::vector<std::thread> threads;
    std::vector<std::future<bool>> futures;
    std::vector<std::promise<bool>> promises(groups.size());

    // 为每个组创建promise和future
    std::unordered_map<int, std::future<bool>> groupFutures;
    int idx = 0;
    for (int groupId = 0; groupId < groups.size(); groupId++) {
        groupFutures[groupId] = promises[idx].get_future();
        idx++;
    }

    // 启动所有线程
    idx = 0;
    for (int groupId = 0; groupId < groups.size(); groupId++) {
        // 使用lambda表达式捕获this指针
        threads.emplace_back([this, groupId, &promises, idx]() {
            this->processGroupTask(groupId, promises[idx]);
        });
        idx++;
    }

    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }

    logger.log("========================================");
    // 检查结果
    bool allSuccess = true;
    for (int groupId = 0; groupId < groups.size(); groupId++) {
        if (!groupFutures[groupId].get()) {
            allSuccess = false;
            logger.logWarning("组[" + std::to_string(groupId) + "]处理失败");
        }
    }

    return allSuccess;
}

bool BCLinker::enterInWorkDir() {
    // 保存当前工作目录
    std::filesystem::path originalPath = std::filesystem::current_path();
    currenpath = originalPath;

    // 切换到工作目录
    try {
        std::filesystem::current_path(config.workDir);
        logger.logToFile("切换到工作目录: " + config.workDir);
    } catch (const std::filesystem::filesystem_error& e) {
        logger.logError("无法切换到工作目录: " + config.workDir + " - " + e.what());
        return false;
    }

    return true;
}

bool BCLinker::returnCurrenPath() {
    // 切换回原始目录
    try {
        std::filesystem::current_path(currenpath);
        logger.logToFile("切换回原始目录: " + currenpath);
    } catch (const std::filesystem::filesystem_error& e) {
        logger.logError("无法切换回原始目录: " + currenpath + " - " + e.what());
        return false;
    }

    return true;
}

void BCLinker::initphase1() {
    auto& fileMap = common.getFileMap();
    for (int groupId = 0; groupId < fileMap.size(); groupId++) {
        auto promise = std::make_shared<std::promise<void>>();
        phase1Promises[groupId] = promise;
        phase1Futures[groupId] = promise->get_future().share();
    }
}

// 将生成的so文件复制到workSpace的output目录
bool BCLinker::copySoFilesToOutput() {
    std::string outputDir = std::filesystem::path(config.workSpace) / "output";

    try {
        // 确保输出目录存在
        if (!std::filesystem::exists(outputDir)) {
            std::filesystem::create_directories(outputDir);
            std::lock_guard<std::mutex> lock(logMutex);
            logger.logToFile("创建输出目录: " + outputDir);
        }

        // 复制所有libkn_*.so文件
        size_t copiedCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(config.workDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".so") {
                std::string filename = entry.path().filename().string();

                // 检查是否是libkn_*.so文件
                if (filename.find("libkn_") == 0) {
                    std::filesystem::path source = entry.path();
                    std::filesystem::path destination = std::filesystem::path(outputDir) / filename;

                    // 复制文件
                    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
                    copiedCount++;

                    std::lock_guard<std::mutex> lock(logMutex);
                    logger.logToFile("复制文件: " + source.string() + " -> " + destination.string());
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(logMutex);
            logger.log("成功复制 " + std::to_string(copiedCount) + " 个so文件到 " + outputDir);
        }

        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::lock_guard<std::mutex> lock(logMutex);
        logger.logError("复制so文件时出错: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(logMutex);
        logger.logError("复制so文件时发生异常: " + std::string(e.what()));
        return false;
    }
}

// 安全地设置promise
void BCLinker::setPhase1Promise(int groupId) {
    std::lock_guard<std::mutex> lock(phaseMutex);
    auto it = phase1Promises.find(groupId);
    if (it != phase1Promises.end() && it->second) {
        try {
            it->second->set_value();
        } catch (const std::future_error& e) {
            // 如果promise已经被设置，忽略异常
            if (e.code() != std::future_errc::promise_already_satisfied) {
                throw;
            }
        }
    }
}

// 安全地获取future
std::shared_future<void> BCLinker::getPhase1Future(int groupId) {
    std::lock_guard<std::mutex> lock(phaseMutex);
    auto it = phase1Futures.find(groupId);
    if (it != phase1Futures.end()) {
        return it->second;
    }
    // 如果没有找到，返回一个已经完成的future
    std::promise<void> p;
    p.set_value();
    return p.get_future().share();
}