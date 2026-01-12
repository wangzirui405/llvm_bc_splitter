// splitter.cpp
#include "splitter.h"
#include "common.h"
#include "core.h"
#include "linker.h"
#include "logging.h"
#include "verifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueMap.h" // ValueToValueMapTy 的详细定义
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h" // 包含 CloneModule 和 ValueToValueMapTy
#include <algorithm>
#include <cstdlib>
#include <queue>

BCModuleSplitter::BCModuleSplitter(BCCommon &commonRef) : common(commonRef), verifier(commonRef) {
    // 构造符号初始化verifier时传入common
}

// 获取链接属性字符串表示
std::string BCModuleSplitter::getLinkageString(llvm::GlobalValue::LinkageTypes linkage) {
    switch (linkage) {
    case llvm::GlobalValue::ExternalLinkage:
        return "External";
    case llvm::GlobalValue::InternalLinkage:
        return "Internal";
    case llvm::GlobalValue::PrivateLinkage:
        return "Private";
    case llvm::GlobalValue::WeakAnyLinkage:
        return "WeakAny";
    case llvm::GlobalValue::WeakODRLinkage:
        return "WeakODR";
    case llvm::GlobalValue::CommonLinkage:
        return "Common";
    case llvm::GlobalValue::AppendingLinkage:
        return "Appending";
    case llvm::GlobalValue::ExternalWeakLinkage:
        return "ExternalWeak";
    case llvm::GlobalValue::AvailableExternallyLinkage:
        return "AvailableExternally";
    default:
        return "Unknown(" + std::to_string(static_cast<int>(linkage)) + ")";
    }
}

// 获取可见性字符串表示
std::string BCModuleSplitter::getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility) {
    switch (visibility) {
    case llvm::GlobalValue::DefaultVisibility:
        return "Default";
    case llvm::GlobalValue::HiddenVisibility:
        return "Hidden";
    case llvm::GlobalValue::ProtectedVisibility:
        return "Protected";
    default:
        return "Unknown(" + std::to_string(static_cast<int>(visibility)) + ")";
    }
}

void BCModuleSplitter::setCloneMode(bool enable) {
    BCModuleSplitter::currentMode = enable ? CLONE_MODE : MANUAL_MODE;
    logger.log("设置拆分模式: " + std::string(enable ? "CLONE_MODE" : "MANUAL_MODE"));
}

bool BCModuleSplitter::loadBCFile(llvm::StringRef filename) {
    logger.log("加载BC文件: " + filename.str());
    llvm::SMDiagnostic err;
    std::string newfilename = config.workSpace + "output/" + common.renameUnnamedGlobalValues(filename);

    // 创建新的上下文
    llvm::LLVMContext *context = new llvm::LLVMContext();
    common.setContext(context);

    auto module = llvm::parseIRFile(newfilename, err, *context);
    common.setModule(std::move(module));

    if (!common.hasModule()) {
        logger.logError("无法加载BC文件: " + newfilename);
        err.print("BCModuleSplitter", llvm::errs());
        return false;
    }

    logger.log("成功加载模块: " + common.getModule()->getModuleIdentifier());
    return true;
}

void BCModuleSplitter::analyzeFunctions() {
    logger.log("开始分析符号调用关系...");

    if (!common.hasModule()) {
        logger.logError("没有加载模块，无法分析符号");
        return;
    }

    llvm::Module *M = common.getModule();
    auto &globalValueMap = common.getGlobalValueMap();

    // 收集所有全局变量，只为无名全局变量分配序号
    int unnamedSequenceNumber = 0;
    int globalVarCount = 0;
    int functionCount = 0;
    int unnamedGlobalVarCount = 0;
    int unnamedFunctionCount = 0;

    for (llvm::GlobalVariable &GVar : M->globals()) {

        GlobalValueInfo tempGVInfo(&GVar); // 临时对象用于判断是否为无名全局变量
        bool isUnnamed = tempGVInfo.isUnnamed();

        // 只有无名全局变量才分配序号
        int seqNum = isUnnamed ? unnamedSequenceNumber++ : -1;
        if (isUnnamed)
            unnamedGlobalVarCount++;

        auto result = globalValueMap.insert({&GVar, GlobalValueInfo(&GVar, seqNum)});
        if (result.second) {
            globalValuePtrs.insert(&GVar);
            globalVarCount++;
        }
    }
    logger.log("收集到 " + std::to_string(globalVarCount) + " 个全局变量");
    logger.log("其中无名全局变量数量: " + std::to_string(unnamedGlobalVarCount));

    // 收集所有符号，只为无名符号分配序号
    for (llvm::Function &F : *M) {
        if (F.isDeclaration())
            continue;

        GlobalValueInfo tempFInfo(&F); // 临时对象用于判断是否为无名符号
        bool isUnnamed = tempFInfo.isUnnamed();
        // 只有无名符号才分配序号
        int seqNum = isUnnamed ? unnamedSequenceNumber++ : -1;
        if (isUnnamed)
            unnamedFunctionCount++;

        auto result = globalValueMap.insert({&F, GlobalValueInfo(&F, seqNum)});
        if (result.second) {
            globalValuePtrs.insert(&F);
            functionCount++;
        }
    }

    logger.log("收集到 " + std::to_string(functionCount) + " 个符号定义");
    logger.log("其中无名符号数量: " + std::to_string(unnamedFunctionCount));

    // 分析调用关系
    common.analyzeCallRelations();
    common.findCyclicGroups();

    logger.log("分析完成，共分析 " + std::to_string(globalValueMap.size()) + " 个符号");
}

