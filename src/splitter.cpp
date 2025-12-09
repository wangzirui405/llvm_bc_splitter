// splitter.cpp
#include "splitter.h"
#include "common.h"
#include "logging.h"
#include "verifier.h"
#include "core.h"
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
#include "llvm/Transforms/Utils/Cloning.h"  // 包含 CloneModule 和 ValueToValueMapTy
#include "llvm/IR/ValueMap.h"              // ValueToValueMapTy 的详细定义
#include <algorithm>
#include <queue>
#include <cstdlib>

BCModuleSplitter::BCModuleSplitter() : verifier(common) {
    // 构造函数初始化verifier时传入common
}
using namespace llvm;
using namespace std;

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

    // 创建新的上下文
    llvm::LLVMContext* context = new llvm::LLVMContext();
    common.setContext(context);

    auto module = llvm::parseIRFile(filename, err, *context);
    common.setModule(std::move(module));

    if (!common.hasModule()) {
        logger.logError("无法加载BC文件: " + filename);
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

    // 收集所有函数，只为无名函数分配序号
    int unnamedSequenceNumber = 0;
    for (llvm::Function& func : *module) {
        if (func.isDeclaration()) continue;

        std::string funcName = func.getName().str();
        FunctionInfo tempInfo(&func);  // 临时对象用于判断是否为无名函数
        bool isUnnamed = tempInfo.isUnnamed();

        // 只有无名函数才分配序号
        int seqNum = isUnnamed ? unnamedSequenceNumber++ : -1;

        auto result = functionMap.emplace(&func, FunctionInfo(&func, seqNum));
        if (result.second) {
            functionPtrs.push_back(&func);
        }
    }

    logger.log("收集到 " + std::to_string(functionMap.size()) + " 个函数定义");
    logger.log("其中无名函数数量: " + std::to_string(unnamedSequenceNumber));

    // 分析调用关系
    for (llvm::Function& func : *module) {
        if (func.isDeclaration()) continue;

        for (llvm::BasicBlock& bb : func) {
            for (llvm::Instruction& inst : bb) {
                if (llvm::CallInst* callInst = llvm::dyn_cast<llvm::CallInst>(&inst)) {
                    llvm::Function* calledFunc = callInst->getCalledFunction();

                    if (!calledFunc && callInst->getNumOperands() > 0) {
                        llvm::Value* calledValue = callInst->getCalledOperand();
                        if (llvm::isa<llvm::Function>(calledValue)) {
                            calledFunc = llvm::cast<llvm::Function>(calledValue);
                        }
                    }

                    if (calledFunc && !calledFunc->isIntrinsic() && functionMap.count(calledFunc)) {
                        functionMap[&func].calledFunctions.insert(calledFunc);
                        functionMap[&func].outDegree++;

                        functionMap[calledFunc].callerFunctions.insert(&func);
                        functionMap[calledFunc].inDegree++;
                    }
                }
            }
        }
    }

    logger.log("分析完成，共分析 " + std::to_string(functionMap.size()) + " 个函数");
}

void BCModuleSplitter::printFunctionInfo() {
    auto& functionMap = common.getFunctionMap();
    logger.logToFile("\n=== 函数调用关系分析 ===");
    for (const auto& pair : functionMap) {
        const FunctionInfo& info = pair.second;
        std::string groupInfo = info.groupIndex >= 0 ?
            " [组: " + std::to_string(info.groupIndex) + "]" : " [未分组]";
        std::string seqInfo = info.isUnnamed() ?
            " [序号: " + std::to_string(info.sequenceNumber) + "]" : " [有名]";

        logger.logToFile("函数: " + info.displayName +
            seqInfo +
            " [出度: " + std::to_string(info.outDegree) +
            ", 入度: " + std::to_string(info.inDegree) + "]" + groupInfo);
    }
}

