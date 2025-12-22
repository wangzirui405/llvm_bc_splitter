// core.cpp
#include "core.h"
#include "common.h"
#include <algorithm>
#include <cctype>
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include <sstream>

FunctionInfo::FunctionInfo(llvm::Function* func, int seqNum) {
    funcPtr = func;
    name = func->getName().str();
    isDeclaration = func->isDeclaration();  // 是否是声明
    isDefinition = func->hasExactDefinition();  // 是否是定义

    // 更新LLVM相关属性
    updateAttributesFromLLVM();
    updateExceptionInfo();

    // 检测是否为无名函数
    bool isUnnamedFunc = isUnnamed();

    if (isUnnamedFunc) {
        // 只有无名函数才分配序号
        sequenceNumber = seqNum;
        displayName = "__unnamed_" + std::to_string(seqNum);
    } else {
        // 有名函数没有序号
        sequenceNumber = -1;
        displayName = name;
    }
}

bool FunctionInfo::isUnnamed() const {
    if (name.empty()) return true;

    // 检查常见的无名函数模式
    if (name.find("__unnamed_") == 0) return true;
    // 检查单字母函数名（可能是简化的内部函数）
    if (name == "d" || name == "t" || name == "b" ||
        name == "f" || name == "g" || name == "h" ||
        name == "i" || name == "j" || name == "k") {
        return true;
    }

    // 检查是否全为数字
    if (BCCommon::isNumberString(name)) return true;

    return false;
}

std::string FunctionInfo::getFunctionType() const {
    if (isUnnamed()) {
        return "无名函数 [序号:" + std::to_string(sequenceNumber) + "]";
    } else {
        return "有名函数";
    }
}

std::string FunctionInfo::getLinkageString() const {
    return linkageString;
}

std::string FunctionInfo::getLinkageAbbreviation() const {
    switch (linkage) {
        case EXTERNAL_LINKAGE: return "EXT";
        case AVAILABLE_EXTERNALLY_LINKAGE: return "AVEXT";
        case LINK_ONCE_ANY_LINKAGE: return "LOA";
        case LINK_ONCE_ODR_LINKAGE: return "LOO";
        case WEAK_ANY_LINKAGE: return "WKA";
        case WEAK_ODR_LINKAGE: return "WKO";
        case APPENDING_LINKAGE: return "APP";
        case INTERNAL_LINKAGE: return "INT";
        case PRIVATE_LINKAGE: return "PRI";
        case EXTERNAL_WEAK_LINKAGE: return "EXWK";
        case COMMON_LINKAGE: return "COM";
        default: return "UNK";
    }
}

std::string FunctionInfo::getVisibilityString() const {
    if (visibility.empty()) {
        return "未知可见性";
    }
    return visibility;
}

