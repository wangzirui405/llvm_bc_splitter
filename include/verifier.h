// verifier.h
#ifndef BC_SPLITTER_VERIFIER_H
#define BC_SPLITTER_VERIFIER_H

#include <string>
#include <fstream>
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
#include "common.h"

class BCVerifier {
private:
    Logger logger;
    BCCommon& common;  // 引用共享数据
    Config config;

    // 辅助函数
    std::string decodeEscapeSequences(llvm::StringRef escapedStr);
    std::string getLinkageString(llvm::GlobalValue::LinkageTypes linkage);
    std::string getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility);

    // 函数名映射构建
    void buildFunctionNameMapsWithLog(const llvm::DenseSet<llvm::Function*>& group,
                                    llvm::StringMap<llvm::Function*>& nameToFunc,
                                    llvm::StringMap<std::string>& escapedToOriginal,
                                    std::ofstream& individualLog);

public:
    BCVerifier(BCCommon& commonRef);

    // 核心验证方法
    bool verifyFunctionSignature(llvm::Function* F);
    bool quickValidateBCFile(llvm::StringRef filename);
    bool quickValidateBCFileWithLog(llvm::StringRef filename, std::ofstream& individualLog);

    // 错误分析和修复
    llvm::StringSet<> analyzeVerifierErrorsWithLog(
        llvm::StringRef verifyOutput,
        const llvm::DenseSet<llvm::Function*>& group,
        std::ofstream& individualLog);

    bool verifyAndFixBCFile(llvm::StringRef filename,
                          const llvm::DenseSet<llvm::Function*>& expectedGroup);

    // 批量验证
    void validateAllBCFiles(llvm::StringRef outputPrefix, bool isCloneMode);

    // 内容分析
    void analyzeBCFileContent(llvm::StringRef filename);

    // 重命名和修复
    bool recreateBCFileWithExternalLinkage(const llvm::DenseSet<llvm::Function*>& group,
                                         const llvm::StringSet<>& externalFuncNames,
                                         llvm::StringRef filename,
                                         int groupIndex);
    void batchFixFunctionLinkageWithUnnamedSupport(llvm::Module& M,
                                                 const llvm::StringSet<>& externalFuncNames);

};

#endif // BC_SPLITTER_VERIFIER_H