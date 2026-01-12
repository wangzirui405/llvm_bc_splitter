#include "common.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
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
#include <filesystem>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/MemoryBuffer.h>
#include <stack>

// 打印单个GroupInfo的详细信息
void GroupInfo::printDetails() const {
    std::cout << "========== GroupInfo Details ==========" << std::endl;
    std::cout << "Group ID: " << groupId << std::endl;
    std::cout << "BC File: " << bcFile << std::endl;
    std::cout << "Has Konan Cxa Demangle: " << (hasKonanCxaDemangle ? "true" : "false") << std::endl;

    // 打印依赖项
    std::cout << "Dependencies (" << dependencies.size() << "): ";
    if (dependencies.empty()) {
        std::cout << "None";
    } else {
        bool first = true;
        for (int dep : dependencies) {
            if (!first)
                std::cout << ", ";
            std::cout << dep;
            first = false;
        }
    }
    std::cout << std::endl;
    std::cout << "======================================" << std::endl;
}

BCCommon::BCCommon() : context(nullptr) {}

BCCommon::~BCCommon() { clear(); }

void BCCommon::clear() {
    module.reset();
    globalValueMap.clear();
    cyclicGroups.clear();
    globalValueToGroupMap.clear();
    context = nullptr;
    GlobalValueNameMatcher.invalidateCache(); // 清理缓存
}

bool BCCommon::isNumberString(llvm::StringRef str) {
    if (str.empty())
        return false;
    for (char c : str) {
        if (!std::isdigit(c))
            return false;
    }
    return true;
}

llvm::SmallVector<int, 32> BCCommon::convertIndexToFiltered(
    const llvm::SmallVector<llvm::DenseSet<llvm::GlobalValue *>, 32> &globalValuesAllGroups) {
    llvm::SmallVector<int, 32> pOriginalToNewIndex;
    int newIndex = 0;
    for (unsigned i = 0; i < globalValuesAllGroups.size(); i++) {
        if (!globalValuesAllGroups[i].empty()) {
            pOriginalToNewIndex.push_back(newIndex++);
        } else {
            pOriginalToNewIndex.push_back(0);
        }
    }
    return pOriginalToNewIndex;
}

std::string BCCommon::renameUnnamedGlobalValues(llvm::StringRef filename) {

    llvm::ValueToValueMapTy VMap;
    llvm::SMDiagnostic err;
    std::string newfilename = "renamed_" + filename.str();
    // 创建新的上下文
    llvm::LLVMContext *context = new llvm::LLVMContext();

    auto M = llvm::parseIRFile(filename, err, *context);
    // 克隆模块
    std::unique_ptr<llvm::Module> newM = llvm::CloneModule(*M, VMap);

    // 计数器用于生成唯一名称
    unsigned globalVarCounter = 0;
    unsigned globalAliasCounter = 0;
    unsigned funcCounter = 0;

    // 第一步：重命名全局变量
    for (auto &GVar : newM->globals()) {
        std::string oldName = GVar.getName().str();

        // 检查是否未命名或名称以数字开头（通常是未命名的情况）
        if (oldName.empty() || (oldName[0] >= '0' && oldName[0] <= '9')) {

            // 跳过以"llvm."开头的特殊全局变量
            if (oldName.substr(0, 5) == "llvm.") {
                continue;
            }

            // 生成新名称
            std::string newName = "renamed_global_var_" + std::to_string(globalVarCounter++);

            // 确保名称唯一
            while (newM->getNamedValue(newName)) {
                newName = "renamed_global_var_" + std::to_string(globalVarCounter++);
            }

            // 直接重命名
            GVar.setName(newName);
            // std::cout << "Renamed global variable: " << oldName << " -> " << newName << std::endl;
        }
    }

    // 第二步：重命名符号
    for (auto &F : newM->functions()) {
        std::string oldName = F.getName().str();

        // 检查是否未命名或名称以数字开头
        if (oldName.empty() || (oldName[0] >= '0' && oldName[0] <= '9')) {

            // 跳过LLVM内部符号
            if (oldName.substr(0, 5) == "llvm.") {
                continue;
            }

            // 生成新名称
            std::string newName = "renamed_func_" + std::to_string(funcCounter++);

            // 确保名称唯一
            while (newM->getNamedValue(newName)) {
                newName = "renamed_func_" + std::to_string(funcCounter++);
            }

            // 直接重命名
            F.setName(newName);
            // std::cout << "Renamed function: " << oldName << " -> " << newName << std::endl;
        }
    }

    // 第三步：重命名全局别名
    for (auto &GA : newM->aliases()) {
        std::string oldName = GA.getName().str();

        if (oldName.empty() || (oldName[0] >= '0' && oldName[0] <= '9')) {

            // 生成新名称
            std::string newName = "renamed_alias_" + std::to_string(globalAliasCounter++);

            // 确保名称唯一
            while (newM->getNamedValue(newName)) {
                newName = "renamed_alias_" + std::to_string(globalAliasCounter++);
            }

            // 直接重命名
            GA.setName(newName);
            // std::cout << "Renamed alias: " << oldName << " -> " << newName << std::endl;
        }
    }
    writeBitcodeSafely(*newM, newfilename);
    return newfilename;
}

