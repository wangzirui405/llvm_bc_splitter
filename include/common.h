#ifndef BC_SPLITTER_COMMON_H
#define BC_SPLITTER_COMMON_H

#include <memory>
#include <unordered_map>
#include <set>
#include <string>
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
#include "core.h"
#include "logging.h"

// 配置结构体
struct Config {
    const std::string workDir = "/Users/wangzirui/Desktop/libkn_so/reproduce_kn_shared_20251119_094034/";
    const std::string relativeDir = "private/var/folders/w7/w26y4gqn3t1f76kvj8r531dr0000gn/T/konan_temp6482467269771911962/";
    const std::string bcWorkDir = workDir + relativeDir;
    const std::string responseFile = "/Users/wangzirui/Desktop/libkn_so/reproduce_kn_shared_20251119_094034/response_test_undefined.txt";
    const std::string workSpace = "/Users/wangzirui/Desktop/libkn_so/test/workspace/";
};

// 组信息结构体
struct GroupInfo {
    int groupId;
    std::string bcFile;
    bool hasKonanCxaDemangle = false;
    std::set<int> dependencies;

    GroupInfo(int id, std::string bc, bool special) : groupId(id), bcFile(bc),
                                                      hasKonanCxaDemangle(special), dependencies() {}
    void printDetails() const;
};

class FunctionNameMatcher {
private:
    struct CacheEntry {
        std::string displayName;
        llvm::Function* functionPtr;
    };

    // 缓存函数名列表
    std::vector<CacheEntry> nameCache;
    std::mutex cacheMutex;
    bool cacheValid = false;

public:
    FunctionNameMatcher() = default;
    ~FunctionNameMatcher() = default;

    // 重建缓存
    void rebuildCache(const std::unordered_map<llvm::Function*, FunctionInfo>& functionMap);

    // 使缓存失效
    void invalidateCache();

    // 检查缓存是否有效
    bool isCacheValid() const { return cacheValid; }

    // 获取缓存大小
    size_t getCacheSize() const { return nameCache.size(); }

    // 检查字符串是否包含任何函数名
    bool containsFunctionName(const std::string& str);

    // 获取匹配的所有函数信息
    struct MatchResult {
        std::string functionName;
        llvm::Function* functionPtr;
    };

    std::vector<MatchResult> getMatchingFunctions(const std::string& str);

};

class BCCommon {
private:
    std::unique_ptr<llvm::Module> module;
    std::unordered_map<llvm::GlobalVariable*, GlobalVariableInfo> globalVariableMap;
    std::unordered_map<llvm::Function*, FunctionInfo> functionMap;
    std::vector<GroupInfo*> fileMap;
    llvm::LLVMContext* context;
    Config config;
    // 存储循环调用组
    std::vector<std::unordered_set<llvm::Function*>> cyclicGroups;
    // 函数到所属循环组的映射（一个函数可能属于多个组）
    std::unordered_map<llvm::Function*, std::vector<int>> functionToGroupMap;
    Logger logger;
    FunctionNameMatcher functionNameMatcher;

public:
    BCCommon();
    ~BCCommon();

    // 获取器
    llvm::Module* getModule() const { return module.get(); }
    std::vector<GroupInfo*>& getFileMap() { return fileMap; }
    const std::vector<GroupInfo*>& getFileMap() const { return fileMap; }
    std::unordered_map<llvm::Function*, FunctionInfo>& getFunctionMap() { return functionMap; }
    const std::unordered_map<llvm::Function*, FunctionInfo>& getFunctionMap() const { return functionMap; }
    std::unordered_map<llvm::GlobalVariable*, GlobalVariableInfo>& getGlobalVariableMap() { return globalVariableMap; }
    const std::unordered_map<llvm::GlobalVariable*, GlobalVariableInfo>& getGlobalVariableMap() const { return globalVariableMap; }
    llvm::LLVMContext* getContext() const { return context; }

    // 设置器
    void setModule(std::unique_ptr<llvm::Module> newModule) { module = std::move(newModule); }
    void setContext(llvm::LLVMContext* newContext) { context = newContext; }

    // 辅助方法
    bool hasModule() const { return module != nullptr; }
    size_t getFunctionCount() const { return functionMap.size(); }
    bool writeBitcodeSafely(llvm::Module& mod, const std::string& filename);
    std::string renameUnnamedGlobals(const std::string& filename);
    static bool matchesPattern(const std::string& filename, const std::string& pattern);
    bool copyByPattern(const std::string& pattern);
    static bool isNumberString(const std::string& str);

    // 清空数据
    void clear();
    void findCyclicGroups();
    std::unordered_set<llvm::Function*> getCyclicGroupsContainingFunction(llvm::Function* func);
    std::vector<std::set<int>> getGroupDependencies();

    // 函数名匹配相关方法
    bool containsFunctionNameInString(const std::string& str);
    std::vector<std::string> getMatchingFunctionNames(const std::string& str);
    std::vector<llvm::Function*> getMatchingFunctions(const std::string& str);
    llvm::Function* getFirstMatchingFunction(const std::string& str);

    // 缓存管理
    void rebuildFunctionNameCache();
    void invalidateFunctionNameCache();
    bool isFunctionNameCacheValid() const;
    size_t getFunctionNameCacheSize() const;
    void analyzeCallRelations();
    void processCallInstruction(
        llvm::CallInst* callInst,
        llvm::Function* callerFunc);
    void processBasicBlockCalls(
        llvm::BasicBlock* bb,
        llvm::Function* callerFunc,
        std::unordered_set<llvm::Function*>& calledSet,
        std::unordered_set<llvm::Function*>& callerSet);
    void processInvokeInstruction(
        llvm::InvokeInst* invokeInst,
        llvm::Function* callerFunc);

private:
    // 确保缓存有效的内部方法
    void ensureCacheValid();
};

#endif // BC_SPLITTER_COMMON_H