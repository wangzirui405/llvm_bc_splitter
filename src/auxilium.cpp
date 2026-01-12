// auxilium.cpp
#include "common.h"
#include "core.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include <algorithm>
#include <cctype>
#include <sstream>

void AttributeStats::addInfo(const GlobalValueInfo &GlobalValueInfo) {
    // 统计链接属性
    switch (GlobalValueInfo.linkage) {
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
    if (GlobalValueInfo.dsoLocal)
        dsoLocalCount++;

    // 统计可见性
    if (GlobalValueInfo.visibility == "Default")
        defaultVisibility++;
    else if (GlobalValueInfo.visibility == "Hidden")
        hiddenVisibility++;
    else if (GlobalValueInfo.visibility == "Protected")
        protectedVisibility++;

    // 统计声明/定义
    if (GlobalValueInfo.isDeclaration)
        declarations++;
    if (GlobalValueInfo.isDefinition)
        definitions++;

    // 统计有名/无名
    if (GlobalValueInfo.isUnnamed())
        unnamedGlobalValues++;
    else
        namedGlobalValues++;

    // 统计链接类型分组
    if (GlobalValueInfo.isExternal)
        externalGlobalValues++;
    if (GlobalValueInfo.isInternal)
        internalGlobalValues++;
    if (GlobalValueInfo.isWeak)
        weakGlobalValues++;
    if (GlobalValueInfo.isLinkOnce)
        linkOnceGlobalValues++;

    if (GlobalValueInfo.isCompilerGenerated())
        compilerGenerated++;
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
    ss << "  外部链接符号: " << externalGlobalValues << "\n";
    ss << "  内部链接符号: " << internalGlobalValues << "\n";
    ss << "  弱链接符号:   " << weakGlobalValues << "\n";
    ss << "  LinkOnce符号: " << linkOnceGlobalValues << "\n";
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

    ss << "名称统计:\n";
    ss << "  有名符号: " << namedGlobalValues << "\n";
    ss << "  无名符号: " << unnamedGlobalValues << "\n";
    ss << "\n";

    ss << "编译器相关:" << compilerGenerated << "\n";
    ss << "\n";

    ss << "  总计符号: " << (namedGlobalValues + unnamedGlobalValues);

    return ss.str();
}
