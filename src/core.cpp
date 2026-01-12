// core.cpp
#include "core.h"
#include "common.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include <algorithm>
#include <cctype>
#include <sstream>

// 获取对象类型描述
std::string GlobalValueInfo::getObjectType() const {
    switch (type) {
    case GlobalValueType::FUNCTION:
        return "符号";
    case GlobalValueType::GLOBAL_VARIABLE:
        return "全局变量";
    default:
        return "Unknown";
    }
}

// 获取对象类型描述
std::string GlobalValueInfo::getObjectTypeDescription() const {
    if (type == GlobalValueType::FUNCTION) {
        return getFunctionType();
    } else {
        return getGlobalVariableType();
    }
}

// 获取符号类型描述（仅符号有效）
std::string GlobalValueInfo::getFunctionType() const {
    if (type != GlobalValueType::FUNCTION)
        return "";

    std::string prefix = "";
    if (isUnnamed()) {
        prefix = "[序号:" + std::to_string(sequenceNumber) + "]无名";
    } else {
        prefix = "有名";
    }

    if (isDeclaration)
        return prefix + "符号(声明)";
    if (isDefinition)
        return prefix + "符号(定义)";

    return prefix + "符号";
}

// 获取全局变量类型描述（仅全局变量有效）
std::string GlobalValueInfo::getGlobalVariableType() const {
    if (type != GlobalValueType::GLOBAL_VARIABLE)
        return "";

    std::string typeDesc;
    if (gvarSpecific.isConstant)
        typeDesc += "Constant ";
    typeDesc += "Global Variable";
    if (isDeclaration)
        typeDesc += " Declaration";
    if (isDefinition)
        typeDesc += " Definition";
    return typeDesc;
}

GlobalValueInfo::GlobalValueInfo(llvm::GlobalValue *GV, int seqNum) {
    globalvaluePtr = GV;
    name = GV->getName().str();
    isDeclaration = GV->isDeclaration();     // 是否是声明
    isDefinition = GV->hasExactDefinition(); // 是否是定义

    // 更新LLVM相关属性
    updateAttributesFromLLVM();

    // 如果是全局变量，更新isConstant属性
    if (auto *GVar = llvm::dyn_cast<llvm::GlobalVariable>(GV)) {
        gvarSpecific.isConstant = GVar->isConstant();
        type = GlobalValueType::GLOBAL_VARIABLE;
    } else if (auto *F = llvm::dyn_cast<llvm::Function>(GV)) {
        type = GlobalValueType::FUNCTION;
    }

    // 检测是否为无名符号
    bool isUnnamedFunc = isUnnamed();

    if (isUnnamedFunc) {
        // 只有无名符号才分配序号
        sequenceNumber = seqNum;
        displayName = "__unnamed_" + std::to_string(seqNum);
    } else {
        // 有名符号没有序号
        sequenceNumber = -1;
        displayName = name;
    }
}

bool GlobalValueInfo::isUnnamed() const {
    if (name.empty())
        return true;

    // 检查常见的无名符号模式
    if (name.find("__unnamed_") == 0)
        return true;
    // 检查单字母符号名（可能是简化的内部符号）
    if (name == "d" || name == "t" || name == "b" || name == "f" || name == "g" || name == "h" || name == "i" ||
        name == "j" || name == "k") {
        return true;
    }

    // 检查是否全为数字
    if (BCCommon::isNumberString(name))
        return true;

    return false;
}

bool GlobalValueInfo::isCompilerGenerated() const {
    // 检查是否是编译器生成的符号
    if (isUnnamed())
        return true;

    // 检查常见编译器生成符号模式
    if (name.find("llvm.") == 0)
        return true; // LLVM内置符号
    if (name.find("__llvm") == 0)
        return true; // LLVM编译器生成
    if (name.find("__clang") == 0)
        return true; // Clang生成
    if (name.find("__gcc") == 0)
        return true; // GCC生成

    return false;
}