// 更新后的生成分组报告方法，添加最终拆分完成后的bc文件的各个链接属性和可见性
void BCModuleSplitter::generateGroupReport(const string& outputPrefix) {
    string reportFile = outputPrefix + "_group_report.txt";
    auto& functionMap = common.getFunctionMap();
    ofstream report(reportFile);

    if (!report.is_open()) {
        logger.logError("无法创建分组报告文件: " + reportFile);
        return;
    }

    report << "=== BC文件分组报告 ===" << endl;
    report << "总函数数: " << functionMap.size() << endl;
    report << "总分组数: " << totalGroups << endl;
    report << "使用模式: " << (BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE") << endl << endl;

    // 按组统计
    unordered_map<int, vector<string>> groupFunctions;
    int ungroupedCount = 0;

    for (const auto& pair : functionMap) {
        const FunctionInfo& info = pair.second;
        if (info.groupIndex >= 0) {
            string funcInfo = info.displayName +
                " [入度:" + to_string(info.inDegree) +
                ", 出度:" + to_string(info.outDegree) +
                ", 总分:" + to_string(info.inDegree + info.outDegree) +
                (info.isUnnamed() ? ", 无名函数序号:" + to_string(info.sequenceNumber) : ", 有名函数") + "]";
            groupFunctions[info.groupIndex].push_back(funcInfo);
        } else {
            ungroupedCount++;
        }
    }

    report << "=== 分组详情 ===" << endl;

    // 全局变量组（组0）
    report << "组 0 (全局变量):" << endl;
    unordered_set<GlobalVariable*> globals = getGlobalVariables();
    int globalCount = 0;
    for (GlobalVariable* global : globals) {
        if (global) {
            report << "  " << to_string(++globalCount) << ". " << global->getName().str()
                    << " [链接: " << getLinkageString(global->getLinkage())
                    << ", 可见性: " << getVisibilityString(global->getVisibility()) << "]" << endl;
        }
    }
    report << "总计: " << globalCount << " 个全局变量" << endl << endl;

    // 高入度函数组（组1）
    if (groupFunctions.count(1)) {
        report << "组 1 (高入度函数):" << endl;
        int count = 0;
        for (const string& funcName : groupFunctions[1]) {
            report << "  " << to_string(++count) << ". " << funcName << endl;
        }
        report << "总计: " << groupFunctions[1].size() << " 个函数" << endl << endl;
    }

    // 孤立函数组（组2）
    if (groupFunctions.count(2)) {
        report << "组 2 (孤立函数):" << endl;
        int count = 0;
        for (const string& funcName : groupFunctions[2]) {
            report << "  " << to_string(++count) << ". " << funcName << endl;
        }
        report << "总计: " << groupFunctions[2].size() << " 个函数" << endl << endl;
    }

    // 新的分组策略（组3-8）
    vector<string> groupDescriptions = {
        "前100个函数",
        "101-400个函数",
        "401-1000个函数",
        "1001-2000个函数",
        "2001-5000个函数",
        "5001-剩余所有函数"
    };

    for (int i = 3; i <= 8; i++) {
        if (groupFunctions.count(i)) {
            report << "组 " << i << " (" << groupDescriptions[i-3] << "):" << endl;
            int count = 0;
            for (const string& funcName : groupFunctions[i]) {
                report << "  " << to_string(++count) << ". " << funcName << endl;
            }
            report << "总计: " << groupFunctions[i].size() << " 个函数" << endl << endl;
        }
    }

    if (ungroupedCount > 0) {
        report << "=== 未分组函数 ===" << endl;
        report << "未分组函数数量: " << ungroupedCount << endl;
        int count = 0;
        for (const auto& pair : functionMap) {
            if (pair.second.groupIndex < 0) {
                const FunctionInfo& info = pair.second;
                report << "  " << to_string(++count) << ". " << info.displayName <<
                    " [入度:" << info.inDegree <<
                    ", 出度:" << info.outDegree <<
                    ", 总分:" << (info.inDegree + info.outDegree) <<
                    (info.isUnnamed() ? ", 无名函数序号:" + to_string(info.sequenceNumber) : ", 有名函数") << "]" << endl;
            }
        }
    }

    // 新增：最终拆分完成后的BC文件链接属性和可见性报告
    report << "=== 最终拆分BC文件链接属性和可见性报告 ===" << endl << endl;

    // 定义所有可能的BC文件组
    vector<pair<string, int>> bcFiles = {
        {"_group_globals.bc", 0},
        {"_group_high_in_degree.bc", 1},
        {"_group_isolated.bc", 2}
    };

    // 添加组3-8
    for (int i = 3; i <= 8; i++) {
        bcFiles.emplace_back("_group_" + to_string(i) + ".bc", i);
    }

    // 检查每个BC文件并报告链接属性和可见性
    for (const auto& bcFileInfo : bcFiles) {
        string filename = outputPrefix + bcFileInfo.first;
        int groupIndex = bcFileInfo.second;

        if (!sys::fs::exists(filename)) {
            continue;
        }

        report << "文件: " << filename << " (组 " << groupIndex << ")" << endl;

        // 加载BC文件进行分析
        LLVMContext tempContext;
        SMDiagnostic err;
        auto testModule = parseIRFile(filename, err, tempContext);

        if (!testModule) {
            report << "  错误: 无法加载文件进行分析" << endl;
            continue;
        }

        // 统计信息
        int totalFuncs = 0;
        int externalLinkageCount = 0;
        int internalLinkageCount = 0;
        int privateLinkageCount = 0;
        int defaultVisibilityCount = 0;
        int hiddenVisibilityCount = 0;
        int protectedVisibilityCount = 0;

        // 分析全局变量（仅对组0）
        if (groupIndex == 0) {
            report << "  全局变量分析:" << endl;
            int globalVarCount = 0;
            for (auto& global : testModule->globals()) {
                globalVarCount++;
                report << "    " << globalVarCount << ". " << global.getName().str()
                       << " [链接: " << getLinkageString(global.getLinkage())
                       << ", 可见性: " << getVisibilityString(global.getVisibility()) << "]" << endl;

                // 统计链接属性
                if (global.getLinkage() == GlobalValue::ExternalLinkage) externalLinkageCount++;
                else if (global.getLinkage() == GlobalValue::InternalLinkage) internalLinkageCount++;
                else if (global.getLinkage() == GlobalValue::PrivateLinkage) privateLinkageCount++;

                // 统计可见性
                if (global.getVisibility() == GlobalValue::DefaultVisibility) defaultVisibilityCount++;
                else if (global.getVisibility() == GlobalValue::HiddenVisibility) hiddenVisibilityCount++;
                else if (global.getVisibility() == GlobalValue::ProtectedVisibility) protectedVisibilityCount++;
            }
            report << "  总计: " << globalVarCount << " 个全局变量" << endl;
        }

        // 分析函数（对组1-8）
        if (groupIndex >= 1) {
            report << "  函数分析:" << endl;
            for (auto& func : *testModule) {
                totalFuncs++;
                report << "    " << totalFuncs << ". " << func.getName().str()
                       << " [链接: " << getLinkageString(func.getLinkage())
                       << ", 可见性: " << getVisibilityString(func.getVisibility())
                       << (func.isDeclaration() ? ", 声明" : ", 定义") << "]" << endl;

                // 统计链接属性
                if (func.getLinkage() == GlobalValue::ExternalLinkage) externalLinkageCount++;
                else if (func.getLinkage() == GlobalValue::InternalLinkage) internalLinkageCount++;
                else if (func.getLinkage() == GlobalValue::PrivateLinkage) privateLinkageCount++;

                // 统计可见性
                if (func.getVisibility() == GlobalValue::DefaultVisibility) defaultVisibilityCount++;
                else if (func.getVisibility() == GlobalValue::HiddenVisibility) hiddenVisibilityCount++;
                else if (func.getVisibility() == GlobalValue::ProtectedVisibility) protectedVisibilityCount++;
            }
            report << "  总计: " << totalFuncs << " 个函数" << endl;
        }

        // 输出统计摘要
        report << "  链接属性统计:" << endl;
        report << "    External链接: " << externalLinkageCount << endl;
        report << "    Internal链接: " << internalLinkageCount << endl;
        report << "    Private链接: " << privateLinkageCount << endl;

        report << "  可见性统计:" << endl;
        report << "    Default可见性: " << defaultVisibilityCount << endl;
        report << "    Hidden可见性: " << hiddenVisibilityCount << endl;
        report << "    Protected可见性: " << protectedVisibilityCount << endl;

        // 验证模块
        string verifyResult;
        raw_string_ostream rso(verifyResult);
        bool moduleValid = !verifyModule(*testModule, &rso);
        report << "  模块验证: " << (moduleValid ? "通过" : "失败") << endl;

        if (!moduleValid) {
            report << "  验证错误: " << rso.str() << endl;
        }

        report << endl;
    }

    // 添加所有BC文件的总体统计
    report << "=== 所有BC文件总体统计 ===" << endl;

    int totalBCFiles = 0;
    int validBCFiles = 0;
    vector<string> existingFiles;

    // 检查文件是否存在并统计
    for (const auto& bcFileInfo : bcFiles) {
        string filename = outputPrefix + bcFileInfo.first;
        if (sys::fs::exists(filename)) {
            totalBCFiles++;
            existingFiles.push_back(filename);
        }
    }

    report << "生成的BC文件总数: " << totalBCFiles << endl;
    report << "存在的BC文件列表:" << endl;
    for (const auto& file : existingFiles) {
        report << "  " << file << endl;
    }

    report << endl << "=== 报告生成完成 ===" << endl;
    report << "报告文件: " << reportFile << endl;
    report << "生成时间: " << __DATE__ << " " << __TIME__ << endl;

    report.close();
    logger.log("分组报告已生成: " + reportFile);

    // 同时在日志中输出关键统计信息
    logger.log("最终拆分完成: 共生成 " + to_string(totalBCFiles) + " 个BC文件");
    for (const auto& file : existingFiles) {
        logger.log("  - " + file);
    }
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

std::unordered_set<llvm::Function*> BCModuleSplitter::getHighInDegreeWithOutDegreeFunctions(const std::unordered_set<llvm::Function*>& highInDegreeFuncs) {
    std::unordered_set<llvm::Function*> completeSet;
    std::queue<llvm::Function*> toProcess;
    auto& functionMap = common.getFunctionMap();

    // 初始添加所有高入度函数
    for (llvm::Function* func : highInDegreeFuncs) {
        if (!func || functionMap[func].isProcessed) continue;
        completeSet.insert(func);
        toProcess.push(func);
    }

    if (toProcess.empty()) {
        logger.logToFile("已无需进行扩展，均已记录，总数为: " + std::to_string(highInDegreeFuncs.size()));
        return highInDegreeFuncs;
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
                logger.logToFile("  添加出度函数: " + functionMap[called].displayName +
                             " [被 " + functionMap[current].displayName + " 调用]");
            }
        }
    }

    return completeSet;
}

