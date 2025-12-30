#include "common.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Function.h"
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
    functionMap.clear();
    globalVariableMap.clear();
    cyclicGroups.clear();
    functionToGroupMap.clear();
    context = nullptr;
    functionNameMatcher.invalidateCache(); // 清理缓存
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

std::string BCCommon::renameUnnamedGlobals(llvm::StringRef filename) {

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
    unsigned funcCounter = 0;
    unsigned globalAliasCounter = 0;

    // 第一步：重命名全局变量
    for (auto &GV : newM->globals()) {
        std::string oldName = GV.getName().str();

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
            GV.setName(newName);
            // std::cout << "Renamed global variable: " << oldName << " -> " << newName << std::endl;
        }
    }

    // 第二步：重命名函数
    for (auto &F : newM->functions()) {
        std::string oldName = F.getName().str();

        // 检查是否未命名或名称以数字开头
        if (oldName.empty() || (oldName[0] >= '0' && oldName[0] <= '9')) {

            // 跳过LLVM内部函数
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
    logger.logToFile("安全写入bitcode: " + filename.str());

    std::error_code ec;
    llvm::raw_fd_ostream outFile(config.workSpace + "output/" + filename.str(), ec, llvm::sys::fs::OF_None);
    if (ec) {
        logger.logError("无法创建文件: " + filename.str() + " - " + ec.message());
        return false;
    }

    try {
        llvm::WriteBitcodeToFile(M, outFile);
        outFile.close();
        logger.log("成功写入: " + filename.str());
        return true;
    } catch (const std::exception &e) {
        logger.logError("写入bitcode时发生异常: " + std::string(e.what()));
        llvm::sys::fs::remove(filename);
        return false;
    }
}

/**
 * @brief 检测并记录所有存在循环调用的函数组
 *
 * 使用Tarjan算法查找函数调用图中的强连通分量（SCC）
 * 强连通分量即存在循环调用的函数组
 */
void BCCommon::findCyclicGroups() {
    cyclicGroups.clear();
    functionToGroupMap.clear();

    if (functionMap.empty()) {
        logger.logWarning("Function map is empty, no cyclic groups to find.");
        return;
    }

    // 构建调用图
    llvm::DenseMap<llvm::Function *, llvm::DenseSet<llvm::Function *>> callGraph;
    llvm::DenseMap<llvm::Function *, int> indices;
    llvm::DenseMap<llvm::Function *, int> lowlinks;
    llvm::DenseMap<llvm::Function *, bool> onStack;
    std::stack<llvm::Function *> stack;
    int index = 0;

    // 初始化调用图
    for (auto &pair : functionMap) {
        llvm::Function *F = pair.first;
        callGraph[F] = llvm::DenseSet<llvm::Function *>();

        // 添加直接调用关系
        for (auto calledFunc : pair.second.calledFunctions) {
            if (functionMap.find(calledFunc) != functionMap.end()) {
                callGraph[F].insert(calledFunc);
            }
        }
    }

    // Tarjan算法实现
    std::function<void(llvm::Function *)> strongConnect;
    strongConnect = [&](llvm::Function *v) {
        indices[v] = index;
        lowlinks[v] = index;
        index++;
        stack.push(v);
        onStack[v] = true;

        // 遍历所有邻居（被调用的函数）
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
            llvm::DenseSet<llvm::Function *> scc;
            llvm::Function *w;
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

                // 更新函数到组的映射
                for (auto F : scc) {
                    functionToGroupMap[F].push_back(groupIndex);
                }

                // logger.logToFile("Found cyclic group " + std::to_string(groupIndex) +
                //            " with " + std::to_string(scc.size()) + " functions");
            }
        }
    };

    // 对每个未访问的函数执行算法
    for (auto &pair : functionMap) {
        llvm::Function *F = pair.first;
        if (indices.find(F) == indices.end()) {
            strongConnect(F);
        }
    }

    logger.logToFile("找到的循环群总数: " + std::to_string(cyclicGroups.size()));
}

/**
 * @brief 根据llvm::Function*查询函数所在的循环调用组
 * @param F 要查询的函数指针
 * @return 包含该函数的所有循环调用组（每个组是一个函数集合）
 */

