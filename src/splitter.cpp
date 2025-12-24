// splitter.cpp
#include "splitter.h"
#include "common.h"
#include "logging.h"
#include "verifier.h"
#include "linker.h"
#include "core.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/Utils/Cloning.h"  // 包含 CloneModule 和 ValueToValueMapTy
#include "llvm/IR/ValueMap.h"              // ValueToValueMapTy 的详细定义
#include <algorithm>
#include <queue>
#include <cstdlib>

BCModuleSplitter::BCModuleSplitter(BCCommon& commonRef) : common(commonRef),verifier(commonRef) {
    // 构造函数初始化verifier时传入common
}

// 获取链接属性字符串表示
std::string BCModuleSplitter::getLinkageString(llvm::GlobalValue::LinkageTypes linkage) {
    switch (linkage) {
        case llvm::GlobalValue::ExternalLinkage: return "External";
        case llvm::GlobalValue::InternalLinkage: return "Internal";
        case llvm::GlobalValue::PrivateLinkage: return "Private";
        case llvm::GlobalValue::WeakAnyLinkage: return "WeakAny";
        case llvm::GlobalValue::WeakODRLinkage: return "WeakODR";
        case llvm::GlobalValue::CommonLinkage: return "Common";
        case llvm::GlobalValue::AppendingLinkage: return "Appending";
        case llvm::GlobalValue::ExternalWeakLinkage: return "ExternalWeak";
        case llvm::GlobalValue::AvailableExternallyLinkage: return "AvailableExternally";
        default: return "Unknown(" + std::to_string(static_cast<int>(linkage)) + ")";
    }
}

// 获取可见性字符串表示
std::string BCModuleSplitter::getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility) {
    switch (visibility) {
        case llvm::GlobalValue::DefaultVisibility: return "Default";
        case llvm::GlobalValue::HiddenVisibility: return "Hidden";
        case llvm::GlobalValue::ProtectedVisibility: return "Protected";
        default: return "Unknown(" + std::to_string(static_cast<int>(visibility)) + ")";
    }
}

void BCModuleSplitter::setCloneMode(bool enable) {
    BCModuleSplitter::currentMode = enable ? CLONE_MODE : MANUAL_MODE;
    logger.log("设置拆分模式: " + std::string(enable ? "CLONE_MODE" : "MANUAL_MODE"));
}

bool BCModuleSplitter::loadBCFile(const std::string& filename) {
    logger.log("加载BC文件: " + filename);
    llvm::SMDiagnostic err;
    std::string newfilename = config.workSpace + "output/" + common.renameUnnamedGlobals(filename);

    // 创建新的上下文
    llvm::LLVMContext* context = new llvm::LLVMContext();
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
    logger.log("开始分析函数调用关系...");

    if (!common.hasModule()) {
        logger.logError("没有加载模块，无法分析函数");
        return;
    }

    llvm::Module* module = common.getModule();
    auto& functionMap = common.getFunctionMap();
    auto& globalVariableMap = common.getGlobalVariableMap();

    // 收集所有全局变量，只为无名全局变量分配序号
    int unnamedSequenceNumber = 0;
    for (llvm::GlobalVariable& GV : module->globals()) {

        GlobalVariableInfo tempGVInfo(&GV);  // 临时对象用于判断是否为无名全局变量
        bool isUnnamed = tempGVInfo.isUnnamed();

        // 只有无名全局变量才分配序号
        int seqNum = isUnnamed ? unnamedSequenceNumber++ : -1;

        auto result = globalVariableMap.emplace(&GV, GlobalVariableInfo(&GV, seqNum));
        if (result.second) {
            globalVariablePtrs.push_back(&GV);
        }
    }
    logger.log("收集到 " + std::to_string(globalVariableMap.size()) + " 个全局变量");
    logger.log("其中无名全局变量数量: " + std::to_string(unnamedSequenceNumber));

    const int unnamedGVNumber = unnamedSequenceNumber;
    // 收集所有函数，只为无名函数分配序号
    for (llvm::Function& func : *module) {
        if (func.isDeclaration()) continue;

        FunctionInfo tempFInfo(&func);  // 临时对象用于判断是否为无名函数
        bool isUnnamed = tempFInfo.isUnnamed();
        // 只有无名函数才分配序号
        int seqNum = isUnnamed ? unnamedSequenceNumber++ : -1;

        auto result = functionMap.emplace(&func, FunctionInfo(&func, seqNum));
        if (result.second) {
            functionPtrs.push_back(&func);
        }
    }

    logger.log("收集到 " + std::to_string(functionMap.size()) + " 个函数定义");
    logger.log("其中无名函数数量: " + std::to_string(unnamedSequenceNumber - unnamedGVNumber));

    // 分析调用关系
    common.analyzeCallRelations();

    std::unordered_set<llvm::Function*> internalFunctionsFromGlobals = collectInternalFunctionsFromGlobals();
    for (llvm::Function* func : internalFunctionsFromGlobals) {
        functionMap[func].isReferencedByGlobals = true;
    }
    logger.logToFile("收集到 " + std::to_string(internalFunctionsFromGlobals.size()) + " 个函数被全局变量引用");

    common.findCyclicGroups();

    //FunctionInfo::analyzeIndirectCallPatterns(functionMap);

    logger.log("分析完成，共分析 " + std::to_string(functionMap.size()) + " 个函数");
}

void BCModuleSplitter::printFunctionInfo() {
    auto& functionMap = common.getFunctionMap();
    logger.logToFile("\n=== 函数调用关系分析 ===");
    for (const auto& pair : functionMap) {
        const FunctionInfo& info = pair.second;

        logger.logToFile(info.getFullInfo());
    }
}

// 统计全局变量中引用的internal函数
std::unordered_set<llvm::Function*> BCModuleSplitter::collectInternalFunctionsFromGlobals() {
    std::unordered_set<llvm::Function*> internalFuncsInGlobals;
    llvm::Module* module = common.getModule();

    // 首先，收集所有internal函数
    std::unordered_set<llvm::Function*> internalFuncs;
    for (llvm::Function& F : *module) {
        if (F.isDeclaration()) continue;
        if (F.hasInternalLinkage() || F.hasPrivateLinkage()) {
            internalFuncs.insert(&F);
        }
    }

    // 遍历全局变量
    for (llvm::GlobalVariable& GV : module->globals()) {
        if (GV.hasInitializer()) {
            llvm::Constant* initializer = GV.getInitializer();
            std::unordered_set<llvm::Function*> funcsInInitializer;
            if (auto* GA = llvm::dyn_cast<llvm::GlobalAlias>(initializer)) {
                if (auto* aliasee = GA->getAliasee()) {
                    collectFunctionsFromConstant(aliasee, funcsInInitializer);
                }
            } else {
                collectFunctionsFromConstant(initializer, funcsInInitializer);
            }

            // 检查这些函数是否是internal函数
            for (llvm::Function* F : funcsInInitializer) {
                if (internalFuncs.count(F)) {
                    internalFuncsInGlobals.insert(F);
                }
            }
        }
    }

    return internalFuncsInGlobals;
}

