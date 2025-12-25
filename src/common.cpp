#include "common.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Transforms/Utils/Cloning.h"  // 包含 CloneModule 和 ValueToValueMapTy
#include "llvm/IR/ValueMap.h"              // ValueToValueMapTy 的详细定义
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/MemoryBuffer.h>
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Verifier.h"
#include <stack>
#include <filesystem>


// 打印单个GroupInfo的详细信息
void GroupInfo::printDetails() const {
    std::cout << "========== GroupInfo Details ==========" << std::endl;
    std::cout << "Group ID: " << groupId << std::endl;
    std::cout << "BC File: " << bcFile << std::endl;
    std::cout << "Has Konan Cxa Demangle: "
              << (hasKonanCxaDemangle ? "true" : "false") << std::endl;

    // 打印依赖项
    std::cout << "Dependencies (" << dependencies.size() << "): ";
    if (dependencies.empty()) {
        std::cout << "None";
    } else {
        bool first = true;
        for (int dep : dependencies) {
            if (!first) std::cout << ", ";
            std::cout << dep;
            first = false;
        }
    }
    std::cout << std::endl;
    std::cout << "======================================" << std::endl;
}

BCCommon::BCCommon() : context(nullptr) {}

BCCommon::~BCCommon() {
    clear();
}

void BCCommon::clear() {
    module.reset();
    functionMap.clear();
    globalVariableMap.clear();
    cyclicGroups.clear();
    functionToGroupMap.clear();
    context = nullptr;
    functionNameMatcher.invalidateCache(); // 清理缓存
}

bool BCCommon::isNumberString(const std::string& str) {
    if (str.empty()) return false;
    for (char c : str) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

std::string BCCommon::renameUnnamedGlobals(const std::string& filename) {

    llvm::ValueToValueMapTy VMap;
    llvm::SMDiagnostic err;
    std::string newfilename = "renamed_" + filename;
    // 创建新的上下文
    llvm::LLVMContext* context = new llvm::LLVMContext();

    auto mod = llvm::parseIRFile(filename, err, *context);
    // 克隆模块
    std::unique_ptr<llvm::Module> newModule = llvm::CloneModule(*mod, VMap);

    // 计数器用于生成唯一名称
    unsigned globalVarCounter = 0;
    unsigned funcCounter = 0;
    unsigned globalAliasCounter = 0;

    // 第一步：重命名全局变量
    for (auto& globalVar : newModule->globals()) {
        std::string oldName = globalVar.getName().str();

        // 检查是否未命名或名称以数字开头（通常是未命名的情况）
        if (oldName.empty() ||
            (oldName[0] >= '0' && oldName[0] <= '9')) {

            // 跳过以"llvm."开头的特殊全局变量
            if (oldName.substr(0, 5) == "llvm.") {
                continue;
            }

            // 生成新名称
            std::string newName = "renamed_global_var_" + std::to_string(globalVarCounter++);

            // 确保名称唯一
            while (newModule->getNamedValue(newName)) {
                newName = "renamed_global_var_" + std::to_string(globalVarCounter++);
            }

            // 直接重命名
            globalVar.setName(newName);
            //std::cout << "Renamed global variable: " << oldName << " -> " << newName << std::endl;
        }
    }

    // 第二步：重命名函数
    for (auto& func : newModule->functions()) {
        std::string oldName = func.getName().str();

        // 检查是否未命名或名称以数字开头
        if (oldName.empty() ||
            (oldName[0] >= '0' && oldName[0] <= '9')) {

            // 跳过LLVM内部函数
            if (oldName.substr(0, 5) == "llvm.") {
                continue;
            }

            // 生成新名称
            std::string newName = "renamed_func_" + std::to_string(funcCounter++);

            // 确保名称唯一
            while (newModule->getNamedValue(newName)) {
                newName = "renamed_func_" + std::to_string(funcCounter++);
            }

            // 直接重命名
            func.setName(newName);
            //std::cout << "Renamed function: " << oldName << " -> " << newName << std::endl;
        }
    }

    // 第三步：重命名全局别名
    for (auto& alias : newModule->aliases()) {
        std::string oldName = alias.getName().str();

        if (oldName.empty() ||
            (oldName[0] >= '0' && oldName[0] <= '9')) {

            // 生成新名称
            std::string newName = "renamed_alias_" + std::to_string(globalAliasCounter++);

            // 确保名称唯一
            while (newModule->getNamedValue(newName)) {
                newName = "renamed_alias_" + std::to_string(globalAliasCounter++);
            }

            // 直接重命名
            alias.setName(newName);
            //std::cout << "Renamed alias: " << oldName << " -> " << newName << std::endl;
        }
    }
    writeBitcodeSafely(*newModule, newfilename);
    return newfilename;
}

// 安全的bitcode写入方法
bool BCCommon::writeBitcodeSafely(llvm::Module& mod, const std::string& filename) {
    logger.logToFile("安全写入bitcode: " + filename);

    std::error_code ec;
    llvm::raw_fd_ostream outFile(config.workSpace + "output/" + filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        logger.logError("无法创建文件: " + filename + " - " + ec.message());
        return false;
    }

    try {
        llvm::WriteBitcodeToFile(mod, outFile);
        outFile.close();
        logger.log("成功写入: " + filename);
        return true;
    } catch (const std::exception& e) {
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
    std::unordered_map<llvm::Function*, std::unordered_set<llvm::Function*>> callGraph;
    std::unordered_map<llvm::Function*, int> indices;
    std::unordered_map<llvm::Function*, int> lowlinks;
    std::unordered_map<llvm::Function*, bool> onStack;
    std::stack<llvm::Function*> stack;
    int index = 0;

    // 初始化调用图
    for (auto& pair : functionMap) {
        llvm::Function* func = pair.first;
        callGraph[func] = std::unordered_set<llvm::Function*>();

        // 添加直接调用关系
        for (auto calledFunc : pair.second.calledFunctions) {
            if (functionMap.find(calledFunc) != functionMap.end()) {
                callGraph[func].insert(calledFunc);
            }
        }
    }

    // Tarjan算法实现
    std::function<void(llvm::Function*)> strongConnect;
    strongConnect = [&](llvm::Function* v) {
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
            std::unordered_set<llvm::Function*> scc;
            llvm::Function* w;
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
                for (auto func : scc) {
                    functionToGroupMap[func].push_back(groupIndex);
                }

                //logger.logToFile("Found cyclic group " + std::to_string(groupIndex) +
                //           " with " + std::to_string(scc.size()) + " functions");
            }
        }
    };

    // 对每个未访问的函数执行算法
    for (auto& pair : functionMap) {
        llvm::Function* func = pair.first;
        if (indices.find(func) == indices.end()) {
            strongConnect(func);
        }
    }

    logger.logToFile("找到的循环群总数: " + std::to_string(cyclicGroups.size()));
}

