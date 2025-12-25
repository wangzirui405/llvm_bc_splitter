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
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"

// 链接类型枚举（与LLVM对应）
enum LinkageType {
    EXTERNAL_LINKAGE = 0,           ///< 外部可见
    AVAILABLE_EXTERNALLY_LINKAGE,   ///< 可用于检查，但不发出
    LINK_ONCE_ANY_LINKAGE,          ///< 链接时保留一个副本（内联）
    LINK_ONCE_ODR_LINKAGE,          ///< 相同，但仅被等效内容替换
    WEAK_ANY_LINKAGE,               ///< 链接时保留一个命名副本（弱）
    WEAK_ODR_LINKAGE,               ///< 相同，但仅被等效内容替换
    APPENDING_LINKAGE,              ///< 特殊用途，仅适用于全局数组
    INTERNAL_LINKAGE,               ///< 链接时重命名冲突（静态）
    PRIVATE_LINKAGE,                ///< 类似于内部链接，但从符号表中省略
    EXTERNAL_WEAK_LINKAGE,          ///< 外部弱链接
    COMMON_LINKAGE                  ///< 暂定定义
};

struct BasicBlockInfo {
    std::string name;
    std::vector<llvm::Instruction*> instructions;
    std::unordered_set<std::string> successors;   // 后继基本块
    std::unordered_set<std::string> predecessors; // 前驱基本块（新增）
    bool isLandingPad = false;
    bool isCleanupPad = false;
    bool isCatchPad = false;

    BasicBlockInfo() = default;
    BasicBlockInfo(const std::string& name) : name(name) {}

    // 获取基本块的完整信息
    std::string getInfo() const {
        std::string info = name;
        if (isLandingPad) info += " [LandingPad]";
        if (isCleanupPad) info += " [CleanupPad]";
        if (isCatchPad) info += " [CatchPad]";
        info += "\n  Predecessors: ";
        for (const auto& pred : predecessors) {
            info += pred + " ";
        }
        info += "\n  Successors: ";
        for (const auto& succ : successors) {
            info += succ + " ";
        }
        return info;
    }
};

// Invoke指令分析结果
struct InvokeInfo {
    llvm::InvokeInst* invokeInst = nullptr;
    std::string normalTarget;     // 正常流程目标基本块
    std::string unwindTarget;     // 异常处理目标基本块
    llvm::Value* calledValue = nullptr;  // 被调用的值（可能是函数或函数指针）
    llvm::Function* calledFunction = nullptr;  // 如果可直接确定函数

    // 是否为间接调用（通过函数指针）
    bool isIndirectCall = false;

    // 调用参数信息
    std::vector<llvm::Type*> argTypes;
    llvm::Type* returnType = nullptr;
};

// 异常处理路径信息
struct ExceptionPathInfo {
    std::string landingPadBlock;  // landingpad基本块
    std::unordered_set<llvm::Function*> personalityCalls;  // personality函数调用
    std::unordered_set<llvm::Function*> cleanupCalls;      // cleanup函数调用
    std::unordered_set<llvm::Function*> catchCalls;        // catch函数调用
    bool hasResume = false;
};

// 间接调用分析
struct IndirectCallInfo {
    llvm::CallBase* callInst;     // 调用指令（可能是CallInst或InvokeInst）
    llvm::Value* calledValue;     // 被调用的值
    llvm::Type* funcType;         // 函数类型
    bool isInvoke = false;        // 是否为invoke指令
    std::string normalTarget;     // invoke的正常目标
    std::string unwindTarget;     // invoke的异常目标
};

// 控制流图信息
struct CFGEdge {
    std::string fromBlock;
    std::string toBlock;
    bool isNormalFlow = true;     // true=正常流程, false=异常流程
    bool isDirectCall = false;    // 是否直接函数调用
    llvm::Function* calledFunc = nullptr;  // 调用的函数（如果有）
};

// 函数信息结构体
struct FunctionInfo {
    std::string name;
    std::string displayName;
    llvm::Function* funcPtr = nullptr;
    int outDegree = 0;
    int inDegree = 0;
    int groupIndex = -1;
    bool isProcessed = false;
    bool isReferencedByGlobals = false;
    int sequenceNumber = -1;  // 只有无名函数才有序号，有名函数为-1

    // 新增的属性
    LinkageType linkage = EXTERNAL_LINKAGE;  // 链接属性
    std::string linkageString;               // 链接属性字符串表示
    bool dsoLocal = false;                   // DSO本地属性
    std::string visibility;                  // 可见性属性
    bool isDeclaration = false;              // 是否是声明（没有函数体）
    bool isDefinition = false;               // 是否是定义（有函数体）
    bool isExternal = false;                 // 是否外部链接
    bool isInternal = false;                 // 是否内部链接
    bool isWeak = false;                     // 是否弱链接
    bool isLinkOnce = false;                 // 是否LinkOnce链接
    bool isCommon = false;                   // 是否Common链接

    // 函数使用信息
    std::unordered_set<llvm::Function*> callerFunctions;
    std::unordered_set<llvm::Function*> calledFunctions;
    std::unordered_set<llvm::Function*> personalityCalledFunctions;      // personality函数（异常处理函数）
    std::unordered_set<llvm::Function*> personalityCallerFunctions;

