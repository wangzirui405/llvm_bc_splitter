// core.cpp
#include "core.h"
#include <algorithm>
#include <cctype>

FunctionInfo::FunctionInfo(llvm::Function* func, int seqNum) {
    funcPtr = func;
    name = func->getName().str();

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

std::string FunctionInfo::getFullInfo() const {
    return displayName +
           " [" + getFunctionType() +
           ", 入度:" + std::to_string(inDegree) +
           ", 出度:" + std::to_string(outDegree) +
           ", 总分:" + std::to_string(inDegree + outDegree) +
           ", 组:" + (groupIndex >= 0 ? std::to_string(groupIndex) : "未分组") + "]";
}