/**
 * @brief 根据llvm::Function*查询函数所在的循环调用组
 * @param func 要查询的函数指针
 * @return 包含该函数的所有循环调用组（每个组是一个函数集合）
 */

std::unordered_set<llvm::Function*> BCCommon::getCyclicGroupsContainingFunction(llvm::Function* func) {
    std::vector<std::unordered_set<llvm::Function*>> result;
    std::unordered_set<llvm::Function*> allRelatedFuncs;

    if (!func) {
        logger.logWarning("查询时提供的函数指针为空。");
        return allRelatedFuncs;
    }

    auto it = functionToGroupMap.find(func);
    if (it == functionToGroupMap.end()) {
        //logger.logToFile("Function " + func->getName().str() + " is not part of any cyclic group.");
        return allRelatedFuncs;
    }

    for (int groupIndex : it->second) {
        if (groupIndex >= 0 && groupIndex < cyclicGroups.size()) {
            result.push_back(cyclicGroups[groupIndex]);
        }
    }

    // 收集所有循环组中的所有函数
    for (const auto& cycGroup : result) {
        allRelatedFuncs.insert(cycGroup.begin(), cycGroup.end());
    }

    return allRelatedFuncs;
}

/**
 * @brief 每个group获取函数calledFunctions中所有函数的groupIndex（去重）
 *
 * @return std::vector<std::set<int>> 每个group去重后的groupIndex矩阵信息
 *         如果传入的函数不在functionMap中，返回空矩阵
 *         如果calledFunctions中的函数不在functionMap中，跳过该函数
 */