// 安全的bitcode写入方法
bool BCCommon::writeBitcodeSafely(llvm::Module &M, llvm::StringRef filename) {
    logger.logToFile("✓ 安全写入bitcode: " + filename.str());

    std::error_code ec;
    llvm::raw_fd_ostream outFile(config.workSpace + "output/" + filename.str(), ec, llvm::sys::fs::OF_None);
    if (ec) {
        logger.logError("无法创建文件: " + filename.str() + " - " + ec.message());
        return false;
    }

    try {
        llvm::WriteBitcodeToFile(M, outFile);
        outFile.close();
        logger.log("✓ 成功写入: " + filename.str());
        return true;
    } catch (const std::exception &e) {
        logger.logError("写入bitcode时发生异常: " + std::string(e.what()));
        llvm::sys::fs::remove(filename);
        return false;
    }
}

/**
 * @brief 检测并记录所有存在循环调用的符号组
 *
 * 使用Tarjan算法查找符号调用图中的强连通分量（SCC）
 * 强连通分量即存在循环调用的符号组
 */
void BCCommon::findCyclicGroups() {
    cyclicGroups.clear();
    globalValueToGroupMap.clear();

    if (globalValueMap.empty()) {
        logger.logWarning("GlobalValueMap is empty, no cyclic groups to find.");
        return;
    }

    // 构建调用图
    llvm::DenseMap<llvm::GlobalValue *, llvm::DenseSet<llvm::GlobalValue *>> callGraph;
    llvm::DenseMap<llvm::GlobalValue *, int> indices;
    llvm::DenseMap<llvm::GlobalValue *, int> lowlinks;
    llvm::DenseMap<llvm::GlobalValue *, bool> onStack;
    std::stack<llvm::GlobalValue *> stack;
    int index = 0;

    // 初始化调用图
    for (auto &pair : globalValueMap) {
        llvm::GlobalValue *GV = pair.first;
        callGraph[GV] = llvm::DenseSet<llvm::GlobalValue *>();

        // 添加直接调用关系
        for (auto calledFunc : pair.second.calleds) {
            if (globalValueMap.find(calledFunc) != globalValueMap.end()) {
                callGraph[GV].insert(calledFunc);
            }
        }
    }

    // Tarjan算法实现
    std::function<void(llvm::GlobalValue *)> strongConnect;
    strongConnect = [&](llvm::GlobalValue *v) {
        indices[v] = index;
        lowlinks[v] = index;
        index++;
        stack.push(v);
        onStack[v] = true;

        // 遍历所有邻居（被调用的符号）
        for (auto w : callGraph[v]) {
            if (indices.find(w) == indices.end()) {
                // w未访问过
                strongConnect(w);
                lowlinks[v] = std::min(lowlinks[v], lowlinks[w]);
            } else if (onStack[w]) {
                // w在栈中，说明找到回边
                lowlinks[v] = std::min(lowlinks[v], indices[w]);
            }
        }

        // 如果v是强连通分量的根
        if (lowlinks[v] == indices[v]) {
            llvm::DenseSet<llvm::GlobalValue *> scc;
            llvm::GlobalValue *w;
            do {
                w = stack.top();
                stack.pop();
                onStack[w] = false;
                scc.insert(w);
            } while (w != v);

            // 只记录非平凡的强连通分量（大小>1）
            if (scc.size() > 1) {
                cyclicGroups.push_back(scc);
                int groupIndex = cyclicGroups.size() - 1;

                // 更新符号到组的映射
                for (auto GV : scc) {
                    globalValueToGroupMap[GV].push_back(groupIndex);
                }
            }
        }
    };

    // 对每个未访问的符号执行算法
    for (auto &pair : globalValueMap) {
        llvm::GlobalValue *GV = pair.first;
        if (indices.find(GV) == indices.end()) {
            strongConnect(GV);
        }
    }

    logger.logToFile("找到的循环群总数: " + std::to_string(cyclicGroups.size()));
}