// 创建全局变量组BC文件
bool BCModuleSplitter::createGlobalVariablesBCFile(const unordered_set<GlobalVariable*>& globals, const string& filename) {
    logger.logToFile("创建全局变量BC文件: " + filename);

    Module* origModule = common.getModule();

    // 1. 使用 CloneModule 克隆整个原始模块（保持所有属性）
    ValueToValueMapTy VMap;
    unique_ptr<Module> newModule = CloneModule(*origModule, VMap);

    // 2. 构建新模块中需要保留的全局变量集合
    unordered_set<GlobalVariable*> clonedGlobalsToKeep;
    for (GlobalVariable* origGV : globals) {
        if (!origGV) continue;

        // 通过 VMap 查找克隆后的对应全局变量
        Value* clonedVal = VMap[origGV];
        if (auto* clonedGV = dyn_cast_or_null<GlobalVariable>(clonedVal)) {
            clonedGlobalsToKeep.insert(clonedGV);
            logger.logToFile("将保留全局变量: " + origGV->getName().str());
        } else {
            logger.logToFile("警告: 全局变量未成功克隆: " + origGV->getName().str());
        }
    }

    // 3. 清理新模块：删除不需要保留的全局变量
    std::vector<GlobalVariable*> globalsToProcess;
    for (GlobalVariable& GV : newModule->globals()) {
        globalsToProcess.push_back(&GV);
    }

    for (GlobalVariable* GV : globalsToProcess) {
        // 检查是否需要保留
        if (clonedGlobalsToKeep.find(GV) != clonedGlobalsToKeep.end()) {
            // 对于需要保留的全局变量，检查是否是LLVM内建变量
            StringRef name = GV->getName();
            if (name.starts_with("llvm.")) {
                // LLVM内建全局变量，保持原样
                logger.logToFile("保留LLVM内建全局变量（保持原样）: " + name.str());
                continue;
            } else {
                GV->setLinkage(GlobalValue::ExternalLinkage);
                GV->setVisibility(GlobalValue::DefaultVisibility);
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
            GV->setLinkage(GlobalValue::ExternalLinkage);
            GV->setVisibility(GlobalValue::DefaultVisibility);
        }
    }

    // 4. 清理函数（全部删除或转为声明）
    std::vector<Function*> functionsToProcess;
    for (Function& F : *newModule) {
        functionsToProcess.push_back(&F);
    }

    for (Function* F : functionsToProcess) {
        // 根据需求选择：全部删除或转为声明
        if (F->use_empty()) {
            F->eraseFromParent(); // 无引用，直接删除
        } else {
            // 有引用，转为声明（保留函数签名供可能的外部引用）
            F->deleteBody();
            F->setLinkage(GlobalValue::ExternalLinkage);
            logger.logToFile("函数转为声明: " + F->getName().str());
        }
    }

    // 5. 清理其他可能的内容（别名、IFunc等）
    // 删除全局别名（GlobalAlias）
    std::vector<GlobalAlias*> aliasesToRemove;
    for (GlobalAlias& GA : newModule->aliases()) {
        aliasesToRemove.push_back(&GA);
    }
    for (GlobalAlias* GA : aliasesToRemove) {
        GA->eraseFromParent();
    }

    // 删除IFunc（间接函数）
    std::vector<GlobalIFunc*> ifuncsToRemove;
    for (GlobalIFunc& GIF : newModule->ifuncs()) {
        ifuncsToRemove.push_back(&GIF);
    }
    for (GlobalIFunc* GIF : ifuncsToRemove) {
        GIF->eraseFromParent();
    }

    // 6. 验证并记录PIC级别（CloneModule已自动复制）
    logger.logToFile("模块克隆完成，包含 " +
                     to_string(clonedGlobalsToKeep.size()) +
                     " 个全局变量，PIC级别: " +
                     to_string(static_cast<int>(newModule->getPICLevel())));

    // 7. 设置模块标识符并写入文件
    newModule->setModuleIdentifier("global_variables");

    return common.writeBitcodeSafely(*newModule, filename);
}

// 创建高入度函数组BC文件（包含完整的出度链）
bool BCModuleSplitter::createHighInDegreeBCFile(const unordered_set<Function*>& highInDegreeFuncs, const string& filename) {
    logger.logToFile("创建高入度函数BC文件: " + filename + " (包含完整出度链)");

    // 首先获取完整的高入度函数组（包含所有出度函数）
    unordered_set<Function*> completeHighInDegreeSet = getHighInDegreeWithOutDegreeFunctions(highInDegreeFuncs);

    LLVMContext newContext;
    llvm::Module* module = common.getModule();
    auto& functionMap = common.getFunctionMap();
    auto newModule = make_unique<Module>("high_in_degree_functions", newContext);

    // 复制原始模块的基本属性
    newModule->setTargetTriple(module->getTargetTriple());
    newModule->setDataLayout(module->getDataLayout());

    // 复制高入度函数签名（包括所有出度函数）
    for (Function* origFunc : completeHighInDegreeSet) {
        if (!origFunc) continue;

        // 创建函数类型
        vector<Type*> paramTypes;
        for (const auto& arg : origFunc->args()) {
            Type* argType = arg.getType();

            if (argType->isIntegerTy()) {
                paramTypes.push_back(Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
            } else if (argType->isPointerTy()) {
                PointerType* ptrType = cast<PointerType>(argType);
                unsigned addressSpace = ptrType->getAddressSpace();
                paramTypes.push_back(PointerType::get(newContext, addressSpace));
            } else if (argType->isVoidTy()) {
                paramTypes.push_back(Type::getVoidTy(newContext));
            } else if (argType->isFloatTy()) {
                paramTypes.push_back(Type::getFloatTy(newContext));
            } else if (argType->isDoubleTy()) {
                paramTypes.push_back(Type::getDoubleTy(newContext));
            } else {
                paramTypes.push_back(PointerType::get(newContext, 0));
            }
        }

        // 确定返回类型
        Type* returnType = origFunc->getReturnType();
        Type* newReturnType;

        if (returnType->isIntegerTy()) {
            newReturnType = Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
        } else if (returnType->isPointerTy()) {
            PointerType* ptrType = cast<PointerType>(returnType);
            unsigned addressSpace = ptrType->getAddressSpace();
            newReturnType = PointerType::get(newContext, addressSpace);
        } else if (returnType->isVoidTy()) {
            newReturnType = Type::getVoidTy(newContext);
        } else if (returnType->isFloatTy()) {
            newReturnType = Type::getFloatTy(newContext);
        } else if (returnType->isDoubleTy()) {
            newReturnType = Type::getDoubleTy(newContext);
        } else {
            newReturnType = PointerType::get(newContext, 0);
        }

        FunctionType* funcType = FunctionType::get(newReturnType, paramTypes, origFunc->isVarArg());

        Function* newFunc = Function::Create(
            funcType,
            origFunc->getLinkage(),
            origFunc->getName(),
            newModule.get()
        );

        newFunc->setCallingConv(origFunc->getCallingConv());
        newFunc->setVisibility(origFunc->getVisibility());
        newFunc->setDLLStorageClass(origFunc->getDLLStorageClass());

        // 标记函数已处理
        if (functionMap.count(origFunc)) {
            functionMap[origFunc].groupIndex = 1; // 高入度函数组编号为1
            functionMap[origFunc].isProcessed = true;
        }

        // 记录函数类型（高入度函数或出度函数）
        string funcTypeStr = (highInDegreeFuncs.find(origFunc) != highInDegreeFuncs.end()) ?
            "高入度函数" : "出度函数";

        logger.logToFile("复制" + funcTypeStr + ": " + origFunc->getName().str() +
                    " [入度: " + to_string(functionMap[origFunc].inDegree) +
                    ", 出度: " + to_string(functionMap[origFunc].outDegree) +
                    ", 链接: " + getLinkageString(origFunc->getLinkage()) + "]");
    }

    return common.writeBitcodeSafely(*newModule, filename);
}

// 创建孤立函数组BC文件
bool BCModuleSplitter::createIsolatedFunctionsBCFile(const unordered_set<Function*>& isolatedFuncs, const string& filename) {
    logger.logToFile("创建孤立函数BC文件: " + filename);

    LLVMContext newContext;
    llvm::Module* module = common.getModule();
    auto& functionMap = common.getFunctionMap();
    auto newModule = make_unique<Module>("isolated_functions", newContext);

    // 复制原始模块的基本属性
    newModule->setTargetTriple(module->getTargetTriple());
    newModule->setDataLayout(module->getDataLayout());

    // 复制孤立函数签名
    for (Function* origFunc : isolatedFuncs) {
        if (!origFunc) continue;

        // 创建函数类型
        vector<Type*> paramTypes;
        for (const auto& arg : origFunc->args()) {
            Type* argType = arg.getType();

            if (argType->isIntegerTy()) {
                paramTypes.push_back(Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
            } else if (argType->isPointerTy()) {
                PointerType* ptrType = cast<PointerType>(argType);
                unsigned addressSpace = ptrType->getAddressSpace();
                paramTypes.push_back(PointerType::get(newContext, addressSpace));
            } else if (argType->isVoidTy()) {
                paramTypes.push_back(Type::getVoidTy(newContext));
            } else if (argType->isFloatTy()) {
                paramTypes.push_back(Type::getFloatTy(newContext));
            } else if (argType->isDoubleTy()) {
                paramTypes.push_back(Type::getDoubleTy(newContext));
            } else {
                paramTypes.push_back(PointerType::get(newContext, 0));
            }
        }

        // 确定返回类型
        Type* returnType = origFunc->getReturnType();
        Type* newReturnType;

        if (returnType->isIntegerTy()) {
            newReturnType = Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
        } else if (returnType->isPointerTy()) {
            PointerType* ptrType = cast<PointerType>(returnType);
            unsigned addressSpace = ptrType->getAddressSpace();
            newReturnType = PointerType::get(newContext, addressSpace);
        } else if (returnType->isVoidTy()) {
            newReturnType = Type::getVoidTy(newContext);
        } else if (returnType->isFloatTy()) {
            newReturnType = Type::getFloatTy(newContext);
        } else if (returnType->isDoubleTy()) {
            newReturnType = Type::getDoubleTy(newContext);
        } else {
            newReturnType = PointerType::get(newContext, 0);
        }

        FunctionType* funcType = FunctionType::get(newReturnType, paramTypes, origFunc->isVarArg());

        Function* newFunc = Function::Create(
            funcType,
            origFunc->getLinkage(),
            origFunc->getName(),
            newModule.get()
        );

        newFunc->setCallingConv(origFunc->getCallingConv());
        newFunc->setVisibility(origFunc->getVisibility());
        newFunc->setDLLStorageClass(origFunc->getDLLStorageClass());

        // 标记函数已处理
        if (functionMap.count(origFunc)) {
            functionMap[origFunc].groupIndex = 2; // 孤立函数组编号为2
            functionMap[origFunc].isProcessed = true;
        }

        logger.logToFile("复制孤立函数: " + origFunc->getName().str() +
                    " [出度=0, 入度=0, 链接: " + getLinkageString(origFunc->getLinkage()) + "]");
    }

    return common.writeBitcodeSafely(*newModule, filename);
}

// 新增：统一的BC文件创建入口，支持两种模式
bool BCModuleSplitter::createBCFile(const unordered_set<Function*>& group,
                    const string& filename,
                    int groupIndex) {
    if (BCModuleSplitter::currentMode == CLONE_MODE) {
        return createBCFileWithClone(group, filename, groupIndex);
    } else {
        return createBCFileWithSignatures(group, filename, groupIndex);
    }
}

// 修改后的拆分方法 - 按照指定数量范围分组
void BCModuleSplitter::splitBCFiles(const string& outputPrefix) {
    logger.log("\n开始拆分BC文件...");
    logger.log("当前模式: " + string(BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE"));

    int fileCount = 0;
    auto& functionMap = common.getFunctionMap();

    // 步骤1: 首先处理全局变量组
    unordered_set<GlobalVariable*> globals = getGlobalVariables();

    if (!globals.empty()) {
        string filename = outputPrefix + "_group_globals.bc";

        if (createGlobalVariablesBCFile(globals, filename)) {
            logger.log("✓ 全局变量BC文件创建成功: " + filename);
            fileCount++;
        }
    }

    // 步骤2: 处理高入度函数组（包含完整的出度链）
    vector<Function*> highInDegreeFuncs = getHighInDegreeFunctions(500);

    if (!highInDegreeFuncs.empty()) {
        unordered_set<Function*> highInDegreeSet(highInDegreeFuncs.begin(), highInDegreeFuncs.end());
        string filename = outputPrefix + "_group_high_in_degree.bc";

        if (createBCFile(highInDegreeSet, filename, 1)) {
            unordered_set<Function*> completeHighInDegreeSet = getHighInDegreeWithOutDegreeFunctions(highInDegreeSet);

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

            if (verified) {
                // 显示高入度函数组的详细信息 - 完整打印
                logger.log("高入度函数组详情:");
                int highInDegreeCount = 0;
                int outDegreeCount = 0;

                // 首先打印高入度函数
                logger.log("=== 高入度函数列表 ===");
                for (Function* func : completeHighInDegreeSet) {
                    if (highInDegreeSet.find(func) != highInDegreeSet.end()) {
                        const FunctionInfo& info = functionMap[func];
                        string funcType = info.isUnnamed() ?
                            "无名函数 [序号:" + to_string(info.sequenceNumber) + "]" :
                            "有名函数";
                        logger.log("  高入度函数: " + info.displayName +
                            " [" + funcType +
                            ", 入度: " + to_string(info.inDegree) +
                            ", 出度: " + to_string(info.outDegree) +
                            ", 链接: " + getLinkageString(func->getLinkage()) + "]");
                        highInDegreeCount++;
                    }
                }

                // 然后打印出度函数
                logger.log("=== 出度函数列表 ===");
                for (Function* func : completeHighInDegreeSet) {
                    if (highInDegreeSet.find(func) == highInDegreeSet.end()) {
                        const FunctionInfo& info = functionMap[func];
                        string funcType = info.isUnnamed() ?
                            "无名函数 [序号:" + to_string(info.sequenceNumber) + "]" :
                            "有名函数";
                        logger.log("  出度函数: " + info.displayName +
                            " [" + funcType +
                            ", 入度: " + to_string(info.inDegree) +
                            ", 出度: " + to_string(info.outDegree) +
                            ", 链接: " + getLinkageString(func->getLinkage()) + "]");
                        outDegreeCount++;
                    }
                }

                logger.log("统计: 高入度函数: " + to_string(highInDegreeCount) + " 个");
                logger.log("统计: 出度函数: " + to_string(outDegreeCount) + " 个");
                logger.log("统计: 总计: " + to_string(completeHighInDegreeSet.size()) + " 个函数");
            } else {
                logger.logError("✗ 高入度函数BC文件验证失败: " + filename);
            }
            fileCount++;
        }
    }

    // 步骤3: 处理孤立函数组（出度=0且入度=0）
    vector<Function*> isolatedFuncs = getIsolatedFunctions();

    if (!isolatedFuncs.empty()) {
        unordered_set<Function*> isolatedSet(isolatedFuncs.begin(), isolatedFuncs.end());
        string filename = outputPrefix + "_group_isolated.bc";

        if (createBCFile(isolatedSet, filename, 2)) {
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

            if (verified) {
                // 显示孤立函数组的详细信息 - 完整打印
                logger.log("孤立函数组详情:");
                int isolatedCount = 0;
                for (Function* func : isolatedSet) {
                    const FunctionInfo& info = functionMap[func];
                    string funcType = info.isUnnamed() ?
                        "无名函数 [序号:" + to_string(info.sequenceNumber) + "]" :
                        "有名函数";
                    logger.log("  孤立函数: " + info.displayName +
                        " [" + funcType +
                        ", 入度: " + to_string(info.inDegree) +
                        ", 出度: " + to_string(info.outDegree) +
                        ", 链接: " + getLinkageString(func->getLinkage()) + "]");
                    isolatedCount++;
                }
                logger.log("统计: 总计: " + to_string(isolatedCount) + " 个孤立函数");
            } else {
                logger.logError("✗ 孤立函数BC文件验证失败: " + filename);
            }
            fileCount++;
        }
    }

    // 步骤4: 按照指定数量范围分组
    logger.log("开始按照指定数量范围分组...");

    // 获取所有未处理的函数并按总分排序
    vector<pair<Function*, int>> remainingFunctions = getRemainingFunctions();

    logger.log("剩余未处理函数数量: " + to_string(remainingFunctions.size()));

    // 定义分组范围
    vector<pair<int, int>> groupRanges = {
        {0, 100},      // 第3组: 前100个
        {100, 400},    // 第4组: 101-400
        {400, 1000},   // 第5组: 401-1000
        {1000, 2000},  // 第6组: 1001-2000
        {2000, 5000},  // 第7组: 2001-5000
        {5000, -1}     // 第8组: 5001-剩余所有
    };

    int groupIndex = 3; // 从组3开始

    for (const auto& range : groupRanges) {
        int start = range.first;
        int end = range.second;

        // 收集该范围内的函数
        unordered_set<Function*> group = getFunctionGroupByRange(remainingFunctions, start, end);

        if (group.empty()) {
            logger.log("组 " + to_string(groupIndex) + " 没有函数，跳过");
            groupIndex++;
            continue;
        }

        logger.log("处理组 " + to_string(groupIndex) + "，范围 [" +
            to_string(start) + "-" + (end == -1 ? "剩余所有" : to_string(end)) +
            "]，包含 " + to_string(group.size()) + " 个函数");

        // 创建BC文件
        string filename = outputPrefix + "_group_" + to_string(groupIndex) + ".bc";
        if (createBCFile(group, filename, groupIndex)) {
            // 验证并修复生成的BC文件
            bool verified = false;
            if (BCModuleSplitter::currentMode == CLONE_MODE) {
                if (quickValidateBCFile(filename)) {
                    verified = true;
                    logger.log("✓ Clone模式分组BC文件验证通过: " + filename);
                }
            } else {
                if (verifyAndFixBCFile(filename, group)) {
                    verified = true;
                    logger.log("✓ 分组BC文件验证通过: " + filename);
                }
            }

            if (verified) {
                // 显示组中的所有函数 - 完整打印
                logger.log("组 " + to_string(groupIndex) + " 函数列表:");
                int funcCount = 0;
                for (Function* f : group) {
                    const FunctionInfo& info = functionMap[f];
                    string funcType = info.isUnnamed() ?
                        "无名函数 [序号:" + to_string(info.sequenceNumber) + "]" :
                        "有名函数";
                    logger.log("  " + to_string(++funcCount) + ". " + info.displayName +
                        " [" + funcType +
                        ", 入度: " + to_string(info.inDegree) +
                        ", 出度: " + to_string(info.outDegree) +
                        ", 总分: " + to_string(info.inDegree + info.outDegree) +
                        ", 链接: " + getLinkageString(f->getLinkage()) + "]");
                }
                logger.log("组 " + to_string(groupIndex) + " 统计: 共 " + to_string(group.size()) + " 个函数");
            } else {
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
    logger.log("共生成 " + to_string(fileCount) + " 个分组BC文件");
    logger.log("使用模式: " + string(BCModuleSplitter::currentMode == CLONE_MODE ? "CLONE_MODE" : "MANUAL_MODE"));

    // 统计处理情况
    int processedCount = 0;
    for (const auto& pair : functionMap) {
        if (pair.second.isProcessed) {
            processedCount++;
        }
    }
    logger.log("已处理 " + to_string(processedCount) + " / " + to_string(functionMap.size()) + " 个函数");

    if (processedCount < functionMap.size()) {
        logger.logWarning("警告: 有 " + to_string(functionMap.size() - processedCount) + " 个函数未被处理");

        // 显示所有未处理的函数 - 完整打印
        logger.log("未处理函数完整列表:");
        int unprocessedCount = 0;
        for (const auto& pair : functionMap) {
            if (!pair.second.isProcessed) {
                const FunctionInfo& info = pair.second;
                string funcType = info.isUnnamed() ?
                    "无名函数 [序号:" + to_string(info.sequenceNumber) + "]" :
                    "有名函数";
                logger.log("  " + to_string(++unprocessedCount) + ". " + info.displayName +
                    " [" + funcType +
                    ", 出度: " + to_string(info.outDegree) +
                    ", 入度: " + to_string(info.inDegree) +
                    ", 总分: " + to_string(info.outDegree + info.inDegree) +
                    ", 链接: " + (info.funcPtr ? getLinkageString(info.funcPtr->getLinkage()) : "N/A") + "]");
            }
        }
        logger.log("未处理函数统计: 共 " + to_string(unprocessedCount) + " 个函数");
    }
}

// 优化的BC文件创建方法，正确处理链接属性
bool BCModuleSplitter::createBCFileWithSignatures(const unordered_set<Function*>& group, const string& filename, int groupIndex) {
    logger.logToFile("创建BC文件: " + filename + " (组 " + to_string(groupIndex) + ")");

    // 使用全新的上下文
    LLVMContext newContext;
    llvm::Module* module = common.getModule();
    auto& functionMap = common.getFunctionMap();
    auto newModule = make_unique<Module>(filename, newContext);

    // 复制原始模块的基本属性
    newModule->setTargetTriple(module->getTargetTriple());
    newModule->setDataLayout(module->getDataLayout());

    // 为每个函数创建签名并保持正确的链接属性
    for (Function* origFunc : group) {
        if (!origFunc) continue;

        // 在新建上下文中重新创建函数类型
        vector<Type*> paramTypes;
        for (const auto& arg : origFunc->args()) {
            Type* argType = arg.getType();

            // 将类型映射到新上下文中的对应类型
            if (argType->isIntegerTy()) {
                paramTypes.push_back(Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
            } else if (argType->isPointerTy()) {
                PointerType* ptrType = cast<PointerType>(argType);
                unsigned addressSpace = ptrType->getAddressSpace();
                paramTypes.push_back(PointerType::get(newContext, addressSpace));
            } else if (argType->isVoidTy()) {
                paramTypes.push_back(Type::getVoidTy(newContext));
            } else if (argType->isFloatTy()) {
                paramTypes.push_back(Type::getFloatTy(newContext));
            } else if (argType->isDoubleTy()) {
                paramTypes.push_back(Type::getDoubleTy(newContext));
            } else {
                paramTypes.push_back(PointerType::get(newContext, 0));
            }
        }

        // 确定返回类型
        Type* returnType = origFunc->getReturnType();
        Type* newReturnType;

        if (returnType->isIntegerTy()) {
            newReturnType = Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
        } else if (returnType->isPointerTy()) {
            PointerType* ptrType = cast<PointerType>(returnType);
            unsigned addressSpace = ptrType->getAddressSpace();
            newReturnType = PointerType::get(newContext, addressSpace);
        } else if (returnType->isVoidTy()) {
            newReturnType = Type::getVoidTy(newContext);
        } else if (returnType->isFloatTy()) {
            newReturnType = Type::getFloatTy(newContext);
        } else if (returnType->isDoubleTy()) {
            newReturnType = Type::getDoubleTy(newContext);
        } else {
            newReturnType = PointerType::get(newContext, 0);
        }

        // 创建函数类型
        FunctionType* funcType = FunctionType::get(newReturnType, paramTypes, origFunc->isVarArg());

        // 创建函数并保持正确的链接属性
        Function* newFunc = Function::Create(
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

        logger.logToFile("复制函数: " + origFunc->getName().str() +
                    " [链接: " + getLinkageString(origFunc->getLinkage()) +
                    ", 可见性: " + getVisibilityString(origFunc->getVisibility()) + "]");
    }

    // 写入bitcode
    return common.writeBitcodeSafely(*newModule, filename);
}

// 新增：使用LLVM CloneModule创建BC文件
bool BCModuleSplitter::createBCFileWithClone(const unordered_set<Function*>& group,
                            const string& filename,
                            int groupIndex) {
    logger.logToFile("使用Clone模式创建BC文件: " + filename + " (组 " + to_string(groupIndex) + ")");

    // 使用ValueToValueMapTy进行克隆
    ValueToValueMapTy vmap;
    llvm::Module* module = common.getModule();
    auto& functionMap = common.getFunctionMap();
    auto newModule = CloneModule(*module, vmap);

    if (!newModule) {
        logger.logError("CloneModule失败: " + filename);
        return false;
    }

    // 设置模块名称
    newModule->setModuleIdentifier("cloned_group_" + to_string(groupIndex));

    // 处理函数：保留组内函数定义，其他转为声明
    processClonedModuleFunctions(*newModule, group);

    // 处理全局变量：全部转为external声明
    processClonedModuleGlobals(*newModule);

    // 标记原始函数已处理
    for (Function* origFunc : group) {
        if (functionMap.count(origFunc)) {
            functionMap[origFunc].groupIndex = groupIndex;
            functionMap[origFunc].isProcessed = true;
        }
    }

    logger.logToFile("Clone模式完成: " + filename + " (包含 " + to_string(group.size()) + " 个函数)");
    return common.writeBitcodeSafely(*newModule, filename);
}

// 新增：处理克隆模块中的函数
void BCModuleSplitter::processClonedModuleFunctions(Module& clonedModule, const unordered_set<Function*>& targetGroup) {
    unordered_set<string> targetFunctionNames;

    // 收集目标函数名称
    for (Function* func : targetGroup) {
        if (func) {
            targetFunctionNames.insert(func->getName().str());
        }
    }

    // 处理所有函数
    vector<Function*> functionsToProcess;
    for (Function& func : clonedModule) {
        functionsToProcess.push_back(&func);
    }

    for (Function* func : functionsToProcess) {
        string funcName = func->getName().str();

        if (targetFunctionNames.find(funcName) != targetFunctionNames.end()) {
            // 目标函数：保留为定义，设置合适的链接属性
            if (func->getLinkage() == GlobalValue::InternalLinkage ||
                func->getLinkage() == GlobalValue::PrivateLinkage) {
                func->setLinkage(GlobalValue::ExternalLinkage);
                func->setVisibility(GlobalValue::DefaultVisibility);
                logger.logToFile("目标函数链接调整: " + funcName + " -> External");
            }
        } else {
            // 非目标函数：转为声明
            if (!func->isDeclaration()) {
                func->deleteBody();
                func->setLinkage(GlobalValue::ExternalLinkage);
                func->setVisibility(GlobalValue::DefaultVisibility);
                // 清除dso_local属性
                func->setDSOLocal(false);
                logger.logToFile("非目标函数转为声明: " + funcName);
            }
        }
    }
}

// 新增：处理克隆模块中的全局变量
void BCModuleSplitter::processClonedModuleGlobals(Module& clonedModule) {
    for (GlobalVariable& global : clonedModule.globals()) {
        global.setDSOLocal(false);
        // 移除初始值，转为外部声明
        if (global.hasInitializer()) {
            global.setInitializer(nullptr);
        }

        // 设置external链接和默认可见性
        global.setLinkage(GlobalValue::ExternalLinkage);
        global.setVisibility(GlobalValue::DefaultVisibility);

        logger.logToFile("全局变量处理: " + global.getName().str() + " -> External");
    }
}

// 新增：获取剩余函数列表
vector<pair<Function*, int>> BCModuleSplitter::getRemainingFunctions() {
    vector<pair<Function*, int>> remainingFunctions;
    auto& functionMap = common.getFunctionMap();

    for (const auto& pair : functionMap) {
        if (!pair.second.isProcessed) {
            int totalScore = pair.second.outDegree + pair.second.inDegree;
            remainingFunctions.emplace_back(pair.first, totalScore);
        }
    }

    // 按总分降序排序
    std::sort(remainingFunctions.begin(), remainingFunctions.end(),
        [](const pair<Function*, int>& a, const pair<Function*, int>& b) {
            return a.second > b.second;
        });

    return remainingFunctions;
}

// 新增：按范围获取函数组
unordered_set<Function*> BCModuleSplitter::getFunctionGroupByRange(const vector<pair<Function*, int>>& functions,
                                                int start, int end) {
    unordered_set<Function*> group;
    auto& functionMap = common.getFunctionMap();

    for (int i = start; i < functions.size(); i++) {
        if (end != -1 && i >= end) break;

        Function* func = functions[i].first;
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