std::string GlobalValueInfo::getLinkageString() const { return linkageString; }

std::string GlobalValueInfo::getLinkageAbbreviation() const {
    switch (linkage) {
    case EXTERNAL_LINKAGE:
        return "EXT";
    case AVAILABLE_EXTERNALLY_LINKAGE:
        return "AVEXT";
    case LINK_ONCE_ANY_LINKAGE:
        return "LOA";
    case LINK_ONCE_ODR_LINKAGE:
        return "LOO";
    case WEAK_ANY_LINKAGE:
        return "WKA";
    case WEAK_ODR_LINKAGE:
        return "WKO";
    case APPENDING_LINKAGE:
        return "APP";
    case INTERNAL_LINKAGE:
        return "INT";
    case PRIVATE_LINKAGE:
        return "PRI";
    case EXTERNAL_WEAK_LINKAGE:
        return "EXWK";
    case COMMON_LINKAGE:
        return "COM";
    default:
        return "UNK";
    }
}

std::string GlobalValueInfo::getVisibilityString() const {
    if (visibility.empty()) {
        return "未知可见性";
    }
    return visibility;
}

void GlobalValueInfo::updateAttributesFromLLVM() {
    if (!globalvaluePtr)
        return;

    // 获取链接属性
    llvm::GlobalValue::LinkageTypes llvmLinkage = globalvaluePtr->getLinkage();

    // 转换为我们的枚举类型
    switch (llvmLinkage) {
    case llvm::GlobalValue::ExternalLinkage:
        linkage = EXTERNAL_LINKAGE;
        linkageString = "External";
        isExternal = true;
        break;
    case llvm::GlobalValue::AvailableExternallyLinkage:
        linkage = AVAILABLE_EXTERNALLY_LINKAGE;
        linkageString = "AvailableExternally";
        break;
    case llvm::GlobalValue::LinkOnceAnyLinkage:
        linkage = LINK_ONCE_ANY_LINKAGE;
        linkageString = "LinkOnceAny";
        isLinkOnce = true;
        break;
    case llvm::GlobalValue::LinkOnceODRLinkage:
        linkage = LINK_ONCE_ODR_LINKAGE;
        linkageString = "LinkOnceODR";
        isLinkOnce = true;
        break;
    case llvm::GlobalValue::WeakAnyLinkage:
        linkage = WEAK_ANY_LINKAGE;
        linkageString = "WeakAny";
        isWeak = true;
        break;
    case llvm::GlobalValue::WeakODRLinkage:
        linkage = WEAK_ODR_LINKAGE;
        linkageString = "WeakODR";
        isWeak = true;
        break;
    case llvm::GlobalValue::AppendingLinkage:
        linkage = APPENDING_LINKAGE;
        linkageString = "Appending";
        break;
    case llvm::GlobalValue::InternalLinkage:
        linkage = INTERNAL_LINKAGE;
        linkageString = "Internal";
        isInternal = true;
        break;
    case llvm::GlobalValue::PrivateLinkage:
        linkage = PRIVATE_LINKAGE;
        linkageString = "Private";
        isInternal = true;
        break;
    case llvm::GlobalValue::ExternalWeakLinkage:
        linkage = EXTERNAL_WEAK_LINKAGE;
        linkageString = "ExternalWeak";
        isWeak = true;
        break;
    case llvm::GlobalValue::CommonLinkage:
        linkage = COMMON_LINKAGE;
        linkageString = "Common";
        isCommon = true;
        break;
    default:
        linkage = EXTERNAL_LINKAGE;
        linkageString = "Unknown";
        break;
    }

    // 获取DSO本地属性
    dsoLocal = globalvaluePtr->isDSOLocal();

    // 获取可见性属性
    switch (globalvaluePtr->getVisibility()) {
    case llvm::GlobalValue::DefaultVisibility:
        visibility = "Default";
        break;
    case llvm::GlobalValue::HiddenVisibility:
        visibility = "Hidden";
        break;
    case llvm::GlobalValue::ProtectedVisibility:
        visibility = "Protected";
        break;
    default:
        visibility = "Unknown";
        break;
    }
}

