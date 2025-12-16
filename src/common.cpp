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

BCCommon::BCCommon() : context(nullptr) {}

BCCommon::~BCCommon() {
    clear();
}

void BCCommon::clear() {
    module.reset();
    functionMap.clear();
    context = nullptr;
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
    llvm::raw_fd_ostream outFile(filename, ec, llvm::sys::fs::OF_None);
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