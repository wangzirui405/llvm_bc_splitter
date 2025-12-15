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

// 链接类型枚举（与LLVM对应）
enum LinkageType {
    EXTERNAL_LINKAGE = 0,           ///< 外部可见函数
    AVAILABLE_EXTERNALLY_LINKAGE,   ///< 可用于检查，但不发出
    LINK_ONCE_ANY_LINKAGE,          ///< 链接时保留一个副本（内联）
    LINK_ONCE_ODR_LINKAGE,          ///< 相同，但仅被等效内容替换
    WEAK_ANY_LINKAGE,               ///< 链接时保留一个命名函数副本（弱）
    WEAK_ODR_LINKAGE,               ///< 相同，但仅被等效内容替换
    APPENDING_LINKAGE,              ///< 特殊用途，仅适用于全局数组
    INTERNAL_LINKAGE,               ///< 链接时重命名冲突（静态函数）
    PRIVATE_LINKAGE,                ///< 类似于内部链接，但从符号表中省略
    EXTERNAL_WEAK_LINKAGE,          ///< 外部弱链接
    COMMON_LINKAGE                  ///< 暂定定义
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
    std::unordered_set<llvm::Function*> calledFunctions;
    std::unordered_set<llvm::Function*> callerFunctions;

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

    // 获取属性汇总字符串
    std::string getAttributesSummary() const;

    // 判断是否需要在拆分时保留
    bool shouldPreserveInSplit() const;

    // 判断是否可以安全删除
    bool canBeSafelyRemoved() const;

    // 判断是否是编译器生成的函数
    bool isCompilerGenerated() const;

    // 判断是否为无名函数
    bool isUnnamed() const;
    // 从LLVM函数更新属性
    void updateAttributesFromLLVM();
    // 辅助函数
    static bool isNumberString(const std::string& str);
    // 判断指定函数的调用者是否全部在指定的组中
    static bool areAllCallersInGroup(llvm::Function* func,
                         const std::unordered_set<llvm::Function*>& group,
                         const std::unordered_map<llvm::Function*, FunctionInfo>& functionMap);
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

    void addFunctionInfo(const FunctionInfo& funcInfo);
    std::string getSummary() const;
    std::string getLinkageSummary() const;
};

#endif // BC_SPLITTER_CORE_H