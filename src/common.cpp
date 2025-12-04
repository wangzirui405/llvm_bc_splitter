#include "common.h"

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

BCCommon::BCCommon() : context(nullptr) {}

BCCommon::~BCCommon() {
    clear();
}

void BCCommon::clear() {
    module.reset();
    functionMap.clear();
    context = nullptr;
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