llvm::DenseSet<llvm::Function *> BCCommon::getCyclicGroupsContainingFunction(llvm::Function *F) {
    if (!F) {
        logger.logWarning("查询时提供的函数指针为空。");
        return llvm::DenseSet<llvm::Function *>();
    }

    auto it = functionToGroupMap.find(F);
    if (it == functionToGroupMap.end()) {
        return llvm::DenseSet<llvm::Function *>();
    }

    llvm::DenseSet<llvm::Function *> allRelatedFuncs;

    // 收集所有相关函数
    for (int groupIndex : it->second) {
        if (groupIndex >= 0 && groupIndex < cyclicGroups.size()) {
            allRelatedFuncs.insert(cyclicGroups[groupIndex].begin(), cyclicGroups[groupIndex].end());
        }
    }

    return allRelatedFuncs;
}

/**
 * @brief 每个group获取函数calledFunctions中所有函数的groupIndex（去重）
 *
 * @return llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> 每个group去重后的groupIndex矩阵信息
 *         如果传入的函数不在functionMap中，返回空矩阵
 *         如果calledFunctions中的函数不在functionMap中，跳过该函数
 */
llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> BCCommon::getGroupDependencies() {
    auto &functionMap = getFunctionMap();

    int maxGroupIndex = -1;
    for (const auto &pair : functionMap) {
        maxGroupIndex = std::max(maxGroupIndex, pair.second.groupIndex);
    }

    if (maxGroupIndex < 0) {
        return {};
    }

    // 使用 SetVector 保持插入顺序并去重
    llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> groupDependencies;
    groupDependencies.resize(maxGroupIndex + 1);

    for (const auto &pair : functionMap) {
        const FunctionInfo &funcInfo = pair.second;

        if (funcInfo.groupIndex < 0) {
            continue;
        }

        for (llvm::Function *calledF : funcInfo.calledFunctions) {
            auto calledIt = functionMap.find(calledF);
            if (calledIt != functionMap.end()) {
                int groupIdx = calledIt->second.groupIndex;
                if (groupIdx >= 0 && groupIdx != funcInfo.groupIndex) {
                    groupDependencies[funcInfo.groupIndex].insert(groupIdx);
                }
            }
        }
    }

    return groupDependencies;
}

// 当functionMap被外部修改时调用此方法
void BCCommon::invalidateFunctionNameCache() { functionNameMatcher.invalidateCache(); }

// 重建函数名缓存
void BCCommon::rebuildFunctionNameCache() {
    if (!functionMap.empty()) {
        functionNameMatcher.rebuildCache(functionMap);
        logger.log("记录：已缓存" + std::to_string(functionNameMatcher.getCacheSize()) + "个名字");
    }
}

// 确保缓存有效
void BCCommon::ensureCacheValid() {
    if (!functionNameMatcher.isCacheValid() && !functionMap.empty()) {
        rebuildFunctionNameCache();
    }
}

// 检查字符串是否包含任何函数名
bool BCCommon::containsFunctionNameInString(llvm::StringRef str) {
    ensureCacheValid();
    return functionNameMatcher.containsFunctionName(str);
}

// 获取匹配的函数名列表
llvm::StringSet<> BCCommon::getMatchingFunctionNames(llvm::StringRef str) {
    ensureCacheValid();
    auto matches = functionNameMatcher.getMatchingFunctions(str);

    llvm::StringSet<> result;
    for (const auto &entry : matches) {
        llvm::StringRef key = entry.getKey();
        if (str.find(key) != llvm::StringRef::npos) {
            result.insert(key);
        }
    }
    return result;
}

// 获取匹配的函数指针列表
llvm::DenseSet<llvm::Function *> BCCommon::getMatchingFunctions(llvm::StringRef str) {
    ensureCacheValid();
    auto matches = functionNameMatcher.getMatchingFunctions(str);

    llvm::DenseSet<llvm::Function *> result;
    for (const auto &match : matches) {
        result.insert(match.second);
    }
    return result;
}

// 获取首个匹配的函数指针
llvm::Function *BCCommon::getFirstMatchingFunction(llvm::StringRef str) {
    ensureCacheValid();
    auto matches = functionNameMatcher.getMatchingFunctions(str);

    llvm::Function *resultF;
    for (const auto &match : matches) {
        resultF = match.second;
    }
    return resultF;
}

// 获取缓存状态
bool BCCommon::isFunctionNameCacheValid() const { return functionNameMatcher.isCacheValid(); }

size_t BCCommon::getFunctionNameCacheSize() const { return functionNameMatcher.getCacheSize(); }

