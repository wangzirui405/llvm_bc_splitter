// linker.h
#ifndef BC_SPLITTER_LINKER_H
#define BC_SPLITTER_LINKER_H

#include <future> // 包含std::promise和std::future
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common.h"
#include "core.h"
#include "logging.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

class BCLinker {
  private:
    Logger logger;
    Config config;
    BCCommon &common; // 引用共享数据
    std::string currenpath;
    std::mutex logMutex; // 用于保护logger的并发访问

    // 用于跟踪阶段完成状态 - 使用智能指针避免悬空引用
    llvm::DenseMap<int, std::shared_ptr<std::promise<void>>> phase1Promises;
    llvm::DenseMap<int, std::shared_future<void>> phase1Futures;
    std::mutex phaseMutex;

  public:
    BCLinker(BCCommon &commonRef);

    // 核心功能
    void initphase1();
    void printFileMapDetails();
    llvm::SmallVector<llvm::StringRef, 200> readResponseFile();
    void generateInputFiles(llvm::StringRef outputPrefix);
    static bool executeLdLld(llvm::StringRef responseFilePath, llvm::StringRef extralCommand);
    void processGroupTask(int groupId, std::promise<bool> &promise);
    bool executeAllGroups();
    bool enterInWorkDir();
    bool returnCurrenPath();
    bool copySoFilesToOutput();
    void setPhase1Promise(int groupId);
    std::shared_future<void> getPhase1Future(int groupId);
};

#endif // BC_SPLITTER_LINKER_H