/**
 * @brief 根据llvm::GlobalValue*查询符号所在的循环调用组
 * @param GV 要查询的符号指针
 * @return 包含该符号的所有循环调用组（每个组是一个符号集合）
 */

llvm::DenseSet<llvm::GlobalValue *> BCCommon::getCyclicGroupsContainingGlobalValue(llvm::GlobalValue *GV) {
    if (!GV) {
        logger.logWarning("查询时提供的符号指针为空。");
        return llvm::DenseSet<llvm::GlobalValue *>();
    }

    auto it = globalValueToGroupMap.find(GV);
    if (it == globalValueToGroupMap.end()) {
        return llvm::DenseSet<llvm::GlobalValue *>();
    }

    llvm::DenseSet<llvm::GlobalValue *> allRelateds;

    // 收集所有相关符号
    for (int groupIndex : it->second) {
        if (groupIndex >= 0 && groupIndex < cyclicGroups.size()) {
            allRelateds.insert(cyclicGroups[groupIndex].begin(), cyclicGroups[groupIndex].end());
        }
    }

    return allRelateds;
}

/**
 * @brief 每个group获取符号calleds中所有符号的groupIndex（去重）
 *
 * @return llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> 每个group去重后的groupIndex矩阵信息
 *         如果传入的符号不在globalValueMap中，返回空矩阵
 *         如果calleds中的符号不在globalValueMap中，跳过该符号
 */
llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> BCCommon::getGroupDependencies() {
    auto &globalValueMap = getGlobalValueMap();
    auto &globalValuesAllGroups = getGlobalValuesAllGroups();
    llvm::SmallVector<int, 32> cacheMapForGVGroups = convertIndexToFiltered(globalValuesAllGroups);

    int maxGroupIndex = -1;
    for (int i = 0; i < cacheMapForGVGroups.size(); i++) {
        maxGroupIndex = std::max(maxGroupIndex, cacheMapForGVGroups[i]);
    }

    if (maxGroupIndex < 0) {
        return {};
    }

    // 使用 SetVector 保持插入顺序并去重
    llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> groupDependencies;
    groupDependencies.resize(maxGroupIndex + 1);

    for (const auto &pair : globalValueMap) {
        const GlobalValueInfo &gvInfo = pair.second;
        int gvIndex = gvInfo.groupIndex;

        if (gvIndex < 0) {
            continue;
        }

        for (llvm::GlobalValue *called : gvInfo.calleds) {
            auto calledIt = globalValueMap.find(called);
            if (calledIt != globalValueMap.end()) {
                int groupIdx = calledIt->second.groupIndex;
                if (groupIdx >= 0 && groupIdx != gvIndex) {
                    groupDependencies[gvIndex].insert(groupIdx);
                }
            }
        }
    }

    return groupDependencies;
}

// 当globalValueMap被外部修改时调用此方法
void BCCommon::invalidateGlobalValueNameCache() { GlobalValueNameMatcher.invalidateCache(); }

// 重建符号名缓存
void BCCommon::rebuildGlobalValueNameCache() {
    if (!globalValueMap.empty()) {
        GlobalValueNameMatcher.rebuildCache(globalValueMap);
        logger.log("记录：已缓存" + std::to_string(GlobalValueNameMatcher.getCacheSize()) + "个名字");
    }
}

// 确保缓存有效
void BCCommon::ensureCacheValid() {
    if (!GlobalValueNameMatcher.isCacheValid() && !globalValueMap.empty()) {
        rebuildGlobalValueNameCache();
    }
}

// 检查字符串是否包含任何符号名
bool BCCommon::containsGlobalValueNameInString(llvm::StringRef str) {
    ensureCacheValid();
    return GlobalValueNameMatcher.containsGlobalValueName(str);
}

