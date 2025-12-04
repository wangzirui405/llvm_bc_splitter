// core.h
#ifndef BC_SPLITTER_CORE_H
#define BC_SPLITTER_CORE_H

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <queue>
#include <memory>
#include <functional>
#include <cstdint>
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"

// 函数信息结构体
struct FunctionInfo {
    std::string name;
    std::string displayName;
    llvm::Function* funcPtr = nullptr;
    int outDegree = 0;
    int inDegree = 0;
    int groupIndex = -1;
    bool isProcessed = false;
    int sequenceNumber = -1;  // 只有无名函数才有序号，有名函数为-1
    std::unordered_set<llvm::Function*> calledFunctions;
    std::unordered_set<llvm::Function*> callerFunctions;

    FunctionInfo() = default;
    FunctionInfo(llvm::Function* func, int seqNum = -1);

    // 辅助函数
    static bool isNumberString(const std::string& str);

    // 判断是否为无名函数
    bool isUnnamed() const;

    // 获取函数类型描述
    std::string getFunctionType() const;

    // 获取完整信息字符串
    std::string getFullInfo() const;
};

// 分组模式枚举
enum SplitMode {
    MANUAL_MODE,
    CLONE_MODE
};

#endif // BC_SPLITTER_CORE_H