void BCModuleSplitter::collectFunctionsFromConstant(llvm::Constant* C, std::unordered_set<llvm::Function*>& funcSet) {
    if (auto* F = llvm::dyn_cast<llvm::Function>(C)) {
        funcSet.insert(F);
    } else if (auto* CE = llvm::dyn_cast<llvm::ConstantExpr>(C)) {
        // 递归处理常量表达式的操作数
        for (unsigned i = 0, e = CE->getNumOperands(); i < e; ++i) {
            collectFunctionsFromConstant(llvm::cast<llvm::Constant>(CE->getOperand(i)), funcSet);
        }
    } else if (auto* CA = llvm::dyn_cast<llvm::ConstantArray>(C)) {
        for (unsigned i = 0, e = CA->getNumOperands(); i < e; ++i) {
            collectFunctionsFromConstant(CA->getOperand(i), funcSet);
        }
    } else if (auto* CAG = llvm::dyn_cast<llvm::ConstantAggregate>(C)) {
        for (unsigned i = 0, e = CAG->getNumOperands(); i < e; ++i) {
            collectFunctionsFromConstant(CAG->getOperand(i), funcSet);
        }
    } else if (auto* CS = llvm::dyn_cast<llvm::ConstantStruct>(C)) {
        for (unsigned i = 0, e = CS->getNumOperands(); i < e; ++i) {
            collectFunctionsFromConstant(CS->getOperand(i), funcSet);
        }
    } else if (auto* CV = llvm::dyn_cast<llvm::ConstantVector>(C)) {
        for (unsigned i = 0, e = CV->getNumOperands(); i < e; ++i) {
            collectFunctionsFromConstant(CV->getOperand(i), funcSet);
        }
    } else if (auto* BA = llvm::dyn_cast<llvm::BlockAddress>(C)) {
        llvm::Function* func = BA->getFunction();
        funcSet.insert(func);
    } else if (auto* CDS = llvm::dyn_cast<llvm::ConstantDataSequential>(C)) {
        return;
    }
    // 其他常量类型，如ConstantInt、ConstantFP等，不会引用函数，所以忽略
}

//遍历internal constant并记录调用者
void BCModuleSplitter::analyzeInternalConstants() {
    logger.log("开始分析全局变量被调用情况...");
    std::string reportFile = "analyzeInternalConstants.txt";
    auto& globalVariableMap = common.getGlobalVariableMap();
    const auto& functionMap = common.getFunctionMap();
    std::unordered_set<llvm::Function*> funcsUsingConstant;

    if (functionMap.empty()) {
        logger.logError("未初始化");
    }
    std::ofstream report(config.workSpace + "logs/" + reportFile);
    if (!report.is_open()) {
        logger.logError("无法创建分组报告文件: " + reportFile);
        return;
    }

    for (auto& pair : globalVariableMap) {
        GlobalVariableInfo& gv = pair.second;
        std::set<llvm::User*> visited;
        if (gv.isInternal && gv.isConstant) {
            for (llvm::User *U : pair.first->users()) {
                // 递归查找最终的使用指令
                findAndRecordUsage(U, gv, visited);
            }
        }
        const GlobalVariableInfo& info = pair.second;
        if (!info.callers.empty()) {
            report << "「" << info.displayName << "」被调用: " << std::endl;
            for (llvm::Function* f : info.callers) {
                funcsUsingConstant.insert(f);
                report << "  " << functionMap.find(f)->second.getBriefInfo() << std::endl;
            }
        }
    }

    report << "=== 涉及的函数列表： ===" << std::endl;
    for (llvm::Function* F : funcsUsingConstant) {
        report << "  " << functionMap.find(F)->second.getBriefInfo() << std::endl;
    }

    report.close();
    logger.log("全局变量被调用情况报告已生成: " + reportFile);
}

// 递归查找并记录使用位置
void BCModuleSplitter::findAndRecordUsage(llvm::User *user,
    GlobalVariableInfo &info, std::set<llvm::User*> visited) {

    // 检查空指针
    if (!user) return;

    // 防止循环引用
    if (visited.find(user) != visited.end()) {
        return;
    }
    visited.insert(user);

    // 如果是函数，直接记录
    if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(user)) {
        info.callers.insert(F);
        return;
    }

    // 如果是指令，记录其所属函数和使用点
    if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(user)) {
        if (llvm::Function *F = I->getFunction()) {
            info.callers.insert(F);
        }
        return;
    }

    // 如果是常量表达式，继续追踪其使用者
    if (llvm::Constant *C = llvm::dyn_cast<llvm::Constant>(user)) {
        for (llvm::User *U : C->users()) {
            findAndRecordUsage(U, info, visited);
        }
    }
}

