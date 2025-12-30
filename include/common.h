#ifndef BC_SPLITTER_COMMON_H
#define BC_SPLITTER_COMMON_H

#include <memory>
#include <unordered_map>
#include <set>
#include <string>
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
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SetVector.h"
#include "core.h"
#include "logging.h"

// 配置结构体
struct Config {
    const std::string workDir = "/Users/wangzirui/Desktop/libkn_so/reproduce_kn_shared_20251119_094034/";
    const std::string relativeDir = "private/var/folders/w7/w26y4gqn3t1f76kvj8r531dr0000gn/T/konan_temp6482467269771911962/";
    const std::string bcWorkDir = workDir + relativeDir;
    const std::string responseFile = "/Users/wangzirui/Desktop/libkn_so/reproduce_kn_shared_20251119_094034/response.txt";
    const std::string workSpace = "/Users/wangzirui/Desktop/libkn_so/test/workspace/";
};

// 组信息结构体
struct GroupInfo {
    int groupId;
    std::string bcFile;
    bool hasKonanCxaDemangle = false;
    llvm::DenseSet<int> dependencies;

    GroupInfo(int id, std::string bc, bool special) : groupId(id), bcFile(bc),
                                                      hasKonanCxaDemangle(special), dependencies() {}
    void printDetails() const;
};

class FunctionNameMatcher {
private:
    // 缓存函数名列表
    llvm::StringMap<llvm::Function*> nameCache;
    std::mutex cacheMutex;
    bool cacheValid = false;

public:
    FunctionNameMatcher() = default;
    ~FunctionNameMatcher() = default;

    // 重建缓存
    void rebuildCache(const llvm::DenseMap<llvm::Function*, FunctionInfo>& functionMap);

    // 使缓存失效
    void invalidateCache();

    // 检查缓存是否有效
    bool isCacheValid() const { return cacheValid; }

    // 获取缓存大小
    size_t getCacheSize() const { return nameCache.size(); }

    // 检查字符串是否包含任何函数名
    bool containsFunctionName(llvm::StringRef str);

    // 获取匹配的所有函数信息
    llvm::StringMap<llvm::Function*> getMatchingFunctions(llvm::StringRef str);

};

class BCCommon {
private:
    std::unique_ptr<llvm::Module> module;
    llvm::DenseMap<llvm::GlobalVariable*, GlobalVariableInfo> globalVariableMap;
    llvm::DenseMap<llvm::Function*, FunctionInfo> functionMap;
    llvm::SmallVector<GroupInfo*, 32> fileMap;
    llvm::LLVMContext* context;
    Config config;
    // 存储循环调用组
    llvm::SmallVector<llvm::DenseSet<llvm::Function*>, 32> cyclicGroups;
    // 函数到所属循环组的映射（一个函数可能属于多个组）
    llvm::DenseMap<llvm::Function*, llvm::SmallVector<int, 32>> functionToGroupMap;
    Logger logger;
    FunctionNameMatcher functionNameMatcher;

public:
    BCCommon();
    ~BCCommon();

    // 获取器
    llvm::Module* getModule() const { return module.get(); }
    llvm::SmallVector<GroupInfo*, 32>& getFileMap() { return fileMap; }
    const llvm::SmallVector<GroupInfo*, 32>& getFileMap() const { return fileMap; }
    llvm::DenseMap<llvm::Function*, FunctionInfo>& getFunctionMap() { return functionMap; }
    const llvm::DenseMap<llvm::Function*, FunctionInfo>& getFunctionMap() const { return functionMap; }
    llvm::DenseMap<llvm::GlobalVariable*, GlobalVariableInfo>& getGlobalVariableMap() { return globalVariableMap; }
    const llvm::DenseMap<llvm::GlobalVariable*, GlobalVariableInfo>& getGlobalVariableMap() const { return globalVariableMap; }
    llvm::LLVMContext* getContext() const { return context; }

    // 设置器
    void setModule(std::unique_ptr<llvm::Module> M) { module = std::move(M); }
    void setContext(llvm::LLVMContext* newContext) { context = newContext; }

    // 辅助方法
    bool hasModule() const { return module != nullptr; }
    size_t getFunctionCount() const { return functionMap.size(); }
    bool writeBitcodeSafely(llvm::Module& M, llvm::StringRef filename);
    std::string renameUnnamedGlobals(llvm::StringRef filename);
    static bool matchesPattern(llvm::StringRef filename, llvm::StringRef pattern);
    bool copyByPattern(llvm::StringRef pattern);
    static bool isNumberString(llvm::StringRef str);

    // 清空数据
    void clear();
    void findCyclicGroups();
    llvm::DenseSet<llvm::Function*> getCyclicGroupsContainingFunction(llvm::Function* F);
    llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> getGroupDependencies();

    // 函数名匹配相关方法
    bool containsFunctionNameInString(llvm::StringRef str);
    llvm::StringSet<> getMatchingFunctionNames(llvm::StringRef str);
    llvm::DenseSet<llvm::Function*> getMatchingFunctions(llvm::StringRef str);
    llvm::Function* getFirstMatchingFunction(llvm::StringRef str);

    // 缓存管理
    void rebuildFunctionNameCache();
    void invalidateFunctionNameCache();
    bool isFunctionNameCacheValid() const;
    size_t getFunctionNameCacheSize() const;
    void analyzeCallRelations();
    llvm::Function* findFunctionFromUser(llvm::User* U);
    llvm::Function* findFunctionForConstant(llvm::Constant* C);
    llvm::Function* findFunctionForConstantImpl(llvm::Constant* C,
                                                      llvm::DenseSet<llvm::Constant*>& visited);
    llvm::Function* findFunctionFromUserImpl(llvm::User* U,
                                                   llvm::DenseSet<llvm::User*>& visited);

private:
    // 确保缓存有效的内部方法
    void ensureCacheValid();
};

#endif // BC_SPLITTER_COMMON_H