void FunctionInfo::updateAttributesFromLLVM() {
    if (!funcPtr) return;

    // 获取链接属性
    llvm::GlobalValue::LinkageTypes llvmLinkage = funcPtr->getLinkage();

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
    dsoLocal = funcPtr->isDSOLocal();

    // 获取可见性属性
    switch (funcPtr->getVisibility()) {
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

bool FunctionInfo::isCompilerGenerated() const {
    // 检查是否是编译器生成的函数
    if (isUnnamed()) return true;

    // 检查常见编译器生成函数模式
    if (name.find("llvm.") == 0) return true;  // LLVM内置函数
    if (name.find("__llvm") == 0) return true; // LLVM编译器生成
    if (name.find("__clang") == 0) return true; // Clang生成
    if (name.find("__gcc") == 0) return true;   // GCC生成

    return false;
}

std::string FunctionInfo::getFullInfo() const {
    std::string info = displayName +
           " [" + getFunctionType() +
           ", 入度:" + std::to_string(inDegree) +
           ", 出度:" + std::to_string(outDegree) +
           ", 总分:" + std::to_string(inDegree + outDegree) +
           ", 链接:" + getLinkageAbbreviation() + "(" + getLinkageString() + ")" +
           ", DSO本地:" + (dsoLocal ? "是" : "否") +
           ", 可见性:" + getVisibilityString() +
           ", 类型:" + (isDeclaration ? "声明" : "定义") +
           ", 被全局变量引用: " + (isReferencedByGlobals ? "是" : "否") +
           ", EH: " + getExceptionAttributes() +
           ", 组:" + (groupIndex >= 0 ? std::to_string(groupIndex) : "未分组") + "]";

    // 添加异常处理信息
    if (getExceptionInfo().size()) {
        info += "\n  异常处理信息: " + getExceptionInfo();
    }

    if (!invokeCalledFunctions.empty()) {
        info += "\n  Invoke方式调用了 (" + std::to_string(invokeCalledFunctions.size()) + "个):";
        for (auto* func : invokeCalledFunctions) {
            info += " " + func->getName().str();
        }
    }

    if (!invokeNormalCalledFunctions.empty()) {
        info += "\n  Invoke方式正常处理路径调用了 (" + std::to_string(invokeNormalCalledFunctions.size()) + "个):";
        for (auto* func : invokeNormalCalledFunctions) {
            info += " " + func->getName().str();
        }
    }

    if (!invokeLandingPadCalledFunctions.empty()) {
        info += "\n  Invoke方式正常处理路径调用了 (" + std::to_string(invokeLandingPadCalledFunctions.size()) + "个):";
        for (auto* func : invokeLandingPadCalledFunctions) {
            info += " " + func->getName().str();
        }
    }

    if (!personalityCalledFunctions.empty()) {
        info += "\n  异常处理函数调用了(" + std::to_string(personalityCalledFunctions.size()) + "个):";
        for (auto* func : personalityCalledFunctions) {
            info += " " + func->getName().str();
        }
    }

    return info;
}

std::string FunctionInfo::getBriefInfo() const {
    return displayName +
           "{ EH: " + getExceptionAttributes() +
           ", 链接:" + getLinkageAbbreviation() + "(" + getLinkageString() + ")," +
           (isDeclaration ? " 声明 }" : " 定义 }");
}

/**
 * 判断指定函数的调用者是否全部在指定的组中
 *
 * @param func 要检查的函数，该函数必须在group和functionMap中
 * @param group 函数组
 * @param functionMap 函数信息映射表
 * @return true 如果func的所有调用者都在group中
 * @return false 如果func有调用者不在group中
 * @throws std::invalid_argument 如果func不在group或functionMap中
 */
bool FunctionInfo::areAllCallersInGroup(llvm::Function* func,
                         const std::unordered_set<llvm::Function*>& group,
                         const std::unordered_map<llvm::Function*, FunctionInfo>& functionMap) {
    // 参数检查
    if (group.find(func) == group.end()) {
        throw std::invalid_argument("Function must be in the group");
    }

    auto funcInfoIt = functionMap.find(func);
    if (funcInfoIt == functionMap.end()) {
        throw std::invalid_argument("Function must be in functionMap");
    }

    const auto& callerFunctions = funcInfoIt->second.callerFunctions;

    // 如果函数没有调用者，那么所有调用者（没有）都在group中
    if (callerFunctions.empty()) {
        return true;
    }

    // 获取当前函数的 isProcessed 状态
    bool currentFuncProcessed = funcInfoIt->second.isProcessed;

    // 检查每个调用者是否都在group中，并检查 isProcessed 状态
    for (llvm::Function* caller : callerFunctions) {
        // 检查调用者是否在group中
        if (group.find(caller) == group.end()) {
            return false;  // 发现一个不在group中的调用者
        }

        // 检查调用者的 isProcessed 状态
        auto callerInfoIt = functionMap.find(caller);
        if (callerInfoIt == functionMap.end()) {
            throw std::invalid_argument("Caller function must be in functionMap");
        }

        bool callerProcessed = callerInfoIt->second.isProcessed;

        // 如果一个已被处理一个未被处理，则一定不同组
        if (currentFuncProcessed != callerProcessed) {
            return false;
        }
    }

    return true;  // 所有调用者都在group中且isProcessed状态一致
}

/**
 * 判断指定函数的被调用者是否全部在指定的组中
 *
 * @param func 要检查的函数，该函数必须在group和functionMap中
 * @param group 函数组
 * @param functionMap 函数信息映射表
 * @return true 如果func的所有被调用者都在group中
 * @return false 如果func有被调用者不在group中
 * @throws std::invalid_argument 如果func不在group或functionMap中
 */
bool FunctionInfo::areAllCalledsInGroup(llvm::Function* func,
                         const std::unordered_set<llvm::Function*>& group,
                         const std::unordered_map<llvm::Function*, FunctionInfo>& functionMap) {
    // 参数检查
    if (group.find(func) == group.end()) {
        throw std::invalid_argument("Function must be in the group");
    }

    auto funcInfoIt = functionMap.find(func);
    if (funcInfoIt == functionMap.end()) {
        throw std::invalid_argument("Function must be in functionMap");
    }

    const auto& calledFunctions = funcInfoIt->second.calledFunctions;

    // 如果函数没有被调用者，那么所有被调用者（没有）都在group中
    if (calledFunctions.empty()) {
        return true;
    }

    // 获取当前函数的 isProcessed 状态
    bool currentFuncProcessed = funcInfoIt->second.isProcessed;

    // 检查每个被调用者是否都在group中，并检查 isProcessed 状态
    for (llvm::Function* called : calledFunctions) {
        // 检查被调用者是否在group中
        if (group.find(called) == group.end()) {
            return false;  // 发现一个不在group中的被调用者
        }

        // 检查被调用者的 isProcessed 状态
        auto calledInfoIt = functionMap.find(called);
        if (calledInfoIt == functionMap.end()) {
            throw std::invalid_argument("Called function must be in functionMap");
        }

        bool calledProcessed = calledInfoIt->second.isProcessed;

        // 如果一个已被处理一个未被处理，则一定不同组
        if (currentFuncProcessed != calledProcessed) {
            return false;
        }
    }

    return true;  // 所有被调用者都在group中且isProcessed状态一致
}

// 更新异常处理信息
void FunctionInfo::updateExceptionInfo() {
    if (!funcPtr) return;

    hasInvokeInst = false;
    hasLandingPad = false;
    hasResumeInst = false;
    hasCleanupPad = false;
    hasCatchPad = false;

    // 检查函数中的指令
    for (auto& BB : *funcPtr) {
        for (auto& I : BB) {
            if (llvm::isa<llvm::InvokeInst>(&I)) {
                hasInvokeInst = true;
            }
            else if (llvm::isa<llvm::LandingPadInst>(&I)) {
                hasLandingPad = true;
            }
            else if (llvm::isa<llvm::ResumeInst>(&I)) {
                hasResumeInst = true;
            }
            else if (llvm::isa<llvm::CleanupPadInst>(&I)) {
                hasCleanupPad = true;
            }
            else if (llvm::isa<llvm::CatchPadInst>(&I)) {
                hasCatchPad = true;
            }
        }
    }
}

// 判断是否为异常处理相关函数
// bool FunctionInfo::isExceptionHandler() const {
//     return hasLandingPad || hasCleanupPad || hasCatchPad ||
//            !personalityFunctions.empty() || !invokeLandingPadFunctions.empty();
// }

// 获取所有异常处理相关调用
// std::unordered_set<llvm::Function*> FunctionInfo::getAllExceptionRelatedCalls() const {
//     std::unordered_set<llvm::Function*> result;
//     result.insert(invokeCalledFunctions.begin(), invokeCalledFunctions.end());
//     result.insert(invokeLandingPadFunctions.begin(), invokeLandingPadFunctions.end());
//     result.insert(personalityFunctions.begin(), personalityFunctions.end());
//     return result;
// }

// 获取异常处理信息字符串
std::string FunctionInfo::getExceptionInfo() const {
    std::string info;

    if (hasInvokeInst) info += "HasInvoke ";
    if (hasLandingPad) info += "HasLandingPad ";
    if (hasResumeInst) info += "HasResume ";
    if (hasCleanupPad) info += "HasCleanupPad ";
    if (hasCatchPad) info += "HasCatchPad ";

    // 统计调用信息
    if (!invokeCalledFunctions.empty()) {
        info += "InvokeCalls:" + std::to_string(invokeCalledFunctions.size()) + " ";
    }

    if (!invokeLandingPadCalledFunctions.empty()) {
        info += "LandingPadCalls:" + std::to_string(invokeLandingPadCalledFunctions.size()) + " ";
    }

    if (!personalityCalledFunctions.empty()) {
        info += "Personality:" + std::to_string(personalityCalledFunctions.size()) + " ";
    }

    return info;
}

// 判断是否通过invoke调用指定函数
bool FunctionInfo::isInvokeCallTo(llvm::Function* func) const {
    return invokeCalledFunctions.find(func) != invokeCalledFunctions.end();
}

// 获取异常处理属性字符串（简略版）
std::string FunctionInfo::getExceptionAttributes() const {
    std::string attrs;

    if (hasInvokeInst) attrs += "I";
    if (hasLandingPad) attrs += "L";
    if (hasResumeInst) attrs += "R";
    if (hasCleanupPad) attrs += "C";
    if (hasCatchPad) attrs += "P";
    if (!personalityCalledFunctions.empty()) attrs += "E";  // E for personality

    return attrs.empty() ? "-" : attrs;
}

GlobalVariableInfo::GlobalVariableInfo(llvm::GlobalVariable* gv, int seqNum) {
    gvPtr = gv;
    name = gv->getName().str();
    isDeclaration = gv->isDeclaration();  // 是否是声明
    isDefinition = gv->hasExactDefinition();  // 是否是定义

    // 更新LLVM相关属性
    updateAttributesFromLLVM();

    // 检测是否为无名函数
    bool isUnnamedFunc = isUnnamed();

    if (isUnnamedFunc) {
        // 只有无名函数才分配序号
        sequenceNumber = seqNum;
        displayName = "__unnamed_" + std::to_string(seqNum);
    } else {
        // 有名函数没有序号
        sequenceNumber = -1;
        displayName = name;
    }
}

bool GlobalVariableInfo::isUnnamed() const {
    if (name.empty()) return true;

    // 检查常见的无名函数模式
    if (name.find("__unnamed_") == 0) return true;
    // 检查单字母函数名（可能是简化的内部函数）
    if (name == "d" || name == "t" || name == "b" ||
        name == "f" || name == "g" || name == "h" ||
        name == "i" || name == "j" || name == "k") {
        return true;
    }

    // 检查是否全为数字
    if (BCCommon::isNumberString(name)) return true;

    return false;
}

std::string GlobalVariableInfo::getGlobalVariableType() const {
    if (isUnnamed()) {
        return "无名全局变量 [序号:" + std::to_string(sequenceNumber) + "]";
    } else {
        return "有名全局变量";
    }
}

std::string GlobalVariableInfo::getLinkageString() const {
    return linkageString;
}

std::string GlobalVariableInfo::getLinkageAbbreviation() const {
    switch (linkage) {
        case EXTERNAL_LINKAGE: return "EXT";
        case AVAILABLE_EXTERNALLY_LINKAGE: return "AVEXT";
        case LINK_ONCE_ANY_LINKAGE: return "LOA";
        case LINK_ONCE_ODR_LINKAGE: return "LOO";
        case WEAK_ANY_LINKAGE: return "WKA";
        case WEAK_ODR_LINKAGE: return "WKO";
        case APPENDING_LINKAGE: return "APP";
        case INTERNAL_LINKAGE: return "INT";
        case PRIVATE_LINKAGE: return "PRI";
        case EXTERNAL_WEAK_LINKAGE: return "EXWK";
        case COMMON_LINKAGE: return "COM";
        default: return "UNK";
    }
}

std::string GlobalVariableInfo::getVisibilityString() const {
    if (visibility.empty()) {
        return "未知可见性";
    }
    return visibility;
}

void GlobalVariableInfo::updateAttributesFromLLVM() {
    if (!gvPtr) return;

    // 获取常量属性
    isConstant = gvPtr->isConstant();

    // 获取链接属性
    llvm::GlobalValue::LinkageTypes llvmLinkage = gvPtr->getLinkage();

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
    dsoLocal = gvPtr->isDSOLocal();

    // 获取可见性属性
    switch (gvPtr->getVisibility()) {
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

bool GlobalVariableInfo::isCompilerGenerated() const {
    // 检查是否是编译器生成的函数
    if (isUnnamed()) return true;

    // 检查常见编译器生成函数模式
    if (name.find("llvm.") == 0) return true;  // LLVM内置函数
    if (name.find("__llvm") == 0) return true; // LLVM编译器生成
    if (name.find("__clang") == 0) return true; // Clang生成
    if (name.find("__gcc") == 0) return true;   // GCC生成

    return false;
}

std::string GlobalVariableInfo::getFullInfo() const {
    return displayName +
           " [" + getGlobalVariableType() +
           ", 链接:" + getLinkageAbbreviation() + "(" + getLinkageString() + ")" +
           ", DSO本地:" + (dsoLocal ? "是" : "否") +
           ", 可见性:" + getVisibilityString() +
           ", 类型:" + (isDeclaration ? "声明" : "定义") +
           ", 常量:" + (isConstant ? "是" : "否") + "]";
           //", 组:" + (groupIndex >= 0 ? std::to_string(groupIndex) : "未分组") + "]";
}

std::string GlobalVariableInfo::getBriefInfo() const {
    return displayName +
           (isConstant ? " (常量)" : "") +
           (isDeclaration ? " (声明)" : " (定义)");
}