std::string GlobalValueInfo::getFullInfo() const {
    std::stringstream ss;

    // 基本信息部分
    ss << "========== GV信息 ==========\n";
    ss << "显示名称: " << displayName << "\n";
    if (!name.empty()) {
        ss << "-- 内部名称: " << name << "\n";
    }
    if (sequenceNumber != -1) {
        ss << "序列号: #" << sequenceNumber << ", ";
    }
    ss << "类型: " << getObjectType() << ",";
    ss << "指针: " << (globalvaluePtr ? "有效" : "空") << "\n";

    // 链接属性部分
    ss << "\n--- 链接属性 ---\n";
    ss << "链接类型: " << linkageString << ", ";
    ss << "可见性: " << (visibility.empty() ? "默认" : visibility) << ", ";
    ss << "DSO本地: " << (dsoLocal ? "是" : "否") << ", ";
    ss << "是否声明: " << (isDeclaration ? "是" : "否") << ", ";
    ss << "是否定义: " << (isDefinition ? "是" : "否") << ", ";
    if (type == GlobalValueType::GLOBAL_VARIABLE) {
        ss << "是否为全局常量" << (gvarSpecific.isConstant ? "是" : "否") << "; ";
    }

    // 详细链接类型
    ss << "外部链接: " << (isExternal ? "是" : "否") << ", ";
    ss << "内部链接: " << (isInternal ? "是" : "否") << ", ";
    ss << "弱链接: " << (isWeak ? "是" : "否") << ", ";
    ss << "LinkOnce: " << (isLinkOnce ? "是" : "否") << ", ";
    ss << "Common: " << (isCommon ? "是" : "否") << "\n";

    // 调用关系部分
    ss << "\n--- 调用关系 ---\n";
    ss << "入度: " << inDegree << ", ";
    ss << "出度: " << outDegree << "\n";
    ss << "调用者数量: " << callers.size() << "\n";
    if (callers.size() < 10) {
        for (const auto *caller : callers) {
            ss << "  -- " << caller->getName().str() << "\n";
        }
    }
    ss << "被调用者数量: " << calleds.size() << "\n";
    if (calleds.size() < 10) {
        for (const auto *called : calleds) {
            ss << "  -- " << called->getName().str() << "\n";
        }
    }
    if (type == GlobalValueType::FUNCTION) {
        ss << "个性符号数量: " << funcSpecific.personalityCalledFunctions.size() << "\n";
        if (funcSpecific.personalityCalledFunctions.size() < 10) {
            for (const auto *personalityCalledFunction : funcSpecific.personalityCalledFunctions) {
                ss << "  -- " << personalityCalledFunction->getName().str() << "\n";
            }
        }
    }

    return ss.str();
}

std::string GlobalValueInfo::getBriefInfo() const {
    std::stringstream ss;

    // 符号标识
    if (sequenceNumber != -1) {
        ss << "[#" << sequenceNumber << "] ";
    }
    ss << displayName;
    if (!name.empty() && name != displayName) {
        ss << " (" << name << ", ";
    } else {
        ss << " (";
    }
    ss << getObjectType() << ")";

    // 链接属性
    ss << " [" << linkageString;
    if (dsoLocal)
        ss << ", dso_local";
    if (!visibility.empty())
        ss << ", " << visibility;
    ss << "]";

    // 定义状态
    if (isDeclaration) {
        ss << " [声明]";
    } else if (isDefinition) {
        ss << " [定义]";
    }

    // 调用关系
    ss << " 入度:" << inDegree << " 出度:" << outDegree;

    return ss.str();
}