// 获取匹配的符号名列表
llvm::StringSet<> BCCommon::getMatchingGlobalValueNames(llvm::StringRef str) {
    ensureCacheValid();
    auto matches = GlobalValueNameMatcher.getMatchingGlobalValues(str);

    llvm::StringSet<> result;
    for (const auto &entry : matches) {
        llvm::StringRef key = entry.getKey();
        if (str.find(key) != llvm::StringRef::npos) {
            result.insert(key);
        }
    }
    return result;
}

// 获取匹配的符号指针列表
llvm::DenseSet<llvm::GlobalValue *> BCCommon::getMatchingGlobalValues(llvm::StringRef str) {
    ensureCacheValid();
    auto matches = GlobalValueNameMatcher.getMatchingGlobalValues(str);

    llvm::DenseSet<llvm::GlobalValue *> result;
    for (const auto &match : matches) {
        result.insert(match.second);
    }
    return result;
}

// 获取首个匹配的符号指针
llvm::GlobalValue *BCCommon::getFirstMatchingGlobalValue(llvm::StringRef str) {
    ensureCacheValid();
    auto matches = GlobalValueNameMatcher.getMatchingGlobalValues(str);

    llvm::GlobalValue *result;
    for (const auto &match : matches) {
        result = match.second;
    }
    return result;
}

// 获取缓存状态
bool BCCommon::isGlobalValueNameCacheValid() const { return GlobalValueNameMatcher.isCacheValid(); }

size_t BCCommon::getGlobalValueNameCacheSize() const { return GlobalValueNameMatcher.getCacheSize(); }

// 用于从常量中收集GlobalValue（包括函数和全局变量）
void BCCommon::collectGlobalValuesFromConstant(llvm::Constant *C, llvm::DenseSet<llvm::GlobalValue *> &globalValueSet) {
    if (!C)
        return;

    // 如果是GlobalValue本身
    if (auto *GV = llvm::dyn_cast<llvm::GlobalValue>(C)) {
        globalValueSet.insert(GV);
        return;
    }

    // 如果是GlobalAlias，获取其目标
    if (auto *GA = llvm::dyn_cast<llvm::GlobalAlias>(C)) {
        if (auto *aliasee = GA->getAliasee()) {
            collectGlobalValuesFromConstant(llvm::dyn_cast<llvm::Constant>(aliasee), globalValueSet);
        }
        return;
    }

    // 处理常量表达式
    if (auto *CE = llvm::dyn_cast<llvm::ConstantExpr>(C)) {
        // 递归处理常量表达式的操作数
        for (unsigned i = 0, e = CE->getNumOperands(); i < e; ++i) {
            collectGlobalValuesFromConstant(llvm::cast<llvm::Constant>(CE->getOperand(i)), globalValueSet);
        }
        return;
    }

    // 处理聚合常量
    if (auto *CA = llvm::dyn_cast<llvm::ConstantArray>(C)) {
        for (unsigned i = 0, e = CA->getNumOperands(); i < e; ++i) {
            collectGlobalValuesFromConstant(CA->getOperand(i), globalValueSet);
        }
        return;
    }

    if (auto *CS = llvm::dyn_cast<llvm::ConstantStruct>(C)) {
        for (unsigned i = 0, e = CS->getNumOperands(); i < e; ++i) {
            collectGlobalValuesFromConstant(CS->getOperand(i), globalValueSet);
        }
        return;
    }

    if (auto *CV = llvm::dyn_cast<llvm::ConstantVector>(C)) {
        for (unsigned i = 0, e = CV->getNumOperands(); i < e; ++i) {
            collectGlobalValuesFromConstant(CV->getOperand(i), globalValueSet);
        }
        return;
    }

    // 处理BlockAddress
    if (auto *BA = llvm::dyn_cast<llvm::BlockAddress>(C)) {
        llvm::Function *F = BA->getFunction();
        globalValueSet.insert(F);
        return;
    }

    // 其他常量类型，如ConstantInt、ConstantFP、ConstantDataSequential等，不会引用GlobalValue
}