// 更新后的生成分组报告方法，添加最终拆分完成后的bc文件的各个链接属性和可见性
void BCModuleSplitter::generateGroupReport(const std::string& outputPrefix) {
    std::string reportFile = outputPrefix + "_group_report.txt";
    const std::string pathPre = config.workSpace + "output/";
    auto& functionMap = common.getFunctionMap();
    auto& globalVariableMap = common.getGlobalVariableMap();
    std::ofstream report(config.workSpace + "logs/" + reportFile);

    auto& fileMap = common.getFileMap();

    if (!report.is_open()) {
        logger.logError("无法创建分组报告文件: " + reportFile);
        return;
    }

    report << "=== BC文件分组报告 ===" << std::endl;
    report << "总函数数: " << functionMap.size() << std::endl;
    report << "总分组数: " << totalGroups << std::endl;
    report << "使用模式: " << (BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE") << std::endl << std::endl;

    // 按组统计
    std::unordered_map<int,  std::vector<std::string>> groupFunctions;

    int ungroupedCount = 0;

    for (const auto& pair : functionMap) {
        const FunctionInfo& info = pair.second;
        if (info.groupIndex >= 0) {
            std::string funcInfo = info.displayName +
                " [入度:" + std::to_string(info.inDegree) +
                ", 出度:" + std::to_string(info.outDegree) +
                ", 总分:" + std::to_string(info.inDegree + info.outDegree) +
                (info.isUnnamed() ? ", 无名函数序号:" + std::to_string(info.sequenceNumber) : ", 有名函数") + "]";
            groupFunctions[info.groupIndex].push_back(funcInfo);
        } else {
            ungroupedCount++;
        }
    }

    // 定义所有可能的BC文件组
     std::vector<std::pair<std::string, int>> bcFiles = {
        {"_group_globals.bc", 0},
        {"_group_external.bc", 1},
        {"_group_high_in_degree.bc", 2},
        {"_group_isolated.bc", 3}
    };

    for (int i = 0; i <= 9; i++ ) {
        if (i >= 4) bcFiles.emplace_back("_group_" + std::to_string(i) + ".bc", i);
        std::string filename = outputPrefix + bcFiles[i].first;

        if (!llvm::sys::fs::exists(pathPre + filename)) continue;

        GroupInfo* groupInfo = new GroupInfo(i, filename, false);
        fileMap.push_back(groupInfo);
    }

    report << "=== 分组详情 ===" << std::endl;

    // 全局变量组（组0）
    report << "组 0 (全局变量):" << std::endl;

    int globalCount = 0;
    int unnamedGIndex = 0;
    for (const auto& pair : globalVariableMap) {
        const GlobalVariableInfo& gInfo = pair.second;
        report << "  " << std::to_string(++globalCount) << ", " << gInfo.getBriefInfo() << std::endl;
    }
    report << "总计: " << globalCount << " 个全局变量" << std::endl << std::endl;

    // 外部链接函数组（组1）
    if (groupFunctions.count(1)) {
        report << "组 1 (外部链接函数组):" << std::endl;
        int count = 0;
        for (const std::string& funcName : groupFunctions[1]) {
            report << "  " << std::to_string(++count) << ", " << funcName << std::endl;
        }
        report << "总计: " << groupFunctions[1].size() << " 个函数" << std::endl << std::endl;
    }

    // 高入度函数组（组2）
    if (groupFunctions.count(2)) {
        report << "组 2 (高入度函数):" << std::endl;
        int count = 0;
        for (const std::string& funcName : groupFunctions[2]) {
            report << "  " << std::to_string(++count) << ". " << funcName << std::endl;
        }
        report << "总计: " << groupFunctions[2].size() << " 个函数" << std::endl << std::endl;
    }

    // 孤立函数组（组3）
    if (groupFunctions.count(3)) {
        report << "组 3 (孤立函数):" << std::endl;
        int count = 0;
        for (const std::string& funcName : groupFunctions[3]) {
            report << "  " << std::to_string(++count) << ". " << funcName << std::endl;
        }
        report << "总计: " << groupFunctions[3].size() << " 个函数" << std::endl << std::endl;
    }

    // 新的分组策略（组4-9）
     std::vector<std::string> groupDescriptions = {
        "前200个函数",
        "201-1600个函数",
        "1601-4000个函数",
        "4001-8000个函数",
        "8001-20000个函数",
        "20001-剩余所有函数"
    };

    for (int i = 4; i <= 9; i++) {
        if (groupFunctions.count(i)) {
            report << "组 " << i << " (" << groupDescriptions[i-4] << "):" << std::endl;
            int count = 0;
            for (const std::string& funcName : groupFunctions[i]) {
                report << "  " << std::to_string(++count) << ". " << funcName << std::endl;
            }
            report << "总计: " << groupFunctions[i].size() << " 个函数" << std::endl << std::endl;
        }
    }

    if (ungroupedCount > 0) {
        report << "=== 未分组函数 ===" << std::endl;
        report << "未分组函数数量: " << ungroupedCount << std::endl;
        int count = 0;
        for (const auto& pair : functionMap) {
            if (pair.second.groupIndex < 0) {
                const FunctionInfo& info = pair.second;
                report << "  " << std::to_string(++count) << ". " << info.displayName <<
                    " [入度:" << info.inDegree <<
                    ", 出度:" << info.outDegree <<
                    ", 总分:" << (info.inDegree + info.outDegree) <<
                    (info.isUnnamed() ? ", 无名函数序号:" + std::to_string(info.sequenceNumber) : ", 有名函数") << "]" << std::endl;
            }
        }
    }

    // 新增：最终拆分完成后的BC文件链接属性和可见性报告
    report << "=== 最终拆分BC文件链接属性和可见性报告 ===" << std::endl << std::endl;
    // 检查每个BC文件并报告链接属性和可见性
    for (const auto& bcFileInfo : fileMap) {
        std::string filename = bcFileInfo->bcFile;
        int groupIndex = bcFileInfo->groupId;

        if (!llvm::sys::fs::exists(pathPre + filename)) {
            continue;
        }

        report << "文件: " << filename << " (组 " << groupIndex << ")" << std::endl;

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
        std::vector<std::set<int>> dependendGroupInfo = common.getGroupDependencies();

        // 分析全局变量（仅对组0）
        if (groupIndex == 0) {
            report << "  全局变量分析:" << std::endl;
            int globalVarCount = 0;
            int unnamedGVIndex = 0;
            for (auto& global : testModule->globals()) {
                globalVarCount++;
                GlobalVariableInfo tempGVInfo(&global, unnamedGVIndex);
                report << "    " << globalVarCount << ", " << tempGVInfo.getFullInfo() << std::endl;

                // 统计链接属性
                globalVariableStatistics.addGlobalVariableInfo(tempGVInfo);
            }
            for (int i = 1; i <= fileMap.size(); i++) {
                fileMap[0]->dependencies.emplace(i);
            }
            report << "  总计: " << globalVarCount << " 个全局变量" << std::endl;

            report << globalVariableStatistics.getGlobalVariablesSummary() << std::endl;
        }

        // 分析函数（对组1-9）
        if (groupIndex >= 1) {
            report << "  函数分析:" << std::endl;
            int totalFuncs = 0;
            int unnamedFIndex = 0;
            for (auto& func : *testModule) {

                if (!func.isDeclaration()) {
                    totalFuncs++;
                    FunctionInfo tempFInfo(&func, unnamedFIndex);
                    report << "    " << totalFuncs << ", " << tempFInfo.getBriefInfo() << std::endl;

                    // 统计链接属性
                    functionStatistics.addFunctionInfo(tempFInfo);

                    if (tempFInfo.displayName == "Konan_cxa_demangle") fileMap[groupIndex]->hasKonanCxaDemangle = true;
                }
            }
            for (int dependGroupIndex : dependendGroupInfo[groupIndex])
            {
                report << "  组[" << groupIndex << "]依赖组[" << dependGroupIndex << "]" << std::endl;
                fileMap[groupIndex]->dependencies.emplace(dependGroupIndex);
            }

            report << "  总计: " << totalFuncs << " 个函数" << std::endl;

            report << functionStatistics.getFunctionsSummary() << std::endl;
        }

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
    std::vector<std::string> existingFiles;

    // 检查文件是否存在并统计
    for (const auto& bcFileInfo : fileMap) {
        std::string filename = bcFileInfo->bcFile;
        if (llvm::sys::fs::exists(pathPre + filename)) {
            totalBCFiles++;
            existingFiles.push_back(filename);
        }
    }

    report << "生成的BC文件总数: " << totalBCFiles << std::endl;
    report << "存在的BC文件列表:" << std::endl;
    for (const auto& file : existingFiles) {
        report << "  " << file << std::endl;
    }

    report << std::endl << "=== 报告生成完成 ===" << std::endl;
    report << "报告文件: " << reportFile << std::endl;
    report << "生成时间: " << __DATE__ << " " << __TIME__ << std::endl;

    report.close();
    logger.log("分组报告已生成: " + reportFile);

    // 同时在日志中输出关键统计信息
    logger.log("最终拆分完成: 共生成 " + std::to_string(totalBCFiles) + " 个BC文件");
    for (const auto& file : existingFiles) {
        logger.log("  - " + file);
    }
}

std::vector<llvm::Function*> BCModuleSplitter::getUnprocessedExternalFunctions() {
    std::vector<llvm::Function*> externalFuncs;
    auto& functionMap = common.getFunctionMap();

    for (const auto& pair : functionMap) {
        const FunctionInfo& funcInfo = pair.second;

        // 检查条件：链接属性为 EXTERNAL_LINKAGE 且未被处理
        if (funcInfo.linkage == EXTERNAL_LINKAGE && !funcInfo.isProcessed) {
            externalFuncs.push_back(pair.first);
        }
    }

    logger.log("找到 " + std::to_string(externalFuncs.size()) +
               " 个未处理的外部链接函数 (ExternalLinkage)");
    return externalFuncs;
}

std::vector<llvm::Function*> BCModuleSplitter::getHighInDegreeFunctions(int threshold) {
    std::vector<llvm::Function*> highInDegreeFuncs;
    auto& functionMap = common.getFunctionMap();

    for (const auto& pair : functionMap) {
        if (pair.second.inDegree > threshold && !pair.second.isProcessed) {
            highInDegreeFuncs.push_back(pair.first);
        }
    }

    logger.log("找到 " + std::to_string(highInDegreeFuncs.size()) + " 个入度大于 " + std::to_string(threshold) + " 的函数");
    return highInDegreeFuncs;
}

std::vector<llvm::Function*> BCModuleSplitter::getIsolatedFunctions() {
    std::vector<llvm::Function*> isolatedFuncs;
    auto& functionMap = common.getFunctionMap();

    for (const auto& pair : functionMap) {
        if (pair.second.outDegree == 0 && pair.second.inDegree == 0 && !pair.second.isProcessed) {
            isolatedFuncs.push_back(pair.first);
        }
    }

    logger.log("找到 " + std::to_string(isolatedFuncs.size()) + " 个孤立函数（出度=0且入度=0）");
    return isolatedFuncs;
}

std::unordered_set<llvm::GlobalVariable*> BCModuleSplitter::getGlobalVariables() {
    std::unordered_set<llvm::GlobalVariable*> globals;
    llvm::Module* module = common.getModule();

    for (llvm::GlobalVariable& global : module->globals()) {
        globals.insert(&global);
    }

    logger.log("找到 " + std::to_string(globals.size()) + " 个全局变量");
    return globals;
}

std::vector<llvm::Function*> BCModuleSplitter::getTopFunctions(int topN) {
    std::vector<std::pair<llvm::Function*, int>> scores;
    auto& functionMap = common.getFunctionMap();

    for (const auto& pair : functionMap) {
        // 只考虑未处理的函数
        if (!pair.second.isProcessed) {
            int totalScore = pair.second.outDegree + pair.second.inDegree;
            scores.emplace_back(pair.first, totalScore);
        }
    }

    std::sort(scores.begin(), scores.end(),
         [](const std::pair<llvm::Function*, int>& a, const std::pair<llvm::Function*, int>& b) -> bool {
             return a.second > b.second;
         });

    std::vector<llvm::Function*> result;
    logger.logToFile("\n=== 函数排名前" + std::to_string(topN) + " ===");
    for (int i = 0; i < std::min(topN, (int)scores.size()); i++) {
        result.push_back(scores[i].first);
        std::string funcName = functionMap[scores[i].first].displayName;
        logger.logToFile("排名 " + std::to_string(i + 1) + ": " + funcName +
            " (总分: " + std::to_string(scores[i].second) + ")");
    }

    return result;
}

std::unordered_set<llvm::Function*> BCModuleSplitter::getFunctionGroup(llvm::Function* func) {
    std::unordered_set<llvm::Function*> group;
    auto& functionMap = common.getFunctionMap();

    if (!func || functionMap.find(func) == functionMap.end()) {
        logger.logError("尝试获取无效函数的组");
        return group;
    }

    const FunctionInfo& info = functionMap[func];

    // 只添加未处理的函数
    if (!info.isProcessed) {
        group.insert(func);
    }

    // 添加被调用的函数（仅未处理）
    for (llvm::Function* called : info.calledFunctions) {
        if (called && functionMap.count(called) && !functionMap[called].isProcessed) {
            group.insert(called);
        }
    }

    // 添加调用者函数（仅未处理）
    for (llvm::Function* caller : info.callerFunctions) {
        if (caller && functionMap.count(caller) && !functionMap[caller].isProcessed) {
            group.insert(caller);
        }
    }

    return group;
}

std::unordered_set<llvm::Function*> BCModuleSplitter::getOriginWithOutDegreeFunctions(const std::unordered_set<llvm::Function*>& originFuncs) {
    std::unordered_set<llvm::Function*> completeSet;
    std::queue<llvm::Function*> toProcess;
    auto& functionMap = common.getFunctionMap();

    // 初始添加所有高入度函数
    for (llvm::Function* func : originFuncs) {
        if (!func || functionMap[func].isProcessed) continue;
        completeSet.insert(func);
        toProcess.push(func);
    }

    if (toProcess.empty()) {
        //logger.logToFile("已无需进行扩展，均已记录，总数为: " + std::to_string(originFuncs.size()));
        return originFuncs;
    }

    // 广度优先遍历所有出度函数
    while (!toProcess.empty()) {
        llvm::Function* current = toProcess.front();
        toProcess.pop();

        // 获取当前函数的所有出度函数
        const FunctionInfo& info = functionMap[current];
        for (llvm::Function* called : info.calledFunctions) {
            if (!called || functionMap[called].isProcessed) continue;

            // 如果函数不在集合中，添加到集合和队列
            if (completeSet.find(called) == completeSet.end()) {
                completeSet.insert(called);
                toProcess.push(called);
                //logger.logToFile("  添加出度函数: " + functionMap[called].displayName +
                //             " [被 " + functionMap[current].displayName + " 调用]");
            }
        }
    }

    return completeSet;
}

std::unordered_set<llvm::Function*> BCModuleSplitter::getStronglyConnectedComponent(
        const std::unordered_set<llvm::Function*>& originFuncs) {
    std::unordered_set<llvm::Function*> completeSet;
    std::queue<llvm::Function*> toProcess;
    auto& functionMap = common.getFunctionMap();
    // 初始添加函数
    for (llvm::Function* func : originFuncs) {
        if (!func || functionMap[func].isProcessed) continue;
        completeSet.insert(func);
        toProcess.push(func);
    }

    if (toProcess.empty()) {
        return originFuncs;
    }

    // 广度优先遍历所有依赖循环
    while (!toProcess.empty()) {
        llvm::Function* current = toProcess.front();
        toProcess.pop();

        // 获取当前函数的所有循环依赖组
        for (llvm::Function* cyc : common.getCyclicGroupsContainingFunction(current)) {
            if (!cyc || functionMap[cyc].isProcessed) continue;

            // 如果函数不在集合中，添加到集合和队列
            if (completeSet.find(cyc) == completeSet.end()) {
                completeSet.insert(cyc);
                toProcess.push(cyc);
            }
        }
    }

    return completeSet;
}

// 创建全局变量组BC文件
bool BCModuleSplitter::createGlobalVariablesBCFile(const std::unordered_set<llvm::GlobalVariable*>& globals, const std::string& filename) {
    logger.logToFile("创建全局变量BC文件: " + filename);

    llvm::Module* origModule = common.getModule();
    const auto& globalVariableMap = common.getGlobalVariableMap();

    // 1. 使用 CloneModule 克隆整个原始模块（保持所有属性）
    llvm::ValueToValueMapTy VMap;
    std::unique_ptr<llvm::Module> newModule = CloneModule(*origModule, VMap);

    // 2. 构建新模块中需要保留的全局变量集合
    std::unordered_set<llvm::GlobalVariable*> clonedGlobalsToKeep;
    for (llvm::GlobalVariable* origGV : globals) {
        if (!origGV) continue;

        // 通过 VMap 查找克隆后的对应全局变量
        llvm::Value* clonedVal = VMap[origGV];
        if (auto* clonedGV = llvm::dyn_cast_or_null<llvm::GlobalVariable>(clonedVal)) {
            clonedGlobalsToKeep.insert(clonedGV);
            logger.logToFile("将保留全局变量: " + globalVariableMap.find(origGV)->second.displayName);
        } else {
            logger.logToFile("警告: 全局变量未成功克隆: " + origGV->getName().str());
        }
    }

    // 3. 清理新模块：删除不需要保留的全局变量
    std::vector<llvm::GlobalVariable*> globalsToProcess;
    for (llvm::GlobalVariable& GV : newModule->globals()) {
        globalsToProcess.push_back(&GV);
    }

    for (llvm::GlobalVariable* GV : globalsToProcess) {
        // 检查是否需要保留
        if (clonedGlobalsToKeep.find(GV) != clonedGlobalsToKeep.end()) {
            // 对于需要保留的全局变量，检查是否是LLVM内建变量
            llvm::StringRef name = GV->getName();
            if (name.starts_with("llvm.")) {
                // LLVM内建全局变量，保持原样
                logger.logToFile("LLVM内建全局变量保持原样: " + name.str());
                continue;
            } else {
                GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
                GV->setVisibility(llvm::GlobalValue::DefaultVisibility);
            }
            continue; // 保留
        }

        // 不保留的全局变量：如果未被引用，直接删除；否则转为外部声明
        if (GV->use_empty()) {
            logger.logToFile("删除未使用全局变量: " + GV->getName().str());
            GV->eraseFromParent();
        } else {
            logger.logToFile("全局变量仍有引用，转为声明: " + GV->getName().str());
            // 转为外部声明（移除初始值，设为外部链接）
            GV->setInitializer(nullptr);
            GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
            GV->setVisibility(llvm::GlobalValue::DefaultVisibility);
        }
    }

    // 4. 清理函数（全部删除或转为声明）
    std::vector<llvm::Function*> functionsToProcess;
    for (llvm::Function& F : *newModule) {
        functionsToProcess.push_back(&F);
    }

    for (llvm::Function* F : functionsToProcess) {
        // 根据需求选择：全部删除或转为声明
        if (F->use_empty()) {
            F->eraseFromParent(); // 无引用，直接删除
        } else {
            // 有引用，转为声明（保留函数签名供可能的外部引用）
            F->deleteBody();
            F->setLinkage(llvm::GlobalValue::ExternalLinkage);
            //logger.logToFile("函数转为声明: " + F->getName().str());
        }
    }

    // 5. 清理其他可能的内容（别名、IFunc等）
    // 删除全局别名（GlobalAlias）
    std::vector<llvm::GlobalAlias*> aliasesToRemove;
    for (llvm::GlobalAlias& GA : newModule->aliases()) {
        aliasesToRemove.push_back(&GA);
    }
    for (llvm::GlobalAlias* GA : aliasesToRemove) {
        GA->eraseFromParent();
    }

    // 删除IFunc（间接函数）
    std::vector<llvm::GlobalIFunc*> ifuncsToRemove;
    for (llvm::GlobalIFunc& GIF : newModule->ifuncs()) {
        ifuncsToRemove.push_back(&GIF);
    }
    for (llvm::GlobalIFunc* GIF : ifuncsToRemove) {
        GIF->eraseFromParent();
    }

    // 6. 验证并记录PIC级别（CloneModule已自动复制）
    logger.logToFile("模块克隆完成，包含 " +
                     std::to_string(clonedGlobalsToKeep.size()) +
                     " 个全局变量，PIC级别: " +
                     std::to_string(static_cast<int>(newModule->getPICLevel())));

    // 7. 设置模块标识符并写入文件
    newModule->setModuleIdentifier("global_variables");

    return common.writeBitcodeSafely(*newModule, filename);
}

// 新增：统一的BC文件创建入口，支持两种模式
bool BCModuleSplitter::createBCFile(const std::unordered_set<llvm::Function*>& group,
                    const std::string& filename,
                    int groupIndex) {
    if (BCModuleSplitter::currentMode == CLONE_MODE) {
        return createBCFileWithClone(group, filename, groupIndex);
    } else {
        return createBCFileWithSignatures(group, filename, groupIndex);
    }
}

// 修改后的拆分方法 - 按照指定数量范围分组
void BCModuleSplitter::splitBCFiles(const std::string& outputPrefix) {
    logger.log("\n开始拆分BC文件...");
    logger.log("当前模式: " + std::string(BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE"));

    int fileCount = 0;
    auto& functionMap = common.getFunctionMap();

    // 步骤1: 首先处理全局变量组
    std::unordered_set<llvm::GlobalVariable*> globals = getGlobalVariables();

    if (!globals.empty()) {
        std::string filename = outputPrefix + "_group_globals.bc";

        if (createGlobalVariablesBCFile(globals, filename)) {
            logger.log("✓ 全局变量BC文件创建成功: " + filename);
            fileCount++;
        }
    }

    // 步骤2: 处理外部链接函数组
     std::vector<llvm::Function*> externalFuncs = getUnprocessedExternalFunctions();

    if (!externalFuncs.empty()) {
        std::unordered_set<llvm::Function*> externalSet(externalFuncs.begin(), externalFuncs.end());
        std::unordered_set<llvm::Function*> completeExternalSet = getStronglyConnectedComponent(externalSet);
        std::string filename = outputPrefix + "_group_external.bc";

        if (createBCFile(completeExternalSet, filename, 1)) {
            bool verified = false;
            if (verifyAndFixBCFile(filename, completeExternalSet)) {
                verified = true;
                const char* modeStr = (BCModuleSplitter::currentMode == CLONE_MODE)
                                    ? "Clone模式" : "";
                logger.log("✓ " + std::string(modeStr) + "外部链接函数BC文件验证通过: " + filename);
            }

            if (!verified) {
                logger.logError("✗ 外部链接函数BC文件验证失败: " + filename);
            }
            fileCount++;
        } else {
            logger.logError("✗ 创建外部链接函数BC文件失败: " + filename);
        }
    } else {
        logger.log("没有找到未处理的外部链接函数");
    }

    // 步骤3: 处理高入度函数组（包含完整的出度链）
     std::vector<llvm::Function*> highInDegreeFuncs = getHighInDegreeFunctions(500);

    if (!highInDegreeFuncs.empty()) {
        std::unordered_set<llvm::Function*> highInDegreeSet(highInDegreeFuncs.begin(), highInDegreeFuncs.end());
        std::string filename = outputPrefix + "_group_high_in_degree.bc";
        std::unordered_set<llvm::Function*> completeHighInDegreeSet = getStronglyConnectedComponent(getOriginWithOutDegreeFunctions(highInDegreeSet));
        if (createBCFile(completeHighInDegreeSet, filename, 2)) {
            bool verified = false;
            if (BCModuleSplitter::currentMode == CLONE_MODE) {
                if (quickValidateBCFile(filename)) {
                    verified = true;
                    logger.log("✓ Clone模式高入度函数BC文件验证通过: " + filename);
                }
            } else {
                if (verifyAndFixBCFile(filename, completeHighInDegreeSet)) {
                    verified = true;
                    logger.log("✓ 高入度函数BC文件验证通过: " + filename);
                }
            }

            if (!verified) {
                logger.logError("✗ 高入度函数BC文件验证失败: " + filename);
            }
            fileCount++;
        }
    }

    // 步骤4: 处理孤立函数组（出度=0且入度=0）
     std::vector<llvm::Function*> isolatedFuncs = getIsolatedFunctions();

    if (!isolatedFuncs.empty()) {
        std::unordered_set<llvm::Function*> isolatedSet(isolatedFuncs.begin(), isolatedFuncs.end());
        std::string filename = outputPrefix + "_group_isolated.bc";

        if (createBCFile(isolatedSet, filename, 3)) {
            bool verified = false;
            if (BCModuleSplitter::currentMode == CLONE_MODE) {
                if (quickValidateBCFile(filename)) {
                    verified = true;
                    logger.log("✓ Clone模式孤立函数BC文件验证通过: " + filename);
                }
            } else {
                if (verifyAndFixBCFile(filename, isolatedSet)) {
                    verified = true;
                    logger.log("✓ 孤立函数BC文件验证通过: " + filename);
                }
            }

            if (!verified) {
                logger.logError("✗ 孤立函数BC文件验证失败: " + filename);
            }
            fileCount++;
        }
    }

    // 步骤4: 按照指定数量范围分组
    logger.log("开始按照指定数量范围分组...");

    // 获取所有未处理的函数并按总分排序
     std::vector<std::pair<llvm::Function*, int>> remainingFunctions = getRemainingFunctions();

    logger.log("剩余未处理函数数量: " + std::to_string(remainingFunctions.size()));

    // 定义分组范围
     std::vector<std::pair<int, int>> groupRanges = {
        {0, 200},      // 第3组: 前200个
        {200, 1600},    // 第4组: 201-1600
        {1600, 4000},   // 第5组: 1601-4000
        {4000, 8000},  // 第6组: 4001-8000
        {8000, 20000},  // 第7组: 8001-20000
        {20000, -1}     // 第8组: 20001-剩余所有
    };

    int groupIndex = 4; // 从组3开始

    for (const auto& range : groupRanges) {
        int start = range.first;
        int end = range.second;

        // 收集该范围内的函数
        std::unordered_set<llvm::Function*> group = getFunctionGroupByRange(remainingFunctions, start, end);
        //std::unordered_set<llvm::Function*> completeGroup = group;
        std::unordered_set<llvm::Function*> completeGroup = getStronglyConnectedComponent(getOriginWithOutDegreeFunctions(group));

        if (completeGroup.empty()) {
            logger.log("组 " + std::to_string(groupIndex) + " 没有函数，跳过");
            groupIndex++;
            continue;
        }

        logger.log("处理组 " + std::to_string(groupIndex) + "，范围 [" +
            std::to_string(start) + "-" + (end == -1 ? "剩余所有" : std::to_string(end)) +
            "]，包含 " + std::to_string(completeGroup.size()) + " 个函数");

        // 创建BC文件
        std::string filename = outputPrefix + "_group_" + std::to_string(groupIndex) + ".bc";
        if (createBCFile(completeGroup, filename, groupIndex)) {
            // 验证并修复生成的BC文件
            bool verified = false;
            if (BCModuleSplitter::currentMode == CLONE_MODE) {
                if (quickValidateBCFile(filename)) {
                    verified = true;
                    logger.log("✓ Clone模式分组BC文件验证通过: " + filename);
                }
            } else {
                if (verifyAndFixBCFile(filename, completeGroup)) {
                    verified = true;
                    logger.log("✓ 分组BC文件验证通过: " + filename);
                }
            }

            if (!verified) {
                logger.logError("✗ BC文件验证失败: " + filename);
            }

            fileCount++;
        } else {
            logger.logError("✗ 创建BC文件失败: " + filename);
        }

        groupIndex++;
    }

    totalGroups = fileCount;

    logger.log("\n=== 拆分完成 ===");
    logger.log("共生成 " + std::to_string(fileCount) + " 个分组BC文件");
    logger.log("使用模式: " + std::string(BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE"));

    // 统计处理情况
    int processedCount = 0;
    for (const auto& pair : functionMap) {
        if (pair.second.isProcessed) {
            processedCount++;
        }
    }
    logger.log("已处理 " + std::to_string(processedCount) + " / " + std::to_string(functionMap.size()) + " 个函数");

    if (processedCount < functionMap.size()) {
        logger.logWarning("警告: 有 " + std::to_string(functionMap.size() - processedCount) + " 个函数未被处理");

        // 显示所有未处理的函数 - 完整打印
        logger.logToFile("未处理函数完整列表:");
        int unprocessedCount = 0;
        for (const auto& pair : functionMap) {
            if (!pair.second.isProcessed) {
                const FunctionInfo& info = pair.second;
                logger.logToFile(info.getFullInfo());
            }
        }
        logger.logToFile("未处理函数统计: 共 " + std::to_string(unprocessedCount) + " 个函数");
    }
}

// 优化的BC文件创建方法，正确处理链接属性
bool BCModuleSplitter::createBCFileWithSignatures(const std::unordered_set<llvm::Function*>& group, const std::string& filename, int groupIndex) {
    logger.logToFile("创建BC文件: " + filename + " (组 " + std::to_string(groupIndex) + ")");

    // 使用全新的上下文
    llvm::LLVMContext newContext;
    llvm::Module* module = common.getModule();
    auto& functionMap = common.getFunctionMap();
    auto newModule = std::make_unique<llvm::Module>(filename, newContext);

    // 复制原始模块的基本属性
    newModule->setTargetTriple(module->getTargetTriple());
    newModule->setDataLayout(module->getDataLayout());

    // 为每个函数创建签名并保持正确的链接属性
    for (llvm::Function* origFunc : group) {
        if (!origFunc) continue;

        // 在新建上下文中重新创建函数类型
         std::vector<llvm::Type*> paramTypes;
        for (const auto& arg : origFunc->args()) {
            llvm::Type* argType = arg.getType();

            // 将类型映射到新上下文中的对应类型
            if (argType->isIntegerTy()) {
                paramTypes.push_back(llvm::Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
            } else if (argType->isPointerTy()) {
                llvm::PointerType* ptrType = llvm::cast<llvm::PointerType>(argType);
                unsigned addressSpace = ptrType->getAddressSpace();
                paramTypes.push_back(llvm::PointerType::get(newContext, addressSpace));
            } else if (argType->isVoidTy()) {
                paramTypes.push_back(llvm::Type::getVoidTy(newContext));
            } else if (argType->isFloatTy()) {
                paramTypes.push_back(llvm::Type::getFloatTy(newContext));
            } else if (argType->isDoubleTy()) {
                paramTypes.push_back(llvm::Type::getDoubleTy(newContext));
            } else {
                paramTypes.push_back(llvm::PointerType::get(newContext, 0));
            }
        }

        // 确定返回类型
        llvm::Type* returnType = origFunc->getReturnType();
        llvm::Type* newReturnType;

        if (returnType->isIntegerTy()) {
            newReturnType = llvm::Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
        } else if (returnType->isPointerTy()) {
            llvm::PointerType* ptrType = llvm::cast<llvm::PointerType>(returnType);
            unsigned addressSpace = ptrType->getAddressSpace();
            newReturnType = llvm::PointerType::get(newContext, addressSpace);
        } else if (returnType->isVoidTy()) {
            newReturnType = llvm::Type::getVoidTy(newContext);
        } else if (returnType->isFloatTy()) {
            newReturnType = llvm::Type::getFloatTy(newContext);
        } else if (returnType->isDoubleTy()) {
            newReturnType = llvm::Type::getDoubleTy(newContext);
        } else {
            newReturnType = llvm::PointerType::get(newContext, 0);
        }

        // 创建函数类型
        llvm::FunctionType* funcType = llvm::FunctionType::get(newReturnType, paramTypes, origFunc->isVarArg());

        // 创建函数并保持正确的链接属性
        llvm::Function* newFunc = llvm::Function::Create(
            funcType,
            origFunc->getLinkage(),  // 使用原始链接属性
            origFunc->getName(),
            newModule.get()
        );

        // 复制重要属性
        newFunc->setCallingConv(origFunc->getCallingConv());
        newFunc->setVisibility(origFunc->getVisibility());  // 保持可见性
        newFunc->setDLLStorageClass(origFunc->getDLLStorageClass());

        // 标记函数在原始模块中的分组信息
        if (functionMap.count(origFunc)) {
            functionMap[origFunc].groupIndex = groupIndex;
            functionMap[origFunc].isProcessed = true;
        }

        logger.logToFile("复制函数: " + functionMap[origFunc].getBriefInfo());
    }

    // 写入bitcode
    return common.writeBitcodeSafely(*newModule, filename);
}

// 新增：使用LLVM CloneModule创建BC文件
bool BCModuleSplitter::createBCFileWithClone(const std::unordered_set<llvm::Function*>& group,
                            const std::string& filename,
                            int groupIndex) {
    logger.logToFile("使用Clone模式创建BC文件: " + filename + " (组 " + std::to_string(groupIndex) + ")");

    // 使用ValueToValueMapTy进行克隆
    llvm::ValueToValueMapTy vmap;
    std::unordered_set<llvm::Function*> newExternalGroup;
    std::unordered_set<llvm::Function*> newGroup;
    llvm::Module* module = common.getModule();
    auto& functionMap = common.getFunctionMap();
    auto newModule = CloneModule(*module, vmap);

    if (!newModule) {
        logger.logError("CloneModule失败: " + filename);
        return false;
    }

    // 设置模块名称
    newModule->setModuleIdentifier("cloned_group_" + std::to_string(groupIndex));
    for (llvm::Function* origFunc : group) {
        // 查找原始函数在vmap中对应的新函数
        auto it = vmap.find(origFunc);
        if (it != vmap.end()) {
            // 确保映射到的是Function类型
            if (llvm::Function* newFunc = llvm::dyn_cast<llvm::Function>(it->second)) {
                if (!FunctionInfo::areAllCallersInGroup(origFunc, group, functionMap) ||
                    functionMap[origFunc].isReferencedByGlobals || true) {
                    newExternalGroup.insert(newFunc);
                    const auto& info = functionMap[origFunc];
                    logger.logToFile("需要使用外部链接: " + info.displayName);
                    // for (llvm::Function* called : info.calledFunctions) {
                    //     if (!group.count(called) || newExternalGroup.count(called)) continue;
                    //     newExternalGroup.insert(called);
                    //     logger.logToFile("需要使用外部链接: " + functionMap[called].displayName);
                    // }
                }
                newGroup.insert(newFunc);
            } else {
                // 处理错误：映射到的不是Function
                logger.logError("Warning: Value mapped is not a Function for " + origFunc->getName().str());
            }
        } else {
            // 处理错误：原始函数没有在vmap中找到映射
            logger.logError("Warning: No mapping found for function " + origFunc->getName().str());
        }
    }
    if (group.size() != newGroup.size()) {
        logger.logError("CloneModule 映射前后大小不匹配");
        return false;
    }
    // 处理函数：保留组内函数定义，其他转为声明
    processClonedModuleFunctions(*newModule, newGroup, newExternalGroup);

    // 处理全局变量：全部转为external声明
    processClonedModuleGlobals(*newModule);

    // 标记原始函数已处理
    for (llvm::Function* origFunc : group) {
        if (functionMap.count(origFunc)) {
            functionMap[origFunc].groupIndex = groupIndex;
            functionMap[origFunc].isProcessed = true;
        }
    }

    logger.logToFile("Clone模式完成: " + filename + " (包含 " + std::to_string(group.size()) + " 个函数)");
    return common.writeBitcodeSafely(*newModule, filename);
}

// 新增：处理克隆模块中的函数
void BCModuleSplitter::processClonedModuleFunctions(llvm::Module& clonedModule,
    const std::unordered_set<llvm::Function*>& targetGroup,
    const std::unordered_set<llvm::Function*>& externalGroup) {
    // 处理所有函数
    std::vector<llvm::Function*> functionsToProcess;
    for (llvm::Function& func : clonedModule) {
        functionsToProcess.push_back(&func);
    }

    for (llvm::Function* func : functionsToProcess) {
        if (targetGroup.find(func) == targetGroup.end()) {
            // 非目标函数：转为声明
            if (!func->isDeclaration()) {
                func->deleteBody();
                func->setLinkage(llvm::GlobalValue::ExternalLinkage);
                func->setVisibility(llvm::GlobalValue::DefaultVisibility);
                // 清除dso_local属性
                func->setDSOLocal(false);
                //logger.logToFile("非目标函数转为声明: " + func->getName().str());
            }
        } else {
            // 目标函数：保留为定义，设置合适的链接属性
            if (externalGroup.find(func) != externalGroup.end() &&
                (func->getLinkage() == llvm::GlobalValue::InternalLinkage ||
                func->getLinkage() == llvm::GlobalValue::PrivateLinkage)) {
                func->setLinkage(llvm::GlobalValue::ExternalLinkage);
                func->setVisibility(llvm::GlobalValue::DefaultVisibility);
                //logger.logToFile("外部函数链接调整: " + func->getName().str() + " -> External");
            } //else {
                //logger.logToFile("目标函数: " + func->getName().str() + " 不需要调整");
            //}
        }
    }
}

// 新增：处理克隆模块中的全局变量
void BCModuleSplitter::processClonedModuleGlobals(llvm::Module& clonedModule) {
    for (llvm::GlobalVariable& global : clonedModule.globals()) {
        global.setDSOLocal(false);
        // 移除初始值，转为外部声明
        if (global.hasInitializer()) {
            global.setInitializer(nullptr);
        }

        // 设置external链接和默认可见性
        global.setLinkage(llvm::GlobalValue::ExternalLinkage);
        global.setVisibility(llvm::GlobalValue::DefaultVisibility);

        //logger.logToFile("全局变量处理: " + global.getName().str() + " -> External");
    }
}

// 新增：获取剩余函数列表
 std::vector<std::pair<llvm::Function*, int>> BCModuleSplitter::getRemainingFunctions() {
     std::vector<std::pair<llvm::Function*, int>> remainingFunctions;
    auto& functionMap = common.getFunctionMap();

    for (const auto& pair : functionMap) {
        if (!pair.second.isProcessed) {
            int totalScore = pair.second.outDegree + pair.second.inDegree;
            remainingFunctions.emplace_back(pair.first, totalScore);
        }
    }

    // 按总分降序排序
    std::sort(remainingFunctions.begin(), remainingFunctions.end(),
        [](const std::pair<llvm::Function*, int>& a, const std::pair<llvm::Function*, int>& b) {
            return a.second > b.second;
        });

    return remainingFunctions;
}

// 新增：按范围获取函数组
std::unordered_set<llvm::Function*> BCModuleSplitter::getFunctionGroupByRange(const  std::vector<std::pair<llvm::Function*, int>>& functions,
                                                int start, int end) {
    std::unordered_set<llvm::Function*> group;
    auto& functionMap = common.getFunctionMap();

    for (int i = start; i < functions.size(); i++) {
        if (end != -1 && i >= end) break;

        llvm::Function* func = functions[i].first;
        if (!func || functionMap[func].isProcessed) continue;

        group.insert(func);
    }
    return group;
}

// 在 splitter.cpp 中添加这些方法的实现

// 验证所有BC文件
void BCModuleSplitter::validateAllBCFiles(const std::string& outputPrefix) {
    logger.log("\n=== 通过 BCModuleSplitter 验证所有BC文件 ===");
    verifier.validateAllBCFiles(outputPrefix, currentMode == CLONE_MODE);
}

// 验证并修复单个BC文件
bool BCModuleSplitter::verifyAndFixBCFile(const std::string& filename,
                                        const std::unordered_set<llvm::Function*>& expectedGroup) {
    return verifier.verifyAndFixBCFile(filename, expectedGroup);
}

// 快速验证BC文件
bool BCModuleSplitter::quickValidateBCFile(const std::string& filename) {
    return verifier.quickValidateBCFile(filename);
}

// 分析BC文件内容
void BCModuleSplitter::analyzeBCFileContent(const std::string& filename) {
    verifier.analyzeBCFileContent(filename);
}