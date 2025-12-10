// core.cpp
#include "core.h"
#include <algorithm>
#include <cctype>
#include "llvm/IR/GlobalValue.h"
#include <sstream>

FunctionInfo::FunctionInfo(llvm::Function* func, int seqNum) {
    funcPtr = func;
    name = func->getName().str();
    isDeclaration = func->isDeclaration();  // 是否是声明
    isDefinition = func->hasExactDefinition();  // 是否是定义

    // 更新LLVM相关属性
    updateAttributesFromLLVM();

    // 检测是否为无名函数
    bool isUnnamedFunc = isUnnamed();

    if (isUnnamedFunc) {
        // 只有无名函数才分配序号
        sequenceNumber = seqNum;
        displayName = "unnamed_" + std::to_string(seqNum) + "_" +
                     std::to_string(reinterpret_cast<uintptr_t>(func));
    } else {
        // 有名函数没有序号
        sequenceNumber = -1;
        displayName = name;
    }
}

bool FunctionInfo::isNumberString(const std::string& str) {
    if (str.empty()) return false;
    for (char c : str) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

bool FunctionInfo::isUnnamed() const {
    if (name.empty()) return true;

    // 检查常见的无名函数模式
    if (name.find("__unnamed_") == 0) return true;
    if (name.find(".") == 0) return true;  // LLVM内部函数通常以.开头
    if (name.find("__") == 0) return true; // 编译器内部函数

    // 检查单字母函数名（可能是简化的内部函数）
    if (name == "d" || name == "t" || name == "b" ||
        name == "f" || name == "g" || name == "h" ||
        name == "i" || name == "j" || name == "k") {
        return true;
    }

    // 检查是否全为数字
    if (isNumberString(name)) return true;

    // 检查是否包含特殊字符（如§等）
    if (name.find("§") != std::string::npos) return true;

    // 检查是否看起来像哈希值（16进制字符串）
    bool looksLikeHash = true;
    if (name.length() >= 8) {  // 哈希值通常较长
        for (char c : name) {
            if (!std::isxdigit(c) && c != '_') {
                looksLikeHash = false;
                break;
            }
        }
        if (looksLikeHash) return true;
    }

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

bool FunctionInfo::shouldPreserveInSplit() const {
    // 外部链接的函数通常需要保留
    if (isExternal) return true;

    // 弱链接的函数通常需要保留
    if (isWeak) return true;

    // Common链接的函数需要保留
    if (isCommon) return true;

    // 被外部调用的函数需要保留（通过调用关系判断）
    if (!callerFunctions.empty()) {
        // 如果有任何调用者，需要保留
        return true;
    }

    // 其他情况可以根据需要调整
    return false;
}

bool FunctionInfo::canBeSafelyRemoved() const {
    // 内部链接的函数如果没有调用者，可以安全删除
    if (isInternal && callerFunctions.empty()) {
        return true;
    }

    // 私有链接的函数如果没有调用者，可以安全删除
    if (linkage == PRIVATE_LINKAGE && callerFunctions.empty()) {
        return true;
    }

    // 声明（没有函数体）不能删除
    if (isDeclaration) {
        return false;
    }

    return false;
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
    return displayName +
           " [" + getFunctionType() +
           ", 入度:" + std::to_string(inDegree) +
           ", 出度:" + std::to_string(outDegree) +
           ", 总分:" + std::to_string(inDegree + outDegree) +
           ", 链接:" + getLinkageAbbreviation() + "(" + getLinkageString() + ")" +
           ", DSO本地:" + (dsoLocal ? "是" : "否") +
           ", 可见性:" + getVisibilityString() +
           ", 类型:" + (isDeclaration ? "声明" : "定义") +
           ", 组:" + (groupIndex >= 0 ? std::to_string(groupIndex) : "未分组") + "]";
}

std::string FunctionInfo::getBriefInfo() const {
    return displayName +
           " 链接:" + getLinkageAbbreviation() +
           " 可见性:" + getVisibilityString() +
           (isDeclaration ? " (声明)" : " (定义)");
}

std::string FunctionInfo::getAttributesSummary() const {
    std::string summary;
    summary += "链接属性: " + linkageString + " [" + getLinkageAbbreviation() + "]\n";
    summary += "DSO本地: " + std::string(dsoLocal ? "是" : "否") + "\n";
    summary += "可见性: " + visibility + "\n";
    summary += "函数类型: " + std::string(isDeclaration ? "声明" : "定义") + "\n";
    summary += "名称类型: " + std::string(isUnnamed() ? "无名函数" : "有名函数");
    if (isUnnamed()) {
        summary += " [序号:" + std::to_string(sequenceNumber) + "]";
    }
    summary += "\n外部链接: " + std::string(isExternal ? "是" : "否");
    summary += "\n内部链接: " + std::string(isInternal ? "是" : "否");
    summary += "\n弱链接: " + std::string(isWeak ? "是" : "否");
    summary += "\nLinkOnce链接: " + std::string(isLinkOnce ? "是" : "否");
    summary += "\nCommon链接: " + std::string(isCommon ? "是" : "否");
    summary += "\n编译器生成: " + std::string(isCompilerGenerated() ? "是" : "否");
    return summary;
}

void AttributeStats::addFunctionInfo(const FunctionInfo& funcInfo) {
    // 统计链接属性
    switch (funcInfo.linkage) {
        case EXTERNAL_LINKAGE: externalLinkage++; break;
        case AVAILABLE_EXTERNALLY_LINKAGE: availableExternallyLinkage++; break;
        case LINK_ONCE_ANY_LINKAGE: linkOnceAnyLinkage++; break;
        case LINK_ONCE_ODR_LINKAGE: linkOnceODRLinkage++; break;
        case WEAK_ANY_LINKAGE: weakAnyLinkage++; break;
        case WEAK_ODR_LINKAGE: weakODRLinkage++; break;
        case APPENDING_LINKAGE: appendingLinkage++; break;
        case INTERNAL_LINKAGE: internalLinkage++; break;
        case PRIVATE_LINKAGE: privateLinkage++; break;
        case EXTERNAL_WEAK_LINKAGE: externalWeakLinkage++; break;
        case COMMON_LINKAGE: commonLinkage++; break;
    }

    // 统计DSO本地
    if (funcInfo.dsoLocal) dsoLocalCount++;

    // 统计可见性
    if (funcInfo.visibility == "Default") defaultVisibility++;
    else if (funcInfo.visibility == "Hidden") hiddenVisibility++;
    else if (funcInfo.visibility == "Protected") protectedVisibility++;

    // 统计声明/定义
    if (funcInfo.isDeclaration) declarations++;
    if (funcInfo.isDefinition) definitions++;

    // 统计有名/无名
    if (funcInfo.isUnnamed()) unnamedFunctions++;
    else namedFunctions++;

    // 统计链接类型分组
    if (funcInfo.isExternal) externalFunctions++;
    if (funcInfo.isInternal) internalFunctions++;
    if (funcInfo.isWeak) weakFunctions++;
    if (funcInfo.isLinkOnce) linkOnceFunctions++;
}

std::string AttributeStats::getLinkageSummary() const {
    std::stringstream ss;
    ss << "链接属性详细统计:\n";
    ss << "  External链接:        " << externalLinkage << "\n";
    ss << "  AvailableExternally: " << availableExternallyLinkage << "\n";
    ss << "  LinkOnceAny:         " << linkOnceAnyLinkage << "\n";
    ss << "  LinkOnceODR:         " << linkOnceODRLinkage << "\n";
    ss << "  WeakAny:             " << weakAnyLinkage << "\n";
    ss << "  WeakODR:             " << weakODRLinkage << "\n";
    ss << "  Appending:           " << appendingLinkage << "\n";
    ss << "  Internal:            " << internalLinkage << "\n";
    ss << "  Private:             " << privateLinkage << "\n";
    ss << "  ExternalWeak:        " << externalWeakLinkage << "\n";
    ss << "  Common:              " << commonLinkage;
    return ss.str();
}

std::string AttributeStats::getSummary() const {
    std::stringstream ss;
    ss << "链接属性统计:\n";
    ss << "  外部链接函数: " << externalFunctions << "\n";
    ss << "  内部链接函数: " << internalFunctions << "\n";
    ss << "  弱链接函数:   " << weakFunctions << "\n";
    ss << "  LinkOnce函数: " << linkOnceFunctions << "\n";
    ss << "\n";

    ss << "DSO本地统计: " << dsoLocalCount << "\n";
    ss << "\n";

    ss << "可见性统计:\n";
    ss << "  Default可见性: " << defaultVisibility << "\n";
    ss << "  Hidden可见性:  " << hiddenVisibility << "\n";
    ss << "  Protected可见性: " << protectedVisibility << "\n";
    ss << "\n";

    ss << "声明/定义统计:\n";
    ss << "  声明: " << declarations << "\n";
    ss << "  定义: " << definitions << "\n";
    ss << "\n";

    ss << "函数名称统计:\n";
    ss << "  有名函数: " << namedFunctions << "\n";
    ss << "  无名函数: " << unnamedFunctions << "\n";
    ss << "\n";

    ss << "编译器相关:\n";
    ss << "  总共函数: " << (namedFunctions + unnamedFunctions);

    return ss.str();
}