void BCModuleSplitter::printFunctionInfo() {
    auto &globalValueMap = common.getGlobalValueMap();
    logger.logToFile("\n=== 符号调用关系分析 ===");
    for (const auto &pair : globalValueMap) {
        const auto &info = pair.second;

        logger.logToFile(info.getFullInfo());
    }
}

// 更新后的生成分组报告方法，添加最终拆分完成后的bc文件的各个链接属性和可见性
void BCModuleSplitter::generateGroupReport(llvm::StringRef outputPrefix) {
    std::string reportFile = outputPrefix.str() + "_group_report.log";
    std::string pathPre = config.workSpace + "output/";
    auto &globalValueMap = common.getGlobalValueMap();
    std::ofstream report(config.workSpace + "logs/" + reportFile);

    auto &fileMap = common.getFileMap();

    if (!report.is_open()) {
        logger.logError("无法创建分组报告文件: " + reportFile);
        return;
    }

    report << "=== BC文件分组报告 ===" << std::endl;
    report << "总符号数: " << globalValueMap.size() << std::endl;
    report << "总分组数: " << totalGroups << std::endl;
    report << "使用模式: " << (BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE") << std::endl
           << std::endl;

    // 按组统计
    llvm::DenseMap<int, llvm::SmallVector<std::string, 32>> groupGlobalValues;

    int ungroupedCount = 0;

    for (const auto &pair : globalValueMap) {
        const GlobalValueInfo &info = pair.second;
        if (info.groupIndex >= 0) {
            std::string gvInfo =
                info.displayName + " [入度:" + std::to_string(info.inDegree) +
                ", 出度:" + std::to_string(info.outDegree) +
                ", 总分:" + std::to_string(info.inDegree + info.outDegree) +
                (info.isUnnamed() ? ", 无名符号序号:" + std::to_string(info.sequenceNumber) : ", 有名符号") + "]";
            groupGlobalValues[info.groupIndex].push_back(gvInfo);
        } else {
            ungroupedCount++;
        }
    }

    // 定义所有可能的BC文件组
    report << "=== 分组详情 ===" << std::endl;
    int countFileMapIndex = 0;
    auto &globalValuesAllGroups = common.getGlobalValuesAllGroups();

    for (int groupId = 0; groupId < globalValuesAllGroups.size(); groupId++) {
        if (groupGlobalValues[groupId].empty())
            continue;

        std::string groupName =
            countFileMapIndex == 0 ? "_publicGroup.bc" : "_group_" + std::to_string(countFileMapIndex) + ".bc";

        std::string filename = outputPrefix.str() + groupName;

        if (!llvm::sys::fs::exists(pathPre + filename))
            continue;

        GroupInfo *groupInfo = new GroupInfo(countFileMapIndex, filename, false);
        fileMap.push_back(groupInfo);

        if (groupGlobalValues.count(countFileMapIndex)) {
            std::string groupString =
                (countFileMapIndex == 0 ? "=== 公共组: "
                                        : "=== 字符匹配组<" + std::to_string(countFileMapIndex) + ">: ");
            report << groupString << std::endl;
            int count = 0;
            for (llvm::StringRef gvName : groupGlobalValues[countFileMapIndex]) {
                report << "  " << std::to_string(++count) << ". " << gvName.str() << std::endl;
            }
            report << "总计: " << groupGlobalValues[countFileMapIndex].size() << " 个符号" << std::endl << std::endl;
        }
        countFileMapIndex++;
    }

    if (ungroupedCount > 0) {
        report << "=== 未分组符号 ===" << std::endl;
        report << "未分组符号数量: " << ungroupedCount << std::endl;
        int count = 0;
        for (const auto &pair : globalValueMap) {
            if (pair.second.groupIndex < 0) {
                const GlobalValueInfo &info = pair.second;
                report << "  " << std::to_string(++count) << ". " << info.displayName << " [入度:" << info.inDegree
                       << ", 出度:" << info.outDegree << ", 总分:" << (info.inDegree + info.outDegree)
                       << (info.isUnnamed() ? ", 无名符号序号:" + std::to_string(info.sequenceNumber) : ", 有名符号")
                       << "]" << std::endl;
            }
        }
    }

    // 新增：最终拆分完成后的BC文件链接属性和可见性报告
    report << "=== 最终拆分BC文件链接属性和可见性报告 ===" << std::endl << std::endl;
    llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> dependendGroupInfo = common.getGroupDependencies();

    // 检查每个BC文件并报告链接属性和可见性
    for (const auto &bcFileInfo : fileMap) {
        std::string filename = bcFileInfo->bcFile;
        int groupIndex = bcFileInfo->groupId;

        if (!llvm::sys::fs::exists(pathPre + filename)) {
            continue;
        }

        report << "文件: " << filename
               << (groupIndex == 0 ? "(公共组)" : "(字符匹配组<" + std::to_string(groupIndex) + ">)") << std::endl;

        // 加载BC文件进行分析
        llvm::LLVMContext tempContext;
        llvm::SMDiagnostic err;
        auto testModule = llvm::parseIRFile(pathPre + filename, err, tempContext);

        if (!testModule) {
            report << "  错误: 无法加载文件进行分析" << std::endl;
            continue;
        }

        // 统计信息
        AttributeStats globalVariableStatistics = AttributeStats();
        AttributeStats functionStatistics = AttributeStats();

        report << "  符号分析:" << std::endl;
        int totalGV = 0;
        int unnamedFIndex = 0;

        for (auto &GVar : testModule->globals()) {
            if (GVar.hasInitializer()) {
                totalGV++;
                GlobalValueInfo tempInfo(&GVar, unnamedFIndex);
                report << "    " << totalGV << ", " << tempInfo.getBriefInfo() << std::endl;

                // 统计链接属性
                globalVariableStatistics.addInfo(tempInfo);
            }
        }

        for (llvm::Function &F : *testModule) {
            if (!F.isDeclaration()) {
                totalGV++;
                GlobalValueInfo tempInfo(&F, unnamedFIndex);
                report << "    " << totalGV << ", " << tempInfo.getBriefInfo() << std::endl;

                // 统计链接属性
                functionStatistics.addInfo(tempInfo);

                if (tempInfo.displayName == "Konan_cxa_demangle")
                    fileMap[groupIndex]->hasKonanCxaDemangle = true;
            }
        }
        for (int dependGroupIndex : dependendGroupInfo[groupIndex]) {
            report << "  组[" << groupIndex << "]依赖组[" << dependGroupIndex << "]" << std::endl;
            fileMap[groupIndex]->dependencies.insert(dependGroupIndex);
        }

        report << "  总计: " << totalGV << " 个符号" << std::endl;

        // 验证模块
        std::string verifyResult;
        llvm::raw_string_ostream rso(verifyResult);
        bool moduleValid = !verifyModule(*testModule, &rso);
        report << "  模块验证: " << (moduleValid ? "通过" : "失败") << std::endl;

        if (!moduleValid) {
            report << "  验证错误: " << rso.str() << std::endl;
        }

        report << std::endl;
    }

    // 添加所有BC文件的总体统计
    report << "=== 所有BC文件总体统计 ===" << std::endl;

    int totalBCFiles = 0;
    int validBCFiles = 0;
    llvm::SmallVector<std::string> existingFiles;

    // 检查文件是否存在并统计
    for (const auto &bcFileInfo : fileMap) {
        std::string filename = bcFileInfo->bcFile;
        if (llvm::sys::fs::exists(pathPre + filename)) {
            totalBCFiles++;
            existingFiles.push_back(filename);
        }
    }

    report << "生成的BC文件总数: " << totalBCFiles << std::endl;
    report << "存在的BC文件列表:" << std::endl;
    for (const auto &file : existingFiles) {
        report << "  " << file << std::endl;
    }

    report << std::endl << "=== 报告生成完成 ===" << std::endl;
    report << "报告文件: " << reportFile << std::endl;
    report << "生成时间: " << __DATE__ << " " << __TIME__ << std::endl;

    report.close();
    logger.log("分组报告已生成: " + reportFile);

    // 同时在日志中输出关键统计信息
    logger.log("最终拆分完成: 共生成 " + std::to_string(totalBCFiles) + " 个BC文件");
    for (const auto &file : existingFiles) {
        logger.log("  - " + file);
    }
}

void BCModuleSplitter::getGlobalValueGroup(int groupIndex) {
    llvm::DenseSet<llvm::GlobalValue *> group;
    auto &globalValueMap = common.getGlobalValueMap();

    const auto packageString = config.packageStrings[groupIndex - 1];

    // 1. 遍历globalValueMap，找出displayName包含packageString的GlobalValue
    for (auto &[GV, info] : globalValueMap) {
        if (!GV || info.preGroupIndex == 0) {
            continue;
        }

        // 检查displayName是否包含packageString
        if (info.displayName.find(packageString) != std::string::npos)
            group.insert(GV);
    }

    // 2. 通过getOriginWithOutDegreeGlobalValues进行扩展
    group = getOriginWithOutDegreeGlobalValues(groupIndex, group);

    // 3. 通过getStronglyConnectedComponent进行补充
    group = getStronglyConnectedComponent(groupIndex, group);

    // 4. 对group中的每个成员标记preGroupIndex和isPreProcessed
    for (llvm::GlobalValue *GV : group) {
        if (GV && globalValueMap.count(GV)) {
            globalValueMap[GV].isPreProcessed = true;
        }
    }
}

llvm::DenseSet<llvm::GlobalValue *>
BCModuleSplitter::getOriginWithOutDegreeGlobalValues(int preGroupId,
                                                     const llvm::DenseSet<llvm::GlobalValue *> &originGVs) {
    llvm::DenseSet<llvm::GlobalValue *> completeSet;
    llvm::SmallVector<llvm::GlobalValue *, 32> toProcess;
    auto &globalValueMap = common.getGlobalValueMap();

    // 初始添加所有符号
    for (llvm::GlobalValue *GV : originGVs) {
        if (!GV || globalValueMap[GV].preGroupIndex == 0)
            continue;
        completeSet.insert(GV);
        toProcess.push_back(GV);
        globalValueMap[GV].preGroupIndex = globalValueMap[GV].isPreProcessed ? 0 : preGroupId;
    }

    if (toProcess.empty()) {
        return originGVs;
    }

    // 广度优先遍历所有出度符号
    size_t frontIndex = 0;
    while (frontIndex < toProcess.size()) {
        llvm::GlobalValue *current = toProcess[frontIndex];
        frontIndex++;

        // 获取当前符号的所有出度符号
        const GlobalValueInfo &info = globalValueMap[current];
        for (llvm::GlobalValue *called : info.calleds) {
            if (!called || globalValueMap[called].preGroupIndex == 0)
                continue;

            if (globalValueMap[called].isPreProcessed) {
                completeSet.insert(called);
                toProcess.push_back(called);
                globalValueMap[called].preGroupIndex = 0;
                continue;
            }

            // 如果符号不在集合中，添加到集合和队列
            if (!completeSet.contains(called)) {
                completeSet.insert(called);
                toProcess.push_back(called);
                globalValueMap[called].preGroupIndex = info.preGroupIndex;
            }
        }
    }

    return completeSet;
}

llvm::DenseSet<llvm::GlobalValue *>
BCModuleSplitter::getStronglyConnectedComponent(int preGroupId, const llvm::DenseSet<llvm::GlobalValue *> &originGVs) {
    llvm::DenseSet<llvm::GlobalValue *> completeSet;

    // 使用 SmallVector 模拟队列
    llvm::SmallVector<llvm::GlobalValue *, 32> toProcess;

    auto &globalValueMap = common.getGlobalValueMap();

    // 初始添加符号
    for (llvm::GlobalValue *GV : originGVs) {
        if (!GV || globalValueMap[GV].preGroupIndex == 0)
            continue;
        completeSet.insert(GV);
        toProcess.push_back(GV);
        globalValueMap[GV].preGroupIndex = globalValueMap[GV].isPreProcessed ? 0 : preGroupId;
    }

    if (toProcess.empty()) {
        return originGVs;
    }

    // 广度优先遍历所有依赖循环
    size_t frontIndex = 0; // 模拟队列的队首索引
    while (frontIndex < toProcess.size()) {
        llvm::GlobalValue *current = toProcess[frontIndex];
        frontIndex++;

        // 获取当前符号的所有循环依赖组
        llvm::DenseSet<llvm::GlobalValue *> cyclicGVs = common.getCyclicGroupsContainingGlobalValue(current);

        for (llvm::GlobalValue *cyc : cyclicGVs) {
            if (!cyc || globalValueMap[cyc].preGroupIndex == 0)
                continue;

            if (globalValueMap[cyc].isPreProcessed) {
                completeSet.insert(cyc);
                toProcess.push_back(cyc);
                globalValueMap[cyc].preGroupIndex = 0;
                continue;
            }

            // 如果符号不在集合中，添加到集合和队列
            if (!completeSet.contains(cyc)) {
                completeSet.insert(cyc);
                toProcess.push_back(cyc);
                globalValueMap[cyc].preGroupIndex = globalValueMap[current].preGroupIndex;
            }
        }
    }

    return completeSet;
}

// 新增：统一的BC文件创建入口，支持两种模式
bool BCModuleSplitter::createBCFile(const llvm::DenseSet<llvm::GlobalValue *> &group, llvm::StringRef filename,
                                    int groupIndex) {
    if (BCModuleSplitter::currentMode == CLONE_MODE) {
        return createBCFileWithClone(group, filename, groupIndex);
    } else {
        logger.log("已不支持此功能...");
        return false;
    }
}

// 修改后的拆分方法 - 按照指定数量范围分组
void BCModuleSplitter::splitBCFiles(llvm::StringRef outputPrefix) {
    logger.log("\n开始拆分BC文件...");
    logger.log("当前模式: " + std::string(BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE"));

    int fileCount = 0;
    auto &globalValueMap = common.getGlobalValueMap();
    auto &globalValuesAllGroups = common.getGlobalValuesAllGroups();

    llvm::DenseSet<llvm::GlobalValue *> publicGroup;
    globalValuesAllGroups.push_back(publicGroup);

    // 1. 处理第1到第n组（对应packageStrings）
    for (size_t i = 0; i < config.packageStrings.size(); ++i) {
        int groupIndex = i + 1;          // 组号从1开始
        getGlobalValueGroup(groupIndex); // 获取当前包对应的组
        llvm::DenseSet<llvm::GlobalValue *> group;
        globalValuesAllGroups.push_back(group);
    }

    // 2. 收集GlobalValue
    for (auto &[GV, info] : globalValueMap) {
        if (!GV) {
            continue;
        }

        if (info.isPreProcessed) {
            globalValuesAllGroups[info.preGroupIndex].insert(GV);
        } else {
            globalValuesAllGroups[0].insert(GV);
            info.preGroupIndex = 0;
            info.isPreProcessed = true;
        }
    }

    // 步骤4: 按照指定数量范围分组
    logger.log("根据分组生成bc文件...");

    // 持续分组直到所有符号都处理完
    for (int groupId = 0; groupId < globalValuesAllGroups.size(); groupId++) {
        llvm::DenseSet<llvm::GlobalValue *> completeGroup = globalValuesAllGroups[groupId];

        if (completeGroup.empty())
            continue;
        logger.log("处理组 {" + std::to_string(fileCount) + "} 包含 " + std::to_string(completeGroup.size()) +
                   " 个符号");

        // 创建BC文件
        std::string filename =
            outputPrefix.str() + (fileCount == 0 ? "_publicGroup.bc" : "_group_" + std::to_string(fileCount) + ".bc");

        if (createBCFile(completeGroup, filename, fileCount)) {
            // 验证并修复生成的BC文件
            bool verified = false;
            if (BCModuleSplitter::currentMode == CLONE_MODE) {
                if (quickValidateBCFile(filename)) {
                    verified = true;
                    logger.log("✓ Clone模式分组BC文件验证通过: " + filename);
                }
            } else {
                logger.log("已不支持此功能...");
            }

            if (!verified) {
                logger.logError("✗ BC文件验证失败: " + filename);
            }

            fileCount++;
        } else {
            logger.logError("✗ 创建BC文件失败: " + filename);
        }
    }

    totalGroups = fileCount;

    logger.log("\n=== 拆分完成 ===");
    logger.log("共生成 " + std::to_string(fileCount) + " 个分组BC文件");
    logger.log("使用模式: " + std::string(BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE"));

    // 统计处理情况
    int processedCount = 0;
    for (const auto &pair : globalValueMap) {
        if (pair.second.isProcessed) {
            processedCount++;
        }
    }
    logger.log("已处理 " + std::to_string(processedCount) + " / " + std::to_string(globalValueMap.size()) + " 个符号");

    if (processedCount < globalValueMap.size()) {
        logger.logWarning("警告: 有 " + std::to_string(globalValueMap.size() - processedCount) + " 个符号未被处理");

        // 显示所有未处理的符号 - 完整打印
        logger.logToFile("未处理符号完整列表:");
        int unprocessedCount = 0;
        for (const auto &pair : globalValueMap) {
            if (!pair.second.isProcessed) {
                const GlobalValueInfo &info = pair.second;
                logger.logToFile(info.getFullInfo());
            }
        }
        logger.logToFile("未处理符号统计: 共 " + std::to_string(unprocessedCount) + " 个符号");
    }
}

// 新增：使用LLVM CloneModule创建BC文件
bool BCModuleSplitter::createBCFileWithClone(const llvm::DenseSet<llvm::GlobalValue *> &group, llvm::StringRef filename,
                                             int groupIndex) {
    logger.logToFile("使用Clone模式创建BC文件: " + filename.str() + " (组 " + std::to_string(groupIndex) + ")");

    // 使用ValueToValueMapTy进行克隆
    llvm::ValueToValueMapTy vmap;
    llvm::DenseSet<llvm::GlobalValue *> newExternalGroup;
    llvm::DenseSet<llvm::GlobalValue *> newGroup;
    llvm::Module *M = common.getModule();
    auto &globalValueMap = common.getGlobalValueMap();
    auto newM = CloneModule(*M, vmap);

    if (!newM) {
        logger.logError("CloneModule失败: " + filename.str());
        return false;
    }

    // 设置模块名称
    newM->setModuleIdentifier("cloned_group_" + std::to_string(groupIndex));

    for (llvm::GlobalValue *orig : group) {
        // 查找原始符号在vmap中对应的新符号
        auto it = vmap.find(orig);
        if (it != vmap.end()) {
            // 确保映射到的是GlobalValue类型
            if (llvm::GlobalObject *newGV = llvm::dyn_cast<llvm::GlobalObject>(it->second)) {
                if (!GlobalValueInfo::areAllCallersInGroup(orig, group, globalValueMap)) {
                    newExternalGroup.insert(newGV);
                    const auto &info = globalValueMap[orig];
                    logger.logToFile("需要使用外部链接: " + info.displayName);
                }
                newGroup.insert(newGV);
            } else {
                // 处理错误：映射到的不是GlobalValue
                logger.logError("Warning: Value mapped is not a GlobalValue for " + orig->getName().str());
            }
        } else {
            // 处理错误：原始符号没有在vmap中找到映射
            logger.logError("Warning: No mapping found for GlobalValue " + orig->getName().str());
        }
    }
    if (group.size() != newGroup.size()) {
        logger.logError("CloneModule 映射前后大小不匹配");
        return false;
    }
    // 处理符号：保留组内符号定义，其他转为声明
    processClonedModuleGlobalValues(*newM, newGroup, newExternalGroup);

    // 标记原始符号已处理
    for (llvm::GlobalValue *orig : group) {
        if (globalValueMap.count(orig)) {
            globalValueMap[orig].groupIndex = groupIndex;
            globalValueMap[orig].isProcessed = true;
        }
    }

    logger.logToFile("Clone模式完成: " + filename.str() + " (包含 " + std::to_string(group.size()) + " 个符号)");

    if (!runOptimizationAndVerify(*newM)) {
        logger.logError("✗ 编译优化失败");
        return false;
    }

    return common.writeBitcodeSafely(*newM, filename);
}

// 新增：处理克隆模块中的符号
void BCModuleSplitter::processClonedModuleGlobalValues(llvm::Module &M,
                                                       const llvm::DenseSet<llvm::GlobalValue *> &targetGroup,
                                                       const llvm::DenseSet<llvm::GlobalValue *> &externalGroup) {
    // 处理所有符号
    llvm::DenseSet<llvm::GlobalValue *> globalValuesToProcess;
    for (llvm::Function &F : M) {
        globalValuesToProcess.insert(&F);
    }
    for (llvm::GlobalVariable &GVar : M.globals()) {
        globalValuesToProcess.insert(&GVar);
    }

    for (llvm::GlobalValue *GV : globalValuesToProcess) {
        if (auto *F = llvm::dyn_cast<llvm::Function>(GV)) {
            if (targetGroup.find(F) == targetGroup.end()) {
                // 非目标符号：转为声明
                if (!F->isDeclaration()) {
                    F->deleteBody();
                    F->setLinkage(llvm::GlobalValue::ExternalLinkage);
                    F->setVisibility(llvm::GlobalValue::DefaultVisibility);
                    F->setDSOLocal(false);
                    // logger.logToFile("非目标符号转为声明: " + F->getName().str());
                }
            } else {
                // 目标符号：保留为定义，设置合适的链接属性
                if (externalGroup.find(F) != externalGroup.end() &&
                    (F->getLinkage() == llvm::GlobalValue::InternalLinkage ||
                     F->getLinkage() == llvm::GlobalValue::PrivateLinkage)) {
                    F->setLinkage(llvm::GlobalValue::ExternalLinkage);
                    F->setVisibility(llvm::GlobalValue::DefaultVisibility);
                    // logger.logToFile("外部符号链接调整: " + F->getName().str() + " -> External");
                }
            }
        } else if (auto *GVar = llvm::dyn_cast<llvm::GlobalVariable>(GV)) {
            if (targetGroup.find(GVar) == targetGroup.end()) {
                // 非目标符号：转为声明
                GVar->setDSOLocal(false);
                if (GVar->hasInitializer()) {
                    GVar->setInitializer(nullptr);
                }
                // 设置external链接和默认可见性
                GVar->setLinkage(llvm::GlobalValue::ExternalLinkage);
                GVar->setVisibility(llvm::GlobalValue::DefaultVisibility);
                // logger.logToFile("非目标符号转为声明: " + GVar->getName().str());
            } else {
                // 目标符号：保留为定义，设置合适的链接属性
                if (externalGroup.find(GVar) != externalGroup.end() &&
                    (GVar->getLinkage() == llvm::GlobalValue::InternalLinkage ||
                     GVar->getLinkage() == llvm::GlobalValue::PrivateLinkage)) {
                    // 设置external链接和默认可见性
                    GVar->setLinkage(llvm::GlobalValue::ExternalLinkage);
                    GVar->setVisibility(llvm::GlobalValue::DefaultVisibility);
                    // logger.logToFile("外部符号链接调整: " + GVar->getName().str() + " -> External");
                }
            }
        }
    }
}

// 在 splitter.cpp 中添加这些方法的实现
bool BCModuleSplitter::runOptimizationAndVerify(llvm::Module &M) {
    // 1. 运行优化
    if (!optimizer.runOptimization(M)) {
        logger.logToFile("✗ 运行优化失败");
        return false;
    }

    // 2. 验证优化后的模块
    std::string ErrorInfo;
    llvm::raw_string_ostream OS(ErrorInfo);
    if (llvm::verifyModule(M, &OS)) {
        logger.logToFile("✗ 编译优化后, 验证失败: " + ErrorInfo);
        return false;
    }

    logger.log("✓ 编译优化已完成");

    return true;
}

// 验证所有BC文件
void BCModuleSplitter::validateAllBCFiles(llvm::StringRef outputPrefix) {
    logger.log("\n=== 通过 BCModuleSplitter 验证所有BC文件 ===");
    verifier.validateAllBCFiles(outputPrefix, currentMode == CLONE_MODE);
}

// 验证并修复单个BC文件
bool BCModuleSplitter::verifyAndFixBCFile(llvm::StringRef filename,
                                          const llvm::DenseSet<llvm::GlobalValue *> &expectedGroup) {
    return verifier.verifyAndFixBCFile(filename, expectedGroup);
}

// 快速验证BC文件
bool BCModuleSplitter::quickValidateBCFile(llvm::StringRef filename) { return verifier.quickValidateBCFile(filename); }

// 分析BC文件内容
void BCModuleSplitter::analyzeBCFileContent(llvm::StringRef filename) { verifier.analyzeBCFileContent(filename); }