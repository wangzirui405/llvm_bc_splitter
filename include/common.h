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

class BCCommon {
private:
    std::unique_ptr<llvm::Module> module;
    std::unordered_map<llvm::Function*, FunctionInfo> functionMap;
    llvm::LLVMContext* context;
    // 存储循环调用组
    std::vector<std::unordered_set<llvm::Function*>> cyclicGroups;
    // 函数到所属循环组的映射（一个函数可能属于多个组）
    std::unordered_map<llvm::Function*, std::vector<int>> functionToGroupMap;
    Logger logger;

public:
    BCCommon();
    ~BCCommon();

    // 获取器
    llvm::Module* getModule() const { return module.get(); }
    std::unordered_map<llvm::Function*, FunctionInfo>& getFunctionMap() { return functionMap; }
    const std::unordered_map<llvm::Function*, FunctionInfo>& getFunctionMap() const { return functionMap; }
    llvm::LLVMContext* getContext() const { return context; }

    // 设置器
    void setModule(std::unique_ptr<llvm::Module> newModule) { module = std::move(newModule); }
    void setContext(llvm::LLVMContext* newContext) { context = newContext; }

    // 辅助方法
    bool hasModule() const { return module != nullptr; }
    size_t getFunctionCount() const { return functionMap.size(); }
    bool writeBitcodeSafely(llvm::Module& mod, const std::string& filename);
    // 清空数据
    void clear();
    void findCyclicGroups();
    std::unordered_set<llvm::Function*> getCyclicGroupsContainingFunction(llvm::Function* func);
    std::vector<std::set<int>> getGroupDependencies();
};

#endif // BC_SPLITTER_COMMON_H