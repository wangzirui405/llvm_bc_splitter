// core.h
#ifndef BC_SPLITTER_CORE_H
#define BC_SPLITTER_CORE_H

#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 链接类型枚举（与LLVM对应）
enum LinkageType {
    EXTERNAL_LINKAGE = 0,         ///< 外部可见
    AVAILABLE_EXTERNALLY_LINKAGE, ///< 可用于检查，但不发出
    LINK_ONCE_ANY_LINKAGE,        ///< 链接时保留一个副本（内联）
    LINK_ONCE_ODR_LINKAGE,        ///< 相同，但仅被等效内容替换
    WEAK_ANY_LINKAGE,             ///< 链接时保留一个命名副本（弱）
    WEAK_ODR_LINKAGE,             ///< 相同，但仅被等效内容替换
    APPENDING_LINKAGE,            ///< 特殊用途，仅适用于全局数组
    INTERNAL_LINKAGE,             ///< 链接时重命名冲突（静态）
    PRIVATE_LINKAGE,              ///< 类似于内部链接，但从符号表中省略
    EXTERNAL_WEAK_LINKAGE,        ///< 外部弱链接
    COMMON_LINKAGE                ///< 暂定定义
};

// 全局对象类型枚举
enum class GlobalValueType { FUNCTION, GLOBAL_VARIABLE };

// 全局对象信息结构体
struct GlobalValueInfo {
    // 基本信息
    GlobalValueType type;
    std::string name;
    std::string displayName;
    int preGroupIndex = -1;
    int groupIndex = -1;
    bool isPreProcessed = false;
    bool isProcessed = false;
    int sequenceNumber = -1; // 只有无名对象才有序号，有名对象为-1

    llvm::GlobalValue *globalvaluePtr = nullptr;

    // 链接属性
    LinkageType linkage = EXTERNAL_LINKAGE;
    std::string linkageString;
    bool dsoLocal = false;
    std::string visibility;
    bool isDeclaration = false;
    bool isDefinition = false;
    bool isExternal = false;
    bool isInternal = false;
    bool isWeak = false;
    bool isLinkOnce = false;
    bool isCommon = false;

    // 调用关系公共属性
    int outDegree = 0;
    int inDegree = 0;
    llvm::DenseSet<llvm::GlobalValue *> callers;
    llvm::DenseSet<llvm::GlobalValue *> calleds;

    // 函数特有属性
    struct {
        // personality函数（异常处理函数）
        llvm::DenseSet<llvm::Function *> personalityCalledFunctions;
        llvm::DenseSet<llvm::Function *> personalityCallerFunctions;
    } funcSpecific;

    // 全局变量特有属性
    struct {
        bool isConstant = false;
    } gvarSpecific;

    // 构造函数
    GlobalValueInfo() = default;

    GlobalValueInfo(llvm::GlobalValue *GV, int seqNum = -1);

    std::string getObjectType() const;
    std::string getObjectTypeDescription() const;
    std::string getFunctionType() const;
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

    // 判断是否是编译器生成的函数
    bool isCompilerGenerated() const;
    // 判断是否为无名函数
    bool isUnnamed() const;
    // 从LLVM函数更新属性
    void updateAttributesFromLLVM();
    // 判断指定函数的调用者是否全部在指定的组中
    static bool areAllCallersInGroup(llvm::GlobalValue *GV, const llvm::DenseSet<llvm::GlobalValue *> &group,
                                     const llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> &globalValueMap);
    // 判断指定函数的被调用者是否全部在指定的组中
    static bool areAllCalledsInGroup(llvm::GlobalValue *GV, const llvm::DenseSet<llvm::GlobalValue *> &group,
                                     const llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> &globalValueMap);
};

// 分组模式枚举
enum SplitMode { MANUAL_MODE, CLONE_MODE };

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

    int unnamedGlobalValues = 0;
    int namedGlobalValues = 0;

    int externalGlobalValues = 0;
    int internalGlobalValues = 0;
    int weakGlobalValues = 0;
    int linkOnceGlobalValues = 0;

    int compilerGenerated = 0;

  public:
    AttributeStats() = default;
    ~AttributeStats() = default;

    void addInfo(const GlobalValueInfo &GlobalValueInfo);
    std::string getSummary() const;
    std::string getLinkageSummary() const;
};

#endif // BC_SPLITTER_CORE_H