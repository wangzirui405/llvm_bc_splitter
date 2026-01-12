// splitter.h
#ifndef BC_SPLITTER_SPLITTER_H
#define BC_SPLITTER_SPLITTER_H

#include "common.h"
#include "core.h"
#include "logging.h"
#include "optimizer.h"
#include "verifier.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
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
#include <memory>
#include <unordered_map>
#include <vector>

class BCModuleSplitter {
  private:
    llvm::DenseSet<llvm::GlobalValue *> globalValuePtrs;
    BCCommon &common;
    Config config;
    Logger logger;
    BCVerifier verifier;
    custom::CustomOptimizer optimizer;

    int totalGroups = 0;
    SplitMode currentMode = MANUAL_MODE;

    // 获取链接属性字符串表示
    std::string getLinkageString(llvm::GlobalValue::LinkageTypes linkage);

    // 获取可见性字符串表示
    std::string getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility);

  public:
    BCModuleSplitter(BCCommon &commonRef);

    // 配置
    void setCloneMode(bool enable);

    // 文件操作
    bool loadBCFile(llvm::StringRef filename);

    // 分析功能
    void analyzeFunctions();
    void printFunctionInfo();
    void generateGroupReport(llvm::StringRef outputPrefix);

    // 分组获取功能
    void getGlobalValueGroup(int groupIndex);
    llvm::DenseSet<llvm::GlobalValue *>
    getOriginWithOutDegreeGlobalValues(int preGroupId, const llvm::DenseSet<llvm::GlobalValue *> &originGVs);
    llvm::DenseSet<llvm::GlobalValue *>
    getStronglyConnectedComponent(int preGroupId, const llvm::DenseSet<llvm::GlobalValue *> &originGVs);

    // BC文件创建
    bool createGlobalVariablesBCFile(const llvm::DenseSet<llvm::GlobalVariable *> &globals, llvm::StringRef filename);
    bool createBCFile(const llvm::DenseSet<llvm::GlobalValue *> &group, llvm::StringRef filename, int groupIndex);

    // 核心拆分逻辑
    void splitBCFiles(llvm::StringRef outputPrefix);

    // 访问器（用于测试或特殊情况）
    BCCommon &getCommon() { return common; }
    const BCCommon &getCommon() const { return common; }

    // 验证相关方法（包装器）
    void validateAllBCFiles(llvm::StringRef outputPrefix);
    bool verifyAndFixBCFile(llvm::StringRef filename, const llvm::DenseSet<llvm::GlobalValue *> &expectedGroup);
    bool quickValidateBCFile(llvm::StringRef filename);
    void analyzeBCFileContent(llvm::StringRef filename);
    // 编译优化
    bool runOptimizationAndVerify(llvm::Module &M);

  private:
    // 私有辅助方法
    bool createBCFileWithClone(const llvm::DenseSet<llvm::GlobalValue *> &group, llvm::StringRef filename,
                               int groupIndex);
    // Clone模式处理
    void processClonedModuleGlobalValues(llvm::Module &M, const llvm::DenseSet<llvm::GlobalValue *> &targetGroup,
                                         const llvm::DenseSet<llvm::GlobalValue *> &externalGroup);
};

#endif // BC_SPLITTER_SPLITTER_H