    FunctionInfo() = default;
    FunctionInfo(llvm::Function* func, int seqNum = -1);

    // 获取函数类型描述
    std::string getFunctionType() const;

    // 获取链接属性描述
    std::string getLinkageString() const;

    // 获取链接类型简写
    std::string getLinkageAbbreviation() const;

    // 获取可见性描述
    std::string getVisibilityString() const;

    // 获取完整信息字符串
    std::string getFullInfo() const;

    // 获取简略信息字符串
    std::string getBriefInfo() const;

    // 判断是否是编译器生成的函数
    bool isCompilerGenerated() const;
    // 判断是否为无名函数
    bool isUnnamed() const;
    // 从LLVM函数更新属性
    void updateAttributesFromLLVM();
    // 判断指定函数的调用者是否全部在指定的组中
    static bool areAllCallersInGroup(llvm::Function* func,
                         const std::unordered_set<llvm::Function*>& group,
                         const std::unordered_map<llvm::Function*, FunctionInfo>& functionMap);
    // 判断指定函数的被调用者是否全部在指定的组中
    static bool areAllCalledsInGroup(llvm::Function* func,
                         const std::unordered_set<llvm::Function*>& group,
                         const std::unordered_map<llvm::Function*, FunctionInfo>& functionMap);

};

// 全局变量信息结构体
struct GlobalVariableInfo {
    std::string name;
    std::string displayName;
    llvm::GlobalVariable* gvPtr = nullptr;
    int groupIndex = -1;
    bool isProcessed = false;
    int sequenceNumber = -1;  // 只有无名全局变量才有序号，有名全局变量为-1

    // 新增的属性
    LinkageType linkage = EXTERNAL_LINKAGE;  // 链接属性
    std::string linkageString;               // 链接属性字符串表示
    bool dsoLocal = false;                   // DSO本地属性
    std::string visibility;                  // 可见性属性
    bool isConstant = false;                 // 是否是常量
    bool isDeclaration = false;              // 是否是声明
    bool isDefinition = false;               // 是否是定义
    bool isExternal = false;                 // 是否外部链接
    bool isInternal = false;                 // 是否内部链接
    bool isWeak = false;                     // 是否弱链接
    bool isLinkOnce = false;                 // 是否LinkOnce链接
    bool isCommon = false;                   // 是否Common链接

    // 全局变量使用信息
    std::unordered_set<llvm::Function*> calleds;
    std::unordered_set<llvm::Function*> callers;

    GlobalVariableInfo() = default;
    GlobalVariableInfo(llvm::GlobalVariable* gvPtr, int seqNum = -1);

    // 获取全局变量类型描述
    std::string getGlobalVariableType() const;

    // 获取链接属性描述
    std::string getLinkageString() const;

    // 获取链接类型简写
    std::string getLinkageAbbreviation() const;

    // 获取可见性描述
    std::string getVisibilityString() const;

    // 获取完整信息字符串
    std::string getFullInfo() const;

    // 获取简略信息字符串
    std::string getBriefInfo() const;

    // 判断是否是编译器生成的全局变量
    bool isCompilerGenerated() const;
    // 判断是否为无名全局变量
    bool isUnnamed() const;
    // 从LLVM全局变量更新属性
    void updateAttributesFromLLVM();

};

// 分组模式枚举
enum SplitMode {
    MANUAL_MODE,
    CLONE_MODE
};

// 属性统计结构体
struct AttributeStats {
    int externalLinkage = 0;
    int availableExternallyLinkage = 0;
    int linkOnceAnyLinkage = 0;
    int linkOnceODRLinkage = 0;
    int weakAnyLinkage = 0;
    int weakODRLinkage = 0;
    int appendingLinkage = 0;
    int internalLinkage = 0;
    int privateLinkage = 0;
    int externalWeakLinkage = 0;
    int commonLinkage = 0;

    int dsoLocalCount = 0;
    int defaultVisibility = 0;
    int hiddenVisibility = 0;
    int protectedVisibility = 0;
    int declarations = 0;
    int definitions = 0;

    int unnamedFunctions = 0;
    int namedFunctions = 0;

    int externalFunctions = 0;
    int internalFunctions = 0;
    int weakFunctions = 0;
    int linkOnceFunctions = 0;

    int unnamedGlobalVariables = 0;
    int namedGlobalVariables = 0;

    int externalGlobalVariables = 0;
    int internalGlobalVariables = 0;
    int weakGlobalVariables = 0;
    int linkOnceGlobalVariables = 0;

    int compilerGenerated = 0;

public:
    AttributeStats() = default;
    ~AttributeStats() = default;

    void addFunctionInfo(const FunctionInfo& funcInfo);
    void addGlobalVariableInfo(const GlobalVariableInfo& globalVariableInfo);
    std::string getFunctionsSummary() const;
    std::string getFunctionsLinkageSummary() const;
    std::string getGlobalVariablesSummary() const;
    std::string getGlobalVariablesLinkageSummary() const;
};

#endif // BC_SPLITTER_CORE_H