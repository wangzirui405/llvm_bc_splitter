// splitter.h
#ifndef BC_SPLITTER_SPLITTER_H
#define BC_SPLITTER_SPLITTER_H

#include "core.h"
#include "logging.h"
#include "verifier.h"
#include "common.h"
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
#include <unordered_map>
#include <vector>
#include <memory>

class BCModuleSplitter {
private:
    std::vector<llvm::Function*> functionPtrs;
    std::vector<llvm::GlobalVariable*> globalVariablePtrs;
    BCCommon& common;
    Config config;
    Logger logger;
    BCVerifier verifier;

    int totalGroups = 0;
    SplitMode currentMode = MANUAL_MODE;

    // 辅助数据
    std::unordered_set<llvm::Function*> functionsNeedExternal;

    // 获取链接属性字符串表示
    std::string getLinkageString(llvm::GlobalValue::LinkageTypes linkage);

    // 获取可见性字符串表示
    std::string getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility);

public:
    BCModuleSplitter(BCCommon& commonRef);

    // 配置
    void setCloneMode(bool enable);

    // 文件操作
    bool loadBCFile(const std::string& filename);

    // 分析功能
    void analyzeFunctions();
    void printFunctionInfo();
    void generateGroupReport(const std::string& outputPrefix);
    std::unordered_set<llvm::Function*> collectInternalFunctionsFromGlobals();
    void analyzeInternalConstants();
    static void collectFunctionsFromConstant(llvm::Constant* C, std::unordered_set<llvm::Function*>& funcSet);
    static void findAndRecordUsage(llvm::User *user,
                                GlobalVariableInfo &info, std::set<llvm::User*> visited);
    // 分组获取功能
    std::vector<llvm::Function*> getUnprocessedExternalFunctions();
    std::vector<llvm::Function*> getHighInDegreeFunctions(int threshold = 200);
    std::vector<llvm::Function*> getIsolatedFunctions();
    std::unordered_set<llvm::GlobalVariable*> getGlobalVariables();
    std::vector<llvm::Function*> getTopFunctions(int topN);
    std::unordered_set<llvm::Function*> getFunctionGroup(llvm::Function* func);
    std::unordered_set<llvm::Function*> getOriginWithOutDegreeFunctions(
        const std::unordered_set<llvm::Function*>& highInDegreeFuncs);
    std::unordered_set<llvm::Function*> getStronglyConnectedComponent(
        const std::unordered_set<llvm::Function*>& originFuncs);

    // BC文件创建
    bool createGlobalVariablesBCFile(const std::unordered_set<llvm::GlobalVariable*>& globals,
                                   const std::string& filename);
    bool createBCFile(const std::unordered_set<llvm::Function*>& group,
                     const std::string& filename,
                     int groupIndex);

    // 核心拆分逻辑
    void splitBCFiles(const std::string& outputPrefix);

    // 访问器（用于测试或特殊情况）
    BCCommon& getCommon() { return common; }
    const BCCommon& getCommon() const { return common; }

    // 验证相关方法（包装器）
    void validateAllBCFiles(const std::string& outputPrefix);
    bool verifyAndFixBCFile(const std::string& filename,
                          const std::unordered_set<llvm::Function*>& expectedGroup);
    bool quickValidateBCFile(const std::string& filename);
    void analyzeBCFileContent(const std::string& filename);

private:
    // 私有辅助方法
    bool createBCFileWithSignatures(const std::unordered_set<llvm::Function*>& group,
                                  const std::string& filename, int groupIndex);
    bool createBCFileWithClone(const std::unordered_set<llvm::Function*>& group,
                             const std::string& filename,
                             int groupIndex);
    // Clone模式处理
    void processClonedModuleFunctions(llvm::Module& clonedModule,
                                    const std::unordered_set<llvm::Function*>& targetGroup,
                                    const std::unordered_set<llvm::Function*>& externalGroup);
    void processClonedModuleGlobals(llvm::Module& clonedModule);

    // 分组策略
    std::vector<std::pair<llvm::Function*, int>> getRemainingFunctions();
    std::unordered_set<llvm::Function*> getFunctionGroupByRange(
        const std::vector<std::pair<llvm::Function*, int>>& functions,
        int start, int end);
};

#endif // BC_SPLITTER_SPLITTER_H