std::vector<std::set<int>> BCCommon::getGroupDependencies() {
    auto& functionMap = getFunctionMap();

    // 1. 先找出最大的groupIndex，以便确定vector大小
    int maxGroupIndex = -1;
    for (const auto& pair : functionMap) {
        maxGroupIndex = std::max(maxGroupIndex, pair.second.groupIndex);
    }

    // 如果没有任何分组，返回空vector
    if (maxGroupIndex < 0) {
        return {};
    }

    // 2. 创建结果vector，大小为maxGroupIndex+1
    std::vector<std::set<int>> groupDependencies(maxGroupIndex + 1);

    // 3. 遍历所有函数，收集每个函数所属组的依赖
    for (const auto& pair : functionMap) {
        const FunctionInfo& funcInfo = pair.second;

        // 跳过未分组的函数
        if (funcInfo.groupIndex < 0) {
            continue;
        }

        // 获取该函数调用的函数所在的组
        std::set<int> calledGroups;
        for (llvm::Function* calledFunc : funcInfo.calledFunctions) {
            auto calledIt = functionMap.find(calledFunc);
            if (calledIt != functionMap.end()) {
                int groupIdx = calledIt->second.groupIndex;
                if (groupIdx >= 0 && groupIdx != funcInfo.groupIndex) {
                    calledGroups.insert(groupIdx);
                }
            }
        }
        // 将依赖组添加到结果中
        groupDependencies[funcInfo.groupIndex].insert(
            calledGroups.begin(), calledGroups.end());
    }

    return groupDependencies;
}

// 当functionMap被外部修改时调用此方法
void BCCommon::invalidateFunctionNameCache() {
    functionNameMatcher.invalidateCache();
}

// 重建函数名缓存
void BCCommon::rebuildFunctionNameCache() {
    if (!functionMap.empty()) {
        functionNameMatcher.rebuildCache(functionMap);
        logger.log("记录：已缓存" +
                  std::to_string(functionNameMatcher.getCacheSize()) + "个名字");
    }
}

// 确保缓存有效
void BCCommon::ensureCacheValid() {
    if (!functionNameMatcher.isCacheValid() && !functionMap.empty()) {
        rebuildFunctionNameCache();
    }
}

// 检查字符串是否包含任何函数名
bool BCCommon::containsFunctionNameInString(const std::string& str) {
    ensureCacheValid();
    return functionNameMatcher.containsFunctionName(str);
}

// 获取匹配的函数名列表
std::vector<std::string> BCCommon::getMatchingFunctionNames(const std::string& str) {
    ensureCacheValid();
    auto matches = functionNameMatcher.getMatchingFunctions(str);

    std::vector<std::string> result;
    for (const auto& match : matches) {
        result.push_back(match.functionName);
    }
    return result;
}

// 获取匹配的函数指针列表
std::vector<llvm::Function*> BCCommon::getMatchingFunctions(const std::string& str) {
    ensureCacheValid();
    auto matches = functionNameMatcher.getMatchingFunctions(str);

    std::vector<llvm::Function*> result;
    for (const auto& match : matches) {
        result.push_back(match.functionPtr);
    }
    return result;
}

// 获取首个匹配的函数指针
llvm::Function* BCCommon::getFirstMatchingFunction(const std::string& str) {
    ensureCacheValid();
    auto matches = functionNameMatcher.getMatchingFunctions(str);

    llvm::Function* result;
    for (const auto& match : matches) {
        result = match.functionPtr;
    }
    return result;
}

// 获取缓存状态
bool BCCommon::isFunctionNameCacheValid() const {
    return functionNameMatcher.isCacheValid();
}

size_t BCCommon::getFunctionNameCacheSize() const {
    return functionNameMatcher.getCacheSize();
}