// 辅助函数：从User中查找其所属的GlobalValue
llvm::GlobalValue *BCCommon::findGlobalValueFromUser(llvm::User *U) {
    if (!U)
        return nullptr;

    // 使用集合记录已访问的User，避免无限递归
    llvm::DenseSet<llvm::User *> visited;

    // 递归查找
    while (true) {
        if (!U || !visited.insert(U).second) {
            return nullptr;
        }

        // 如果User本身就是GlobalValue
        if (auto *GV = llvm::dyn_cast<llvm::GlobalValue>(U)) {
            return GV;
        }

        // 如果User是指令，获取所在的函数
        if (auto *I = llvm::dyn_cast<llvm::Instruction>(U)) {
            return I->getFunction();
        }

        // 如果User是基本块，获取所在的函数
        if (auto *BB = llvm::dyn_cast<llvm::BasicBlock>(U)) {
            return BB->getParent();
        }

        // 对于其他User，向上查找其users
        if (U->hasNUses(0)) {
            return nullptr;
        }

        // 取第一个user继续查找
        U = *(U->user_begin());
    }
}

// 统一的调用关系分析函数
void BCCommon::analyzeCallRelations() {
    // 清空现有的调用关系（如果需要重新分析）
    for (auto &pair : globalValueMap) {
        pair.second.callers.clear();
        pair.second.calleds.clear();
        pair.second.outDegree = 0;
        pair.second.inDegree = 0;
        if (pair.second.type == GlobalValueType::FUNCTION) {
            pair.second.funcSpecific.personalityCalledFunctions.clear();
            pair.second.funcSpecific.personalityCallerFunctions.clear();
        }
    }

    // 第一阶段：分析GlobalVariable的初始值
    for (auto &pair : globalValueMap) {
        llvm::GlobalValue *GV = pair.first;

        if (auto *GlobalVar = llvm::dyn_cast<llvm::GlobalVariable>(GV)) {
            GlobalValueInfo &gvInfo = pair.second;

            // 处理全局变量的初始值
            if (GlobalVar->hasInitializer()) {
                llvm::Constant *initializer = GlobalVar->getInitializer();
                llvm::DenseSet<llvm::GlobalValue *> referencedValues;

                // 收集初始值中引用的所有GlobalValue
                collectGlobalValuesFromConstant(initializer, referencedValues);

                // 记录调用关系
                for (llvm::GlobalValue *refGV : referencedValues) {
                    if (refGV != GV && globalValueMap.count(refGV)) {
                        // GV引用了refGV
                        gvInfo.calleds.insert(refGV);
                        GlobalValueInfo &refInfo = globalValueMap[refGV];
                        refInfo.callers.insert(GV);
                    }
                }
            }
        }
    }

    // 第二阶段：分析Function的指令和uses
    for (auto &pair : globalValueMap) {
        llvm::GlobalValue *GV = pair.first;

        if (auto *F = llvm::dyn_cast<llvm::Function>(GV)) {
            GlobalValueInfo &funcInfo = pair.second;

            // 1. 处理personality函数（异常处理函数）
            if (F->hasPersonalityFn()) {
                if (auto *personality = F->getPersonalityFn()) {
                    if (auto *personalityF = llvm::dyn_cast<llvm::Function>(personality)) {
                        if (globalValueMap.count(personalityF)) {
                            // 记录personality符号调用关系
                            funcInfo.funcSpecific.personalityCalledFunctions.insert(personalityF);
                            GlobalValueInfo &personalityInfo = globalValueMap[personalityF];
                            personalityInfo.funcSpecific.personalityCallerFunctions.insert(F);

                            // 同时也记录到普通的调用关系中
                            funcInfo.calleds.insert(personalityF);
                            personalityInfo.callers.insert(F);
                        }
                    }
                }
            }

            // 跳过声明（只有函数体才需要分析）
            if (F->isDeclaration()) {
                continue;
            }

            // 2. 处理被调用者（函数体中的指令）
            for (llvm::BasicBlock &BB : *F) {
                for (llvm::Instruction &I : BB) {
                    // 处理直接调用
                    if (auto *callInst = llvm::dyn_cast<llvm::CallInst>(&I)) {
                        llvm::Value *calledValue = callInst->getCalledOperand();
                        if (!calledValue)
                            continue;

                        calledValue = calledValue->stripPointerCasts();
                        if (auto *calledF = llvm::dyn_cast<llvm::Function>(calledValue)) {
                            if (calledF != F && globalValueMap.count(calledF)) {
                                funcInfo.calleds.insert(calledF);
                                GlobalValueInfo &calledInfo = globalValueMap[calledF];
                                calledInfo.callers.insert(F);
                            }
                        }
                    }
                    // 处理调用指令的变体（如InvokeInst）
                    else if (auto *invokeInst = llvm::dyn_cast<llvm::InvokeInst>(&I)) {
                        llvm::Value *calledValue = invokeInst->getCalledOperand();
                        if (!calledValue)
                            continue;

                        calledValue = calledValue->stripPointerCasts();
                        if (auto *calledF = llvm::dyn_cast<llvm::Function>(calledValue)) {
                            if (calledF != F && globalValueMap.count(calledF)) {
                                funcInfo.calleds.insert(calledF);
                                GlobalValueInfo &calledInfo = globalValueMap[calledF];
                                calledInfo.callers.insert(F);
                            }
                        }
                    }
                    // 处理间接调用（通过函数指针）
                    else if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(&I)) {
                        llvm::Value *addr = loadInst->getPointerOperand();
                        addr = addr->stripPointerCasts();

                        // 如果是全局变量，检查其初始值中是否有函数
                        if (auto *globalVar = llvm::dyn_cast<llvm::GlobalVariable>(addr)) {
                            if (globalVar->hasInitializer()) {
                                llvm::Constant *init = globalVar->getInitializer();
                                llvm::DenseSet<llvm::GlobalValue *> referencedValues;
                                collectGlobalValuesFromConstant(init, referencedValues);

                                for (llvm::GlobalValue *refGV : referencedValues) {
                                    if (refGV != F && globalValueMap.count(refGV)) {
                                        funcInfo.calleds.insert(refGV);
                                        GlobalValueInfo &refInfo = globalValueMap[refGV];
                                        refInfo.callers.insert(F);
                                    }
                                }
                            }
                        }
                    }
                    // 处理其他可能引用GlobalValue的指令
                    else {
                        // 遍历指令的所有操作数
                        for (unsigned i = 0; i < I.getNumOperands(); i++) {
                            llvm::Value *operand = I.getOperand(i);
                            if (!operand)
                                continue;

                            operand = operand->stripPointerCasts();

                            // 如果是GlobalValue
                            if (auto *globalVal = llvm::dyn_cast<llvm::GlobalValue>(operand)) {
                                if (globalVal != F && globalValueMap.count(globalVal)) {
                                    funcInfo.calleds.insert(globalVal);
                                    GlobalValueInfo &opInfo = globalValueMap[globalVal];
                                    opInfo.callers.insert(F);
                                }
                            }
                            // 如果是常量，检查其中是否包含GlobalValue
                            else if (auto *constant = llvm::dyn_cast<llvm::Constant>(operand)) {
                                llvm::DenseSet<llvm::GlobalValue *> referencedValues;
                                collectGlobalValuesFromConstant(constant, referencedValues);

                                for (llvm::GlobalValue *refGV : referencedValues) {
                                    if (refGV != F && globalValueMap.count(refGV)) {
                                        funcInfo.calleds.insert(refGV);
                                        GlobalValueInfo &refInfo = globalValueMap[refGV];
                                        refInfo.callers.insert(F);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 3. 处理调用者（通过uses分析）
            llvm::SmallVector<llvm::User *, 32> users;
            for (llvm::Use &use : F->uses()) {
                if (llvm::User *U = use.getUser()) {
                    users.push_back(U);
                }
            }

            for (llvm::User *U : users) {
                llvm::GlobalValue *callerGV = findGlobalValueFromUser(U);
                if (callerGV && callerGV != F && globalValueMap.count(callerGV)) {
                    // 记录调用关系
                    funcInfo.callers.insert(callerGV);
                    GlobalValueInfo &callerInfo = globalValueMap[callerGV];
                    callerInfo.calleds.insert(F);
                }
            }
        }
    }

    // 第三阶段：处理GlobalVariable的uses（调用者）
    for (auto &pair : globalValueMap) {
        llvm::GlobalValue *GV = pair.first;

        if (auto *GlobalVar = llvm::dyn_cast<llvm::GlobalVariable>(GV)) {
            GlobalValueInfo &gvInfo = pair.second;

            // 处理所有uses
            llvm::SmallVector<llvm::User *, 32> users;
            for (llvm::Use &use : GlobalVar->uses()) {
                if (llvm::User *U = use.getUser()) {
                    users.push_back(U);
                }
            }

            for (llvm::User *U : users) {
                llvm::GlobalValue *callerGV = findGlobalValueFromUser(U);
                if (callerGV && callerGV != GV && globalValueMap.count(callerGV)) {
                    // 记录调用关系
                    gvInfo.callers.insert(callerGV);
                    GlobalValueInfo &callerInfo = globalValueMap[callerGV];
                    callerInfo.calleds.insert(GV);
                }
            }
        }
    }

    // 第四阶段：计算入度和出度
    for (auto &pair : globalValueMap) {
        GlobalValueInfo &info = pair.second;
        info.inDegree = info.callers.size();
        info.outDegree = info.calleds.size();
    }

    // 第五阶段：验证和清理（可选）
    // 确保调用关系的对称性
    for (auto &pair : globalValueMap) {
        llvm::GlobalValue *GV = pair.first;
        GlobalValueInfo &info = pair.second;

        // 检查所有callers中的GV是否确实将当前GV作为called
        for (llvm::GlobalValue *caller : info.callers) {
            if (globalValueMap.count(caller)) {
                auto &callerInfo = globalValueMap[caller];
                if (!callerInfo.calleds.count(GV)) {
                    // 修复不对称的调用关系
                    callerInfo.calleds.insert(GV);
                    callerInfo.outDegree = callerInfo.calleds.size();
                }
            }
        }

        // 检查所有calleds中的GV是否确实将当前GV作为caller
        for (llvm::GlobalValue *called : info.calleds) {
            if (globalValueMap.count(called)) {
                auto &calledInfo = globalValueMap[called];
                if (!calledInfo.callers.count(GV)) {
                    // 修复不对称的调用关系
                    calledInfo.callers.insert(GV);
                    calledInfo.inDegree = calledInfo.callers.size();
                }
            }
        }
    }
}

void GlobalValueNameMatcher::rebuildCache(const llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> &globalValueMap) {
    std::lock_guard<std::mutex> lock(cacheMutex);

    nameCache.clear();
    for (const auto &[F, info] : globalValueMap) {
        if (!info.displayName.empty()) {
            nameCache.insert({info.displayName, F});
        }
    }
    if (globalValueMap.size() > 100)
        cacheValid = true;
}

void GlobalValueNameMatcher::invalidateCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cacheValid = false;
}

bool GlobalValueNameMatcher::containsGlobalValueName(llvm::StringRef str) {
    std::lock_guard<std::mutex> lock(cacheMutex);

    if (!cacheValid || nameCache.empty()) {
        return false;
    }

    for (const auto &entry : nameCache) {
        llvm::StringRef key = entry.getKey();
        if (str.find(key) != llvm::StringRef::npos) {
            return true;
        }
    }
    return false;
}

llvm::StringMap<llvm::GlobalValue *> GlobalValueNameMatcher::getMatchingGlobalValues(llvm::StringRef str) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    llvm::StringMap<llvm::GlobalValue *> results;

    if (!cacheValid || nameCache.empty()) {
        return results;
    }

    for (const auto &entry : nameCache) {
        llvm::StringRef key = entry.getKey();
        llvm::GlobalValue *value = entry.getValue();
        if (str.find(key) != llvm::StringRef::npos) {
            results[key] = value;
        }
    }
    return results;
}

bool BCCommon::matchesPattern(llvm::StringRef filename, llvm::StringRef pattern) {
    if (pattern.empty() || filename.empty()) {
        return false;
    }

    // 确保文件名足够长以包含 ".bc"
    if (filename.size() < 3) {
        return false;
    }

    // 使用 endswith 检查后缀
    return (filename.find(pattern) != llvm::StringRef::npos) && filename.ends_with(".bc");
}

bool BCCommon::copyByPattern(llvm::StringRef pattern) {
    bool anyCopied = false;

    try {
        // 遍历源目录
        for (const auto &entry : std::filesystem::directory_iterator(config.workSpace + "output")) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();

                // 检查文件名是否匹配模式
                if (matchesPattern(filename, pattern)) {
                    std::filesystem::path destFile = std::filesystem::path(config.bcWorkDir) / filename;

                    std::filesystem::copy_file(entry.path(), destFile,
                                               std::filesystem::copy_options::overwrite_existing);

                    anyCopied = true;
                }
            }
        }
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "遍历目录失败: " << e.what() << std::endl;
        return false;
    }

    if (!anyCopied) {
        std::cout << "警告: 没有找到匹配的文件" << std::endl;
    }

    return true;
}