/**
 * 判断指定符号的调用者是否全部在指定的组中
 *
 * @param GV 要检查的符号，该符号必须在group和globalValueMap中
 * @param group 符号组
 * @param globalValueMap 符号信息映射表
 * @return true 如果GV的所有调用者都在group中
 * @return false 如果GV有调用者不在group中
 * @throws std::invalid_argument 如果GV不在globalValueMap中
 */
bool GlobalValueInfo::areAllCallersInGroup(llvm::GlobalValue *GV, const llvm::DenseSet<llvm::GlobalValue *> &group,
                                           const llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> &globalValueMap) {
    // 参数检查
    if (group.find(GV) == group.end()) {
        throw std::invalid_argument("GlobalValue must be in the group");
    }

    auto infoIt = globalValueMap.find(GV);
    if (infoIt == globalValueMap.end()) {
        throw std::invalid_argument("GlobalValue must be in globalValueMap");
    }

    const auto &callers = infoIt->second.callers;

    // 如果符号没有调用者，那么所有调用者（没有）都在group中
    if (callers.empty()) {
        return true;
    }

    // 获取当前符号的 isProcessed 状态
    bool currentGVProcessed = infoIt->second.isProcessed;

    // 检查每个调用者是否都在group中，并检查 isProcessed 状态
    for (llvm::GlobalValue *caller : callers) {
        // 检查调用者是否在group中
        if (group.find(caller) == group.end()) {
            return false; // 发现一个不在group中的调用者
        }

        // 检查调用者的 isProcessed 状态
        auto callerInfoIt = globalValueMap.find(caller);
        if (callerInfoIt == globalValueMap.end()) {
            throw std::invalid_argument("Caller globalValue must be in globalValueMap");
        }

        bool callerProcessed = callerInfoIt->second.isProcessed;

        // 如果一个已被处理一个未被处理，则一定不同组
        if (currentGVProcessed != callerProcessed) {
            return false;
        }
    }

    return true; // 所有调用者都在group中且isProcessed状态一致
}

/**
 * 判断指定符号的被调用者是否全部在指定的组中
 *
 * @param GV 要检查的符号，该符号必须在group和globalValueMap中
 * @param group 符号组
 * @param globalValueMap 符号信息映射表
 * @return true 如果GV的所有被调用者都在group中
 * @return false 如果GV有被调用者不在group中
 * @throws std::invalid_argument 如果F不在group或globalValueMap中
 */
bool GlobalValueInfo::areAllCalledsInGroup(llvm::GlobalValue *GV, const llvm::DenseSet<llvm::GlobalValue *> &group,
                                           const llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> &globalValueMap) {
    // 参数检查
    if (group.find(GV) == group.end()) {
        throw std::invalid_argument("GlobalValue must be in the group");
    }

    auto infoIt = globalValueMap.find(GV);
    if (infoIt == globalValueMap.end()) {
        throw std::invalid_argument("GlobalValue must be in globalValueMap");
    }

    const auto &calleds = infoIt->second.calleds;

    // 如果符号没有被调用者，那么所有被调用者（没有）都在group中
    if (calleds.empty()) {
        return true;
    }

    // 获取当前符号的 isProcessed 状态
    bool currentGVProcessed = infoIt->second.isProcessed;

    // 检查每个被调用者是否都在group中，并检查 isProcessed 状态
    for (llvm::GlobalValue *called : calleds) {
        // 检查被调用者是否在group中
        if (group.find(called) == group.end()) {
            return false; // 发现一个不在group中的被调用者
        }

        // 检查被调用者的 isProcessed 状态
        auto calledInfoIt = globalValueMap.find(called);
        if (calledInfoIt == globalValueMap.end()) {
            throw std::invalid_argument("Called globalValue must be in globalValueMap");
        }

        bool calledProcessed = calledInfoIt->second.isProcessed;

        // 如果一个已被处理一个未被处理，则一定不同组
        if (currentGVProcessed != calledProcessed) {
            return false;
        }
    }

    return true; // 所有被调用者都在group中且isProcessed状态一致
}