void BCCommon::analyzeCallRelations() {
    // 首先遍历所有函数
    for (llvm::Function& func : *module) {
        if (func.isDeclaration()) continue;

        // 获取personality函数
        if (func.hasPersonalityFn()) {
            if (auto* personality = func.getPersonalityFn()) {
                if (auto* personalityFunc = llvm::dyn_cast<llvm::Function>(personality)) {
                    if (functionMap.count(&func) && functionMap.count(personalityFunc)) {
                        // 记录personality函数调用关系
                        functionMap[&func].personalityCalledFunctions.insert(personalityFunc);
                        functionMap[personalityFunc].personalityCallerFunctions.insert(&func);
                    }
                }
            }
        }

        if (!functionMap.count(&func)) {
            continue;
        }

        FunctionInfo& funcInfo = functionMap[&func];

        // 1. 遍历该函数的使用者，收集调用者信息（入度）
        // 使用一个临时集合来避免在遍历过程中修改users
        std::vector<llvm::User*> users;
        users.reserve(func.getNumUses());
        for (llvm::Use& use : func.uses()) {
            if (llvm::User* user = use.getUser()) {
                users.push_back(user);
            }
        }

        for (llvm::User* user : users) {
            if (!user) continue;

            // 尝试从user中找到其所在的函数
            llvm::Function* callerFunc = nullptr;

            // 首先检查user本身是否是函数
            if (llvm::Function* caller = llvm::dyn_cast<llvm::Function>(user)) {
                callerFunc = caller;
            }
            else if (llvm::Instruction* inst = llvm::dyn_cast<llvm::Instruction>(user)) {
                // 如果user是指令，获取指令所在的函数
                callerFunc = inst->getFunction();
            }
            else if (llvm::BasicBlock* bb = llvm::dyn_cast<llvm::BasicBlock>(user)) {
                // 如果user是基本块，获取基本块所在的函数
                callerFunc = bb->getParent();
            }
            else if (llvm::Constant* constant = llvm::dyn_cast<llvm::Constant>(user)) {
                // 对于常量使用，需要向上查找其所在的函数
                callerFunc = findFunctionForConstant(constant);
            }
            // 对于其他类型的user，我们尝试获取其所在的上下文
            else {
                callerFunc = findFunctionFromUser(user);
            }

            // 如果找到了调用者函数，并且它在我们追踪的范围内
            if (callerFunc && functionMap.count(callerFunc)) {
                // 添加到调用者集合
                funcInfo.callerFunctions.insert(callerFunc);

                // 同时更新调用者的出度信息
                FunctionInfo& callerInfo = functionMap[callerFunc];
                callerInfo.calledFunctions.insert(&func);
            }
        }

        // 2. 遍历函数体，收集被调用函数信息（出度）
        for (llvm::BasicBlock& bb : func) {
            for (llvm::Instruction& inst : bb) {
                // 处理调用指令
                if (llvm::CallInst* callInst = llvm::dyn_cast<llvm::CallInst>(&inst)) {
                    llvm::Value* calledValue = callInst->getCalledOperand();
                    if (!calledValue) continue;

                    // 剥离指针转换
                    calledValue = calledValue->stripPointerCasts();

                    // 检查是否是函数
                    if (llvm::Function* calledFunc = llvm::dyn_cast<llvm::Function>(calledValue)) {
                        if (functionMap.count(calledFunc)) {
                            funcInfo.calledFunctions.insert(calledFunc);

                            // 同时更新被调用者的入度信息
                            FunctionInfo& calledInfo = functionMap[calledFunc];
                            calledInfo.callerFunctions.insert(&func);
                        }
                    }
                }
                // 处理调用指令的变体（如InvokeInst）
                else if (llvm::InvokeInst* invokeInst = llvm::dyn_cast<llvm::InvokeInst>(&inst)) {
                    llvm::Value* calledValue = invokeInst->getCalledOperand();
                    if (!calledValue) continue;

                    // 剥离指针转换
                    calledValue = calledValue->stripPointerCasts();

                    if (llvm::Function* calledFunc = llvm::dyn_cast<llvm::Function>(calledValue)) {
                        if (functionMap.count(calledFunc)) {
                            funcInfo.calledFunctions.insert(calledFunc);

                            FunctionInfo& calledInfo = functionMap[calledFunc];
                            calledInfo.callerFunctions.insert(&func);
                        }
                    }
                }
                // 处理函数地址的存储操作
                else if (llvm::StoreInst* storeInst = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                    llvm::Value* value = storeInst->getValueOperand();
                    if (!value) continue;

                    value = value->stripPointerCasts();
                    if (llvm::Function* storedFunc = llvm::dyn_cast<llvm::Function>(value)) {
                        if (functionMap.count(storedFunc)) {
                            funcInfo.calledFunctions.insert(storedFunc);

                            FunctionInfo& storedInfo = functionMap[storedFunc];
                            storedInfo.callerFunctions.insert(&func);
                        }
                    }
                }
                // 处理函数地址作为参数传递
                else {
                    // 遍历指令的所有操作数
                    for (unsigned i = 0; i < inst.getNumOperands(); i++) {
                        llvm::Value* operand = inst.getOperand(i);
                        if (!operand) continue;

                        operand = operand->stripPointerCasts();
                        if (llvm::Function* funcOperand = llvm::dyn_cast<llvm::Function>(operand)) {
                            if (functionMap.count(funcOperand)) {
                                funcInfo.calledFunctions.insert(funcOperand);

                                FunctionInfo& operandInfo = functionMap[funcOperand];
                                operandInfo.callerFunctions.insert(&func);
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. 遍历所有函数，统计入度和出度
    for (auto& pair : functionMap) {
        FunctionInfo& funcInfo = pair.second;
        funcInfo.inDegree = funcInfo.callerFunctions.size();
        funcInfo.outDegree = funcInfo.calledFunctions.size();
    }
}

// 辅助函数：从常量中查找其所在的函数
llvm::Function* BCCommon::findFunctionForConstant(llvm::Constant* constant) {
    if (!constant) return nullptr;

    // 使用集合记录已访问的常量，避免无限递归
    std::unordered_set<llvm::Constant*> visited;
    return findFunctionForConstantImpl(constant, visited);
}

llvm::Function* BCCommon::findFunctionForConstantImpl(llvm::Constant* constant,
                                                      std::unordered_set<llvm::Constant*>& visited) {
    if (!constant || !visited.insert(constant).second) {
        return nullptr;
    }

    // 遍历常量的所有使用者
    for (llvm::User* user : constant->users()) {
        if (!user) continue;

        if (llvm::Instruction* inst = llvm::dyn_cast<llvm::Instruction>(user)) {
            if (llvm::Function* func = inst->getFunction()) {
                return func;
            }
        }
        else if (llvm::Constant* constUser = llvm::dyn_cast<llvm::Constant>(user)) {
            // 递归查找
            if (llvm::Function* func = findFunctionForConstantImpl(constUser, visited)) {
                return func;
            }
        }
        else if (llvm::GlobalVariable* globalVar = llvm::dyn_cast<llvm::GlobalVariable>(user)) {
            // 对于全局变量，我们可能需要查看哪些函数使用这个全局变量
            // 这里简单返回nullptr，或者可以进一步处理
            continue;
        }
    }
    return nullptr;
}

// 辅助函数：从任意User中查找其所在的函数
llvm::Function* BCCommon::findFunctionFromUser(llvm::User* user) {
    if (!user) return nullptr;

    std::unordered_set<llvm::User*> visited;
    return findFunctionFromUserImpl(user, visited);
}

llvm::Function* BCCommon::findFunctionFromUserImpl(llvm::User* user,
                                                   std::unordered_set<llvm::User*>& visited) {
    if (!user || !visited.insert(user).second) {
        return nullptr;
    }

    // 尝试获取user的父节点
    if (llvm::Instruction* inst = llvm::dyn_cast<llvm::Instruction>(user)) {
        return inst->getFunction();
    }
    else if (llvm::BasicBlock* bb = llvm::dyn_cast<llvm::BasicBlock>(user)) {
        return bb->getParent();
    }
    else if (llvm::Function* func = llvm::dyn_cast<llvm::Function>(user)) {
        return func;
    }

    // 对于其他类型的User，尝试查找其使用者链
    for (llvm::User* userOfUser : user->users()) {
        if (llvm::Function* func = findFunctionFromUserImpl(userOfUser, visited)) {
            return func;
        }
    }

    return nullptr;
}

void FunctionNameMatcher::rebuildCache(const std::unordered_map<llvm::Function*, FunctionInfo>& functionMap) {
    std::lock_guard<std::mutex> lock(cacheMutex);

    nameCache.clear();
    for (const auto& [func, info] : functionMap) {
        if (!info.displayName.empty()) {
            nameCache.push_back({info.displayName, func});
        }
    }
    if (functionMap.size() > 100) cacheValid = true;
}

void FunctionNameMatcher::invalidateCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cacheValid = false;
}

bool FunctionNameMatcher::containsFunctionName(const std::string& str) {
    std::lock_guard<std::mutex> lock(cacheMutex);

    if (!cacheValid || nameCache.empty()) {
        return false;
    }

    for (const auto& entry : nameCache) {
        if (str.find(entry.displayName) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<FunctionNameMatcher::MatchResult> FunctionNameMatcher::getMatchingFunctions(const std::string& str) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    std::vector<MatchResult> results;

    if (!cacheValid || nameCache.empty()) {
        return results;
    }

    for (const auto& entry : nameCache) {
        if (str.find(entry.displayName) != std::string::npos) {
            results.push_back({entry.displayName, entry.functionPtr});
        }
    }
    return results;
}

bool BCCommon::matchesPattern(const std::string& filename, const std::string& pattern) {
    // 检查是否以特定前缀开头并以.bc结尾
    if (pattern.empty()) {
        return false;
    }
    return (filename.find(pattern) != std::string::npos) &&
           (filename.substr(filename.length() - 3) == ".bc");
}

bool BCCommon::copyByPattern(const std::string& pattern) {
    bool anyCopied = false;

    try {
        // 遍历源目录
        for (const auto& entry : std::filesystem::directory_iterator(config.workSpace + "output")) {
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
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "遍历目录失败: " << e.what() << std::endl;
        return false;
    }

    if (!anyCopied) {
        std::cout << "警告: 没有找到匹配的文件" << std::endl;
    }

    return true;
}