void BCCommon::analyzeCallRelations() {
    // 首先遍历所有函数
    for (llvm::Function &F : *module) {
        if (F.isDeclaration())
            continue;

        // 获取personality函数
        if (F.hasPersonalityFn()) {
            if (auto *personality = F.getPersonalityFn()) {
                if (auto *personalityF = llvm::dyn_cast<llvm::Function>(personality)) {
                    if (functionMap.count(&F) && functionMap.count(personalityF)) {
                        // 记录personality函数调用关系
                        functionMap[&F].personalityCalledFunctions.insert(personalityF);
                        functionMap[personalityF].personalityCallerFunctions.insert(&F);
                    }
                }
            }
        }

        if (!functionMap.count(&F)) {
            continue;
        }

        FunctionInfo &funcInfo = functionMap[&F];

        // 1. 遍历该函数的使用者，收集调用者信息（入度）
        // 使用一个临时集合来避免在遍历过程中修改users
        llvm::SmallPtrSet<llvm::User *, 32> users;

        for (llvm::Use &use : F.uses()) {
            if (llvm::User *U = use.getUser()) {
                users.insert(U);
            }
        }

        for (llvm::User *U : users) {
            if (!U)
                continue;

            // 尝试从user中找到其所在的函数
            llvm::Function *callerF = nullptr;

            // 首先检查user本身是否是函数
            if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(U)) {
                callerF = F;
            } else if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(U)) {
                // 如果user是指令，获取指令所在的函数
                callerF = I->getFunction();
            } else if (llvm::BasicBlock *BB = llvm::dyn_cast<llvm::BasicBlock>(U)) {
                // 如果user是基本块，获取基本块所在的函数
                callerF = BB->getParent();
            } else if (llvm::Constant *C = llvm::dyn_cast<llvm::Constant>(U)) {
                // 对于常量使用，需要向上查找其所在的函数
                callerF = findFunctionForConstant(C);
            }
            // 对于其他类型的user，我们尝试获取其所在的上下文
            else {
                callerF = findFunctionFromUser(U);
            }

            // 如果找到了调用者函数，并且它在我们追踪的范围内
            if (callerF && functionMap.count(callerF)) {
                // 添加到调用者集合
                funcInfo.callerFunctions.insert(callerF);

                // 同时更新调用者的出度信息
                FunctionInfo &callerInfo = functionMap[callerF];
                callerInfo.calledFunctions.insert(&F);
            }
        }

        // 2. 遍历函数体，收集被调用函数信息（出度）
        for (llvm::BasicBlock &BB : F) {
            for (llvm::Instruction &I : BB) {
                // 处理调用指令
                if (llvm::CallInst *callInst = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    llvm::Value *calledValue = callInst->getCalledOperand();
                    if (!calledValue)
                        continue;

                    // 剥离指针转换
                    calledValue = calledValue->stripPointerCasts();

                    // 检查是否是函数
                    if (llvm::Function *calledF = llvm::dyn_cast<llvm::Function>(calledValue)) {
                        if (functionMap.count(calledF)) {
                            funcInfo.calledFunctions.insert(calledF);

                            // 同时更新被调用者的入度信息
                            FunctionInfo &calledInfo = functionMap[calledF];
                            calledInfo.callerFunctions.insert(&F);
                        }
                    }
                }
                // 处理调用指令的变体（如InvokeInst）
                else if (llvm::InvokeInst *invokeInst = llvm::dyn_cast<llvm::InvokeInst>(&I)) {
                    llvm::Value *calledValue = invokeInst->getCalledOperand();
                    if (!calledValue)
                        continue;

                    // 剥离指针转换
                    calledValue = calledValue->stripPointerCasts();

                    if (llvm::Function *calledF = llvm::dyn_cast<llvm::Function>(calledValue)) {
                        if (functionMap.count(calledF)) {
                            funcInfo.calledFunctions.insert(calledF);

                            FunctionInfo &calledInfo = functionMap[calledF];
                            calledInfo.callerFunctions.insert(&F);
                        }
                    }
                }
                // 处理函数地址的存储操作
                else if (llvm::StoreInst *storeInst = llvm::dyn_cast<llvm::StoreInst>(&I)) {
                    llvm::Value *value = storeInst->getValueOperand();
                    if (!value)
                        continue;

                    value = value->stripPointerCasts();
                    if (llvm::Function *storedF = llvm::dyn_cast<llvm::Function>(value)) {
                        if (functionMap.count(storedF)) {
                            funcInfo.calledFunctions.insert(storedF);

                            FunctionInfo &storedInfo = functionMap[storedF];
                            storedInfo.callerFunctions.insert(&F);
                        }
                    }
                }
                // 处理函数地址作为参数传递
                else {
                    // 遍历指令的所有操作数
                    for (unsigned i = 0; i < I.getNumOperands(); i++) {
                        llvm::Value *operand = I.getOperand(i);
                        if (!operand)
                            continue;

                        operand = operand->stripPointerCasts();
                        if (llvm::Function *operandF = llvm::dyn_cast<llvm::Function>(operand)) {
                            if (functionMap.count(operandF)) {
                                funcInfo.calledFunctions.insert(operandF);

                                FunctionInfo &operandInfo = functionMap[operandF];
                                operandInfo.callerFunctions.insert(&F);
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. 遍历所有函数，统计入度和出度
    for (auto &pair : functionMap) {
        FunctionInfo &funcInfo = pair.second;
        funcInfo.inDegree = funcInfo.callerFunctions.size();
        funcInfo.outDegree = funcInfo.calledFunctions.size();
    }
}

// 辅助函数：从常量中查找其所在的函数
llvm::Function *BCCommon::findFunctionForConstant(llvm::Constant *C) {
    if (!C)
        return nullptr;

    // 使用集合记录已访问的常量，避免无限递归
    llvm::DenseSet<llvm::Constant *> visited;
    return findFunctionForConstantImpl(C, visited);
}

llvm::Function *BCCommon::findFunctionForConstantImpl(llvm::Constant *C, llvm::DenseSet<llvm::Constant *> &visited) {
    if (!C || !visited.insert(C).second) {
        return nullptr;
    }

    // 遍历常量的所有使用者
    for (llvm::User *U : C->users()) {
        if (!U)
            continue;

        if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(U)) {
            if (llvm::Function *F = I->getFunction()) {
                return F;
            }
        } else if (llvm::Constant *constUser = llvm::dyn_cast<llvm::Constant>(U)) {
            // 递归查找
            if (llvm::Function *F = findFunctionForConstantImpl(constUser, visited)) {
                return F;
            }
        } else if (llvm::GlobalVariable *GV = llvm::dyn_cast<llvm::GlobalVariable>(U)) {
            // 对于全局变量，我们可能需要查看哪些函数使用这个全局变量
            // 这里简单返回nullptr，或者可以进一步处理
            continue;
        }
    }
    return nullptr;
}

// 辅助函数：从任意User中查找其所在的函数
llvm::Function *BCCommon::findFunctionFromUser(llvm::User *U) {
    if (!U)
        return nullptr;

    llvm::DenseSet<llvm::User *> visited;
    return findFunctionFromUserImpl(U, visited);
}

llvm::Function *BCCommon::findFunctionFromUserImpl(llvm::User *U, llvm::DenseSet<llvm::User *> &visited) {
    if (!U || !visited.insert(U).second) {
        return nullptr;
    }

    // 尝试获取user的父节点
    if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(U)) {
        return I->getFunction();
    } else if (llvm::BasicBlock *BB = llvm::dyn_cast<llvm::BasicBlock>(U)) {
        return BB->getParent();
    } else if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(U)) {
        return F;
    }

    // 对于其他类型的User，尝试查找其使用者链
    for (llvm::User *userOfUser : U->users()) {
        if (llvm::Function *F = findFunctionFromUserImpl(userOfUser, visited)) {
            return F;
        }
    }

    return nullptr;
}

void FunctionNameMatcher::rebuildCache(const llvm::DenseMap<llvm::Function *, FunctionInfo> &functionMap) {
    std::lock_guard<std::mutex> lock(cacheMutex);

    nameCache.clear();
    for (const auto &[F, info] : functionMap) {
        if (!info.displayName.empty()) {
            nameCache.insert({info.displayName, F});
        }
    }
    if (functionMap.size() > 100)
        cacheValid = true;
}

void FunctionNameMatcher::invalidateCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cacheValid = false;
}

bool FunctionNameMatcher::containsFunctionName(llvm::StringRef str) {
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

llvm::StringMap<llvm::Function *> FunctionNameMatcher::getMatchingFunctions(llvm::StringRef str) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    llvm::StringMap<llvm::Function *> results;

    if (!cacheValid || nameCache.empty()) {
        return results;
    }

    for (const auto &entry : nameCache) {
        llvm::StringRef key = entry.getKey();
        llvm::Function *value = entry.getValue();
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