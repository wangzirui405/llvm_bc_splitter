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
    llvm::DenseSet<llvm::Function*> functionPtrs;
    llvm::DenseSet<llvm::GlobalVariable*> globalVariablePtrs;
    BCCommon& common;
    Config config;
    Logger logger;
    BCVerifier verifier;

    int totalGroups = 0;
    SplitMode currentMode = MANUAL_MODE;

    // 辅助数据
    llvm::DenseSet<llvm::Function*> functionsNeedExternal;

    // 获取链接属性字符串表示
    std::string getLinkageString(llvm::GlobalValue::LinkageTypes linkage);

    // 获取可见性字符串表示
    std::string getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility);

public:
    BCModuleSplitter(BCCommon& commonRef);

    // 配置
    void setCloneMode(bool enable);

    // 文件操作
    bool loadBCFile(llvm::StringRef filename);

    // 分析功能
    void analyzeFunctions();
    void printFunctionInfo();
    void generateGroupReport(llvm::StringRef outputPrefix);
    llvm::DenseSet<llvm::Function*> collectInternalFunctionsFromGlobals();
    void analyzeInternalConstants();
    static void collectFunctionsFromConstant(llvm::Constant* C, llvm::DenseSet<llvm::Function*>& funcSet);
    static void findAndRecordUsage(llvm::User *U,
                                GlobalVariableInfo &info, llvm::DenseSet<llvm::User*> visited);
    // 分组获取功能
    llvm::DenseSet<llvm::Function*> getUnprocessedExternalFunctions();
    llvm::DenseSet<llvm::Function*> getHighInDegreeFunctions(int threshold = 150);
    llvm::DenseSet<llvm::Function*> getIsolatedFunctions();
    llvm::DenseSet<llvm::GlobalVariable*> getGlobalVariables();
    llvm::SmallVector<llvm::Function*> getTopFunctions(int topN);
    llvm::DenseSet<llvm::Function*> getFunctionGroup(llvm::Function* F);
    llvm::DenseSet<llvm::Function*> getOriginWithOutDegreeFunctions(
        const llvm::DenseSet<llvm::Function*>& originFuncs);
    llvm::DenseSet<llvm::Function*> getStronglyConnectedComponent(
        const llvm::DenseSet<llvm::Function*>& originFuncs);

    // BC文件创建
    bool createGlobalVariablesBCFile(const llvm::DenseSet<llvm::GlobalVariable*>& globals,
                                   llvm::StringRef filename);
    bool createBCFile(const llvm::DenseSet<llvm::Function*>& group,
                     llvm::StringRef filename,
                     int groupIndex);

    // 核心拆分逻辑
    void splitBCFiles(llvm::StringRef outputPrefix);

    // 访问器（用于测试或特殊情况）
    BCCommon& getCommon() { return common; }
    const BCCommon& getCommon() const { return common; }

    // 验证相关方法（包装器）
    void validateAllBCFiles(llvm::StringRef outputPrefix);
    bool verifyAndFixBCFile(llvm::StringRef filename,
                          const llvm::DenseSet<llvm::Function*>& expectedGroup);
    bool quickValidateBCFile(llvm::StringRef filename);
    void analyzeBCFileContent(llvm::StringRef filename);

private:
    // 私有辅助方法
    bool createBCFileWithSignatures(const llvm::DenseSet<llvm::Function*>& group,
                                  llvm::StringRef filename, int groupIndex);
    bool createBCFileWithClone(const llvm::DenseSet<llvm::Function*>& group,
                             llvm::StringRef filename,
                             int groupIndex);
    // Clone模式处理
    void processClonedModuleFunctions(llvm::Module& M,
                                    const llvm::DenseSet<llvm::Function*>& targetGroup,
                                    const llvm::DenseSet<llvm::Function*>& externalGroup);
    void processClonedModuleGlobals(llvm::Module& M);

    // 分组策略
    llvm::SmallVector<std::pair<llvm::Function*, int>> getRemainingFunctions();
    llvm::SmallVector<llvm::Function*> getFunctionGroupByRange(
        const llvm::SmallVector<std::pair<llvm::Function*, int>>& functions,
        int start, int end);
};

#endif // BC_SPLITTER_SPLITTER_H