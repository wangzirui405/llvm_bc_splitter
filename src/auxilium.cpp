// auxilium.cpp
#include "common.h"
#include "core.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include <algorithm>
#include <cctype>
#include <sstream>

void AttributeStats::addFunctionInfo(const FunctionInfo &funcInfo) {
    // 统计链接属性
    switch (funcInfo.linkage) {
    case EXTERNAL_LINKAGE:
        externalLinkage++;
        break;
    case AVAILABLE_EXTERNALLY_LINKAGE:
        availableExternallyLinkage++;
        break;
    case LINK_ONCE_ANY_LINKAGE:
        linkOnceAnyLinkage++;
        break;
    case LINK_ONCE_ODR_LINKAGE:
        linkOnceODRLinkage++;
        break;
    case WEAK_ANY_LINKAGE:
        weakAnyLinkage++;
        break;
    case WEAK_ODR_LINKAGE:
        weakODRLinkage++;
        break;
    case APPENDING_LINKAGE:
        appendingLinkage++;
        break;
    case INTERNAL_LINKAGE:
        internalLinkage++;
        break;
    case PRIVATE_LINKAGE:
        privateLinkage++;
        break;
    case EXTERNAL_WEAK_LINKAGE:
        externalWeakLinkage++;
        break;
    case COMMON_LINKAGE:
        commonLinkage++;
        break;
    }

    // 统计DSO本地
    if (funcInfo.dsoLocal)
        dsoLocalCount++;

    // 统计可见性
    if (funcInfo.visibility == "Default")
        defaultVisibility++;
    else if (funcInfo.visibility == "Hidden")
        hiddenVisibility++;
    else if (funcInfo.visibility == "Protected")
        protectedVisibility++;

    // 统计声明/定义
    if (funcInfo.isDeclaration)
        declarations++;
    if (funcInfo.isDefinition)
        definitions++;

    // 统计有名/无名
    if (funcInfo.isUnnamed())
        unnamedFunctions++;
    else
        namedFunctions++;

    // 统计链接类型分组
    if (funcInfo.isExternal)
        externalFunctions++;
    if (funcInfo.isInternal)
        internalFunctions++;
    if (funcInfo.isWeak)
        weakFunctions++;
    if (funcInfo.isLinkOnce)
        linkOnceFunctions++;

    if (funcInfo.isCompilerGenerated())
        compilerGenerated++;
}

void AttributeStats::addGlobalVariableInfo(const GlobalVariableInfo &globalVariableInfo) {
    // 统计链接属性
    switch (globalVariableInfo.linkage) {
    case EXTERNAL_LINKAGE:
        externalLinkage++;
        break;
    case AVAILABLE_EXTERNALLY_LINKAGE:
        availableExternallyLinkage++;
        break;
    case LINK_ONCE_ANY_LINKAGE:
        linkOnceAnyLinkage++;
        break;
    case LINK_ONCE_ODR_LINKAGE:
        linkOnceODRLinkage++;
        break;
    case WEAK_ANY_LINKAGE:
        weakAnyLinkage++;
        break;
    case WEAK_ODR_LINKAGE:
        weakODRLinkage++;
        break;
    case APPENDING_LINKAGE:
        appendingLinkage++;
        break;
    case INTERNAL_LINKAGE:
        internalLinkage++;
        break;
    case PRIVATE_LINKAGE:
        privateLinkage++;
        break;
    case EXTERNAL_WEAK_LINKAGE:
        externalWeakLinkage++;
        break;
    case COMMON_LINKAGE:
        commonLinkage++;
        break;
    }

    // 统计DSO本地
    if (globalVariableInfo.dsoLocal)
        dsoLocalCount++;

    // 统计可见性
    if (globalVariableInfo.visibility == "Default")
        defaultVisibility++;
    else if (globalVariableInfo.visibility == "Hidden")
        hiddenVisibility++;
    else if (globalVariableInfo.visibility == "Protected")
        protectedVisibility++;

    // 统计声明/定义
    if (globalVariableInfo.isDeclaration)
        declarations++;
    if (globalVariableInfo.isDefinition)
        definitions++;

    // 统计有名/无名
    if (globalVariableInfo.isUnnamed())
        unnamedGlobalVariables++;
    else
        namedGlobalVariables++;

    // 统计链接类型分组
    if (globalVariableInfo.isExternal)
        externalGlobalVariables++;
    if (globalVariableInfo.isInternal)
        internalGlobalVariables++;
    if (globalVariableInfo.isWeak)
        weakGlobalVariables++;
    if (globalVariableInfo.isLinkOnce)
        linkOnceGlobalVariables++;

    if (globalVariableInfo.isCompilerGenerated())
        compilerGenerated++;
}

std::string AttributeStats::getFunctionsLinkageSummary() const {
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

std::string AttributeStats::getGlobalVariablesLinkageSummary() const {
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

std::string AttributeStats::getFunctionsSummary() const {
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

    ss << "编译器相关:" << compilerGenerated << "\n";
    ss << "\n";

    ss << "  总共函数: " << (namedFunctions + unnamedFunctions);

    return ss.str();
}

std::string AttributeStats::getGlobalVariablesSummary() const {
    std::stringstream ss;
    ss << "链接属性统计:\n";
    ss << "  外部链接全局变量: " << externalGlobalVariables << "\n";
    ss << "  内部链接全局变量: " << internalGlobalVariables << "\n";
    ss << "  弱链接全局变量:   " << weakGlobalVariables << "\n";
    ss << "  LinkOnce全局变量: " << linkOnceGlobalVariables << "\n";
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

    ss << "全局变量名称统计:\n";
    ss << "  有名全局变量: " << namedGlobalVariables << "\n";
    ss << "  无名全局变量: " << namedGlobalVariables << "\n";
    ss << "\n";

    ss << "编译器相关:" << compilerGenerated << "\n";
    ss << "\n";

    ss << "  总共全局变量: " << (namedGlobalVariables + unnamedGlobalVariables);

    return ss.str();
}
