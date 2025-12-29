// verifier.h
#ifndef BC_SPLITTER_VERIFIER_H
#define BC_SPLITTER_VERIFIER_H

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
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
    std::string decodeEscapeSequences(const std::string& escapedStr);
    std::string getLinkageString(llvm::GlobalValue::LinkageTypes linkage);
    std::string getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility);

    // 函数名映射构建
    void buildFunctionNameMapsWithLog(const std::unordered_set<llvm::Function*>& group,
                                    std::unordered_map<std::string, llvm::Function*>& nameToFunc,
                                    std::unordered_map<std::string, std::string>& escapedToOriginal,
                                    std::ofstream& individualLog);

public:
    BCVerifier(BCCommon& commonRef);

    // 核心验证方法
    bool verifyFunctionSignature(llvm::Function* F);
    bool quickValidateBCFile(const std::string& filename);
    bool quickValidateBCFileWithLog(const std::string& filename, std::ofstream& individualLog);

    // 错误分析和修复
    std::unordered_set<std::string> analyzeVerifierErrorsWithLog(
        const std::string& verifyOutput,
        const std::unordered_set<llvm::Function*>& group,
        std::ofstream& individualLog);

    bool verifyAndFixBCFile(const std::string& filename,
                          const std::unordered_set<llvm::Function*>& expectedGroup);

    // 批量验证
    void validateAllBCFiles(const std::string& outputPrefix, bool isCloneMode);

    // 内容分析
    void analyzeBCFileContent(const std::string& filename);

    // 重命名和修复
    bool recreateBCFileWithExternalLinkage(const std::unordered_set<llvm::Function*>& group,
                                         const std::unordered_set<std::string>& externalFuncNames,
                                         const std::string& filename,
                                         int groupIndex);
    void batchFixFunctionLinkageWithUnnamedSupport(llvm::Module& M,
                                                 const std::unordered_set<std::string>& externalFuncNames);

};

#endif // BC_SPLITTER_VERIFIER_H