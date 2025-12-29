// verifier.cpp
#include "verifier.h"
#include "common.h"
#include "logging.h"
#include "core.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Verifier.h"
#include <sstream>
#include <algorithm>
#include <cctype>

BCVerifier::BCVerifier(BCCommon& commonRef) : common(commonRef) {}

std::string BCVerifier::decodeEscapeSequences(const std::string& escapedStr) {
    std::string result;
    for (size_t i = 0; i < escapedStr.length(); ) {
        // 处理转义序列的逻辑...
        // 保留原始逻辑
        result += escapedStr[i++];
    }
    return result;
}

std::string BCVerifier::getLinkageString(llvm::GlobalValue::LinkageTypes linkage) {
    switch (linkage) {
        case llvm::GlobalValue::ExternalLinkage: return "External";
        case llvm::GlobalValue::InternalLinkage: return "Internal";
        case llvm::GlobalValue::PrivateLinkage: return "Private";
        case llvm::GlobalValue::WeakAnyLinkage: return "WeakAny";
        case llvm::GlobalValue::WeakODRLinkage: return "WeakODR";
        case llvm::GlobalValue::CommonLinkage: return "Common";
        case llvm::GlobalValue::AppendingLinkage: return "Appending";
        case llvm::GlobalValue::ExternalWeakLinkage: return "ExternalWeak";
        case llvm::GlobalValue::AvailableExternallyLinkage: return "AvailableExternally";
        default: return "Unknown";
    }
}

std::string BCVerifier::getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility) {
    switch (visibility) {
        case llvm::GlobalValue::DefaultVisibility: return "Default";
        case llvm::GlobalValue::HiddenVisibility: return "Hidden";
        case llvm::GlobalValue::ProtectedVisibility: return "Protected";
        default: return "Unknown";
    }
}

// 新增：带独立日志的构建函数名映射方法
void BCVerifier::buildFunctionNameMapsWithLog(const std::unordered_set<llvm::Function*>& group,
                                std::unordered_map<std::string, llvm::Function*>& nameToFunc,
                                std::unordered_map<std::string, std::string>& escapedToOriginal,
                                std::ofstream& individualLog) {

    for (llvm::Function* F : group) {
        if (!F) continue;

        std::string originalName = F->getName().str();
        nameToFunc[originalName] = F;

        logger.logToIndividualLog(individualLog, "组内函数: " + originalName +
                        " [链接: " + getLinkageString(F->getLinkage()) +
                        ", 可见性: " + getVisibilityString(F->getVisibility()) + "]");

        // 转义序列处理逻辑...
        if (originalName.find("§") != std::string::npos) {
            std::string escapedName = originalName;
            size_t pos = 0;
            while ((pos = escapedName.find("§", pos)) != std::string::npos) {
                escapedName.replace(pos, 2, "\\C2\\A7");
                pos += 6;
            }
            escapedToOriginal[escapedName] = originalName;
            logger.logToIndividualLog(individualLog, "  转义序列映射: " + escapedName + " -> " + originalName);
        }
    }
}

// 验证函数签名的完整性
bool BCVerifier::verifyFunctionSignature(llvm::Function* F) {
    if (!F) return false;

    llvm::FunctionType* funcType = F->getFunctionType();
    if (!funcType) return false;

    llvm::Type* returnType = funcType->getReturnType();
    if (!returnType) return false;

    for (llvm::Type* paramType : funcType->params()) {
        if (!paramType) return false;
    }

    return true;
}

// 新增：简化验证方法
bool BCVerifier::quickValidateBCFile(const std::string& filename) {
    llvm::LLVMContext tempContext;
    llvm::SMDiagnostic err;
    auto testModule = parseIRFile(config.workSpace + "output/" + filename, err, tempContext);

    if (!testModule) {
        logger.logError("快速验证失败 - 无法加载: " + filename);
        return false;
    }

    std::string verifyResult;
    llvm::raw_string_ostream rso(verifyResult);
    bool moduleValid = !verifyModule(*testModule, &rso);

    if (moduleValid) {
        logger.log("✓ 快速验证通过: " + filename);
    } else {
        logger.logError("快速验证失败: " + filename);
        logger.logError("错误详情: " + rso.str());
    }

    return moduleValid;
}

// 新增：带独立日志的快速验证方法
bool BCVerifier::quickValidateBCFileWithLog(const std::string& filename, std::ofstream& individualLog) {
    logger.logToIndividualLog(individualLog, "快速验证BC文件: " + filename);

    llvm::LLVMContext tempContext;
    llvm::SMDiagnostic err;
    auto testModule = parseIRFile(config.workSpace + "output/" + filename, err, tempContext);

    if (!testModule) {
        logger.logToIndividualLog(individualLog, "错误: 无法加载BC文件进行快速验证");
        return false;
    }

    std::string verifyResult;
    llvm::raw_string_ostream rso(verifyResult);
    bool moduleValid = !verifyModule(*testModule, &rso);

    if (moduleValid) {
        logger.logToIndividualLog(individualLog, "✓ 快速验证通过");
    } else {
        logger.logToIndividualLog(individualLog, "✗ 快速验证失败");
        logger.logToIndividualLog(individualLog, "验证错误: " + rso.str());
    }

    return moduleValid;
}

// 完整的带独立日志的分析验证错误方法
// 修改 analyzeVerifierErrorsWithLog 函数中的映射构建部分
std::unordered_set<std::string> BCVerifier::analyzeVerifierErrorsWithLog(const std::string& verifyOutput,
                                                const std::unordered_set<llvm::Function*>& group,
                                                std::ofstream& individualLog) {
    std::unordered_set<std::string> functionsNeedExternal;

    logger.logToIndividualLog(individualLog, "分析verifier错误输出...");
    logger.logToIndividualLog(individualLog, "Verifier输出长度: " + std::to_string(verifyOutput.length()));

    // 构建函数名映射和序号映射（只包含无名函数）
    std::unordered_map<std::string, llvm::Function*> nameToFunc;
    std::unordered_map<int, llvm::Function*> sequenceToFunc;  // 只包含无名函数的序号映射
    std::unordered_map<std::string, std::string> escapedToOriginal;
    std::unordered_map<llvm::Function*, FunctionInfo>& functionMap = common.getFunctionMap();

    // 新增：记录所有无名函数的原始名称和序号
    std::vector<std::pair<int, std::string>> unnamedFunctions;

    // 构建映射
    for (llvm::Function* F : group) {
        if (!F) continue;

        std::string originalName = F->getName().str();
        nameToFunc[originalName] = F;

        // 只记录无名函数的序号映射
        if (functionMap.count(F) && functionMap[F].isUnnamed()) {
            int seqNum = functionMap[F].sequenceNumber;
            if (seqNum >= 0) {
                sequenceToFunc[seqNum] = F;
                // 记录无名函数信息
                unnamedFunctions.emplace_back(seqNum, originalName);
                logger.logToIndividualLog(individualLog, "无名函数序号映射: " + std::to_string(seqNum) + " -> " + originalName);
            }
        }

        // 记录函数信息
        std::string seqInfo = functionMap[F].isUnnamed() ?
                            " [序号: " + std::to_string(functionMap[F].sequenceNumber) + "]" :
                            " [有名函数]";
        logger.logToIndividualLog(individualLog, "组内函数: " + originalName +
                        seqInfo +
                        " [链接: " + functionMap[F].getLinkageString() +
                        ", 可见性: " + functionMap[F].getVisibilityString() + "]");

        // 转义序列处理
        if (originalName.find("§") != std::string::npos) {
            std::string escapedName = originalName;
            size_t pos = 0;
            while ((pos = escapedName.find("§", pos)) != std::string::npos) {
                escapedName.replace(pos, 2, "\\C2\\A7");
                pos += 6;
            }
            escapedToOriginal[escapedName] = originalName;
            logger.logToIndividualLog(individualLog, "  转义序列映射: " + escapedName + " -> " + originalName);
        }
    }

    // 输出无名函数统计信息
    if (!unnamedFunctions.empty()) {
        logger.logToIndividualLog(individualLog, "组内无名函数统计: 共 " + std::to_string(unnamedFunctions.size()) + " 个无名函数");
        for (const auto& unnamedF : unnamedFunctions) {
            logger.logToIndividualLog(individualLog, "  序号 " + std::to_string(unnamedF.first) + ": " + unnamedF.second);
        }
    }

    // 专门处理 "Global is external, but doesn't have external or weak linkage!" 错误
    std::string searchPattern = "Global is external, but doesn't have external or weak linkage!";
    size_t patternLength = searchPattern.length();
    size_t searchPos = 0;
    int errorCount = 0;
    int unnamedMatchCount = 0;  // 新增：统计通过序号匹配的无名函数数量

    while ((searchPos = verifyOutput.find(searchPattern, searchPos)) != std::string::npos) {
        errorCount++;

        // 找到错误描述后的函数指针信息
        size_t ptrPos = verifyOutput.find("ptr @", searchPos + patternLength);
        if (ptrPos != std::string::npos) {
            size_t nameStart = ptrPos + 5; // "ptr @" 的长度

            // 检查函数名是否带引号
            bool isQuoted = (nameStart < verifyOutput.length() && verifyOutput[nameStart] == '"');
            std::string extractedName;

            if (isQuoted) {
                // 处理带引号的函数名
                size_t quoteEnd = verifyOutput.find('"', nameStart + 1);
                if (quoteEnd != std::string::npos) {
                    extractedName = verifyOutput.substr(nameStart + 1, quoteEnd - nameStart - 1);
                    logger.logToIndividualLog(individualLog, "发现带引号的函数名: \"" + extractedName + "\"");
                }
            } else {
                // 处理不带引号的函数名
                size_t nameEnd = verifyOutput.find_first_of(" \n\r\t,;", nameStart);
                if (nameEnd == std::string::npos) nameEnd = verifyOutput.length();

                extractedName = verifyOutput.substr(nameStart, nameEnd - nameStart);
                logger.logToIndividualLog(individualLog, "发现不带引号的函数名: " + extractedName);
            }

            if (!extractedName.empty()) {
                bool foundMatch = false;

                // 尝试1: 直接匹配函数名
                if (nameToFunc.find(extractedName) != nameToFunc.end()) {
                    functionsNeedExternal.insert(extractedName);
                    logger.logToIndividualLog(individualLog, "直接匹配到函数 [" + std::to_string(errorCount) + "]: " + extractedName);
                    foundMatch = true;
                }

                // 尝试2: 序号匹配（只针对无名函数）
                if (!foundMatch) {
                    try {
                        int sequenceNum = std::stoi(extractedName);
                        if (sequenceToFunc.find(sequenceNum) != sequenceToFunc.end()) {
                            llvm::Function* unnamedF = sequenceToFunc[sequenceNum];
                            std::string actualName = unnamedF->getName().str();
                            functionsNeedExternal.insert(actualName);
                            unnamedMatchCount++;  // 统计无名函数匹配
                            logger.logToIndividualLog(individualLog, "通过序号匹配到无名函数 [" + std::to_string(errorCount) + "]: " +
                                            actualName + " (序号: " + extractedName + ")");
                            foundMatch = true;
                        }
                    } catch (const std::exception& e) {
                        // 不是数字，继续其他匹配方式
                    }
                }

                // 尝试3: 转义序列解码匹配
                if (!foundMatch) {
                    std::string decodedName = decodeEscapeSequences(extractedName);
                    if (decodedName != extractedName && nameToFunc.find(decodedName) != nameToFunc.end()) {
                        functionsNeedExternal.insert(decodedName);
                        logger.logToIndividualLog(individualLog, "通过转义解码匹配到函数 [" + std::to_string(errorCount) + "]: " +
                                        decodedName + " (原始: " + extractedName + ")");
                        foundMatch = true;
                    }
                }

                // 尝试4: 预定义的转义映射
                if (!foundMatch && escapedToOriginal.find(extractedName) != escapedToOriginal.end()) {
                    std::string originalName = escapedToOriginal[extractedName];
                    if (nameToFunc.find(originalName) != nameToFunc.end()) {
                        functionsNeedExternal.insert(originalName);
                        logger.logToIndividualLog(individualLog, "通过转义映射匹配到函数 [" + std::to_string(errorCount) + "]: " +
                                        originalName + " (转义: " + extractedName + ")");
                        foundMatch = true;
                    }
                }

                // 尝试5: 部分匹配（处理可能的转义序列变体）
                if (!foundMatch) {
                    for (const auto& pair : nameToFunc) {
                        const std::string& candidateName = pair.first;

                        // 检查提取的名称是否是候选名称的转义版本
                        std::string escapedCandidate = candidateName;
                        size_t pos = 0;
                        while ((pos = escapedCandidate.find("§", pos)) != std::string::npos) {
                            escapedCandidate.replace(pos, 2, "\\C2\\A7");
                            pos += 6;
                        }

                        if (extractedName == escapedCandidate) {
                            functionsNeedExternal.insert(candidateName);
                            logger.logToIndividualLog(individualLog, "通过转义转换匹配到函数 [" + std::to_string(errorCount) + "]: " +
                                            candidateName + " (转义: " + extractedName + ")");
                            foundMatch = true;
                            break;
                        }
                    }
                }

                if (!foundMatch) {
                    logger.logToIndividualLog(individualLog, "无法匹配函数: " + extractedName);

                    // 记录详细信息用于调试
                    size_t debugStart = (ptrPos > 50) ? ptrPos - 50 : 0;
                    size_t debugLength = std::min(verifyOutput.length() - debugStart, size_t(150));
                    logger.logToIndividualLog(individualLog, "  附近文本: " + verifyOutput.substr(debugStart, debugLength));

                    // 新增：如果是数字但未匹配，可能是无名函数序号超出范围
                    try {
                        int sequenceNum = std::stoi(extractedName);
                        logger.logToIndividualLog(individualLog, "  注意: 序号 " + extractedName + " 可能是无名函数，但未在组内找到对应函数");
                        logger.logToIndividualLog(individualLog, "  组内无名函数序号范围: " +
                                        (unnamedFunctions.empty() ? "无无名函数" :
                                        std::to_string(unnamedFunctions.front().first) + " - " +
                                        std::to_string(unnamedFunctions.back().first)));
                    } catch (const std::exception& e) {
                        // 不是数字，忽略
                    }
                }
            }

            // 移动到下一个可能的位置
            searchPos = ptrPos + 1;
            if (searchPos >= verifyOutput.length()) break;
        } else {
            // 没有找到ptr @，移动到下一个位置
            searchPos += patternLength;
        }
    }

    // 新增：输出匹配统计信息
    logger.logToIndividualLog(individualLog, "匹配统计:");
    logger.logToIndividualLog(individualLog, "  总错误数: " + std::to_string(errorCount));
    logger.logToIndividualLog(individualLog, "  总匹配函数数: " + std::to_string(functionsNeedExternal.size()));
    logger.logToIndividualLog(individualLog, "  通过序号匹配的无名函数数: " + std::to_string(unnamedMatchCount));
    logger.logToIndividualLog(individualLog, "  组内无名函数总数: " + std::to_string(unnamedFunctions.size()));

    // 如果仍然没有找到，但存在链接错误，标记所有组内函数
    if (functionsNeedExternal.empty() && errorCount > 0) {
        logger.logToIndividualLog(individualLog, "检测到 " + std::to_string(errorCount) + " 个链接错误但匹配失败，标记所有组内函数需要external");
        for (const auto& pair : nameToFunc) {
            functionsNeedExternal.insert(pair.first);
        }
    }

    // 原有的其他错误模式检测（作为补充）
    std::vector<std::string> supplementalPatterns = {
        "has private linkage",
        "has internal linkage",
        "visibility not default",
        "linkage not external",
        "invalid linkage",
        "undefined reference"
    };

    for (const std::string& pattern : supplementalPatterns) {
        if (verifyOutput.find(pattern) != std::string::npos) {
            logger.logToIndividualLog(individualLog, "发现补充错误模式: " + pattern);

            // 尝试匹配组内函数
            for (const auto& pair : nameToFunc) {
                const std::string& funcName = pair.first;
                if (verifyOutput.find(funcName) != std::string::npos) {
                    functionsNeedExternal.insert(funcName);
                    logger.logToIndividualLog(individualLog, "通过补充模式匹配到函数: " + funcName);
                }
            }
        }
    }

    logger.logToIndividualLog(individualLog, "分析完成，找到 " + std::to_string(functionsNeedExternal.size()) + " 个需要external的函数");

    // 输出详细信息
    if (!functionsNeedExternal.empty()) {
        logger.logToIndividualLog(individualLog, "需要external的函数列表:");
        std::unordered_map<llvm::Function*, FunctionInfo>& functionMap = common.getFunctionMap();
        for (const std::string& funcName : functionsNeedExternal) {
            llvm::Function* F = nameToFunc[funcName];
            if (F) {
                std::string seqInfo = functionMap[F].isUnnamed() ?
                                ", 序号: " + std::to_string(functionMap[F].sequenceNumber) :
                                ", 有名函数";
                logger.logToIndividualLog(individualLog, "  " + funcName +
                        " [当前链接: " + functionMap[F].getLinkageString() +
                        ", 可见性: " + functionMap[F].getVisibilityString() + seqInfo + "]");
            }
        }
    }

    return functionsNeedExternal;
}

// 新增：验证并修复BC文件的方法
// 添加独立日志支持
bool BCVerifier::verifyAndFixBCFile(const std::string& filename, const std::unordered_set<llvm::Function*>& expectedGroup) {
    // 创建独立日志文件
    std::ofstream individualLog = logger.createIndividualLogFile(filename, "_verify");

    logger.logToIndividualLog(individualLog, "开始验证并修复BC文件: " + filename, true);

    llvm::LLVMContext verifyContext;
    llvm::SMDiagnostic err;
    auto loadedModule = parseIRFile(config.workSpace + "output/" + filename, err, verifyContext);
    std::unordered_map<llvm::Function*, FunctionInfo>& functionMap = common.getFunctionMap();


    if (!loadedModule) {
        logger.logToIndividualLog(individualLog, "错误: 无法加载验证的BC文件: " + filename, true);
        err.print("BCVerifier", llvm::errs());
        individualLog.close();
        return false;
    }

    // 检查1: 验证模块完整性
    std::string verifyResult;
    llvm::raw_string_ostream rso(verifyResult);
    bool moduleValid = !verifyModule(*loadedModule, &rso);

    if (moduleValid) {
        logger.logToIndividualLog(individualLog, "✓ 模块完整性验证通过", true);

        // 检查2: 验证函数数量
        int functionCount = 0;

        for (auto& F : *loadedModule) {
            if (!F.isDeclaration()) {
                functionCount++;
            }
        }

        logger.logToIndividualLog(individualLog, "实际函数数量: " + std::to_string(functionCount));
        logger.logToIndividualLog(individualLog, "期望函数数量: " + std::to_string(expectedGroup.size()));

        if (functionCount != expectedGroup.size()) {
            logger.logToIndividualLog(individualLog, "错误: 函数数量不匹配: 期望 " +
                            std::to_string(expectedGroup.size()) + ", 实际 " + std::to_string(functionCount), true);
            individualLog.close();
            return false;
        }

        logger.logToIndividualLog(individualLog, "✓ 函数数量验证通过: " + std::to_string(functionCount) + " 个函数", true);

        // 检查3: 验证函数签名完整性
        bool allSignaturesValid = true;
        std::unordered_set<std::string> expectedNames;

        for (llvm::Function* expectedF : expectedGroup) {
            if (expectedF) {
                expectedNames.insert(expectedF->getName().str());
            }
        }

        for (auto& F : *loadedModule) {
            if (F.isDeclaration()) continue;

            std::string funcName = F.getName().str();

            if (expectedNames.find(funcName) == expectedNames.end()) {
                logger.logToIndividualLog(individualLog, "警告: 发现未预期的函数: " + funcName);
                allSignaturesValid = false;
                continue;
            }

            if (!verifyFunctionSignature(&F)) {
                logger.logToIndividualLog(individualLog, "错误: 函数签名不完整: " + funcName);
                allSignaturesValid = false;
            }
        }

        if (!allSignaturesValid) {
            logger.logToIndividualLog(individualLog, "错误: 函数签名验证失败", true);
            individualLog.close();
            return false;
        }

        logger.logToIndividualLog(individualLog, "✓ 函数签名验证通过", true);
        individualLog.close();
        return true;
    } else {
        // 模块验证失败，尝试分析错误并修复
        logger.logToIndividualLog(individualLog, "模块验证失败，尝试分析错误并修复...", true);
        logger.logToIndividualLog(individualLog, "验证错误详情: " + rso.str());

        // 特别记录无名函数信息用于调试
        logger.logToIndividualLog(individualLog, "组内无名函数信息:");
        for (llvm::Function* F : expectedGroup) {
            if (F && functionMap.count(F) && functionMap[F].isUnnamed()) {
                const FunctionInfo& info = functionMap[F];
                logger.logToIndividualLog(individualLog, "  无名函数: " + info.displayName +
                                " [序号: " + std::to_string(info.sequenceNumber) +
                                ", 实际名称: " + F->getName().str() + "]");
            }
        }

        // 分析错误信息，识别需要external链接的函数
        std::unordered_set<std::string> externalFuncNames = analyzeVerifierErrorsWithLog(rso.str(), expectedGroup, individualLog);

        if (!externalFuncNames.empty()) {
            logger.logToIndividualLog(individualLog, "发现需要修复的函数数量: " + std::to_string(externalFuncNames.size()), true);

            // 重新生成BC文件，将需要external的函数设置为external链接
            std::string fixedFilename = filename + ".fixed.bc";
            if (recreateBCFileWithExternalLinkage(expectedGroup, externalFuncNames, fixedFilename, -1)) {
                logger.logToIndividualLog(individualLog, "重新生成修复后的BC文件: " + fixedFilename, true);

                // 验证修复后的文件
                if (quickValidateBCFileWithLog(fixedFilename, individualLog)) {
                    logger.logToIndividualLog(individualLog, "✓ 修复后的BC文件验证通过", true);

                    // 替换原文件
                    llvm::sys::fs::remove(filename);
                    llvm::sys::fs::rename(fixedFilename, filename);
                    logger.logToIndividualLog(individualLog, "已替换原文件: " + filename, true);

                    individualLog.close();
                    return true;
                } else {
                    logger.logToIndividualLog(individualLog, "✗ 修复后的BC文件仍然验证失败", true);
                    individualLog.close();
                    return false;
                }
            } else {
                logger.logToIndividualLog(individualLog, "✗ 重新生成BC文件失败", true);
                individualLog.close();
                return false;
            }
        } else {
            logger.logToIndividualLog(individualLog, "无法识别需要修复的具体函数", true);
            individualLog.close();
            return false;
        }
    }
}

// 修改批量验证方法，为每个文件创建独立日志
void BCVerifier::validateAllBCFiles(const std::string& outputPrefix, bool isCloneMode) {
    logger.log("\n=== 开始批量验证所有BC文件 ===");

    int totalFiles = 0;
    int validFiles = 0;
    std::string pathPrefix = config.workSpace + "output/";

    // 检查全局变量组
    std::string globalsFilename = outputPrefix + "_group_globals.bc";
    if (llvm::sys::fs::exists(pathPrefix + globalsFilename)) {
        totalFiles++;
        std::ofstream individualLog = logger.createIndividualLogFile(globalsFilename, "_validation");
        if (quickValidateBCFileWithLog(globalsFilename, individualLog)) {
            logger.logToIndividualLog(individualLog, "✓ 快速验证通过", true);
            validFiles++;
        } else {
            logger.logToIndividualLog(individualLog, "✗ 快速验证失败", true);
        }
        individualLog.close();
    }

    // 检查外部链接函数组
    std::string externalFilename = outputPrefix + "_group_external.bc";
    if (llvm::sys::fs::exists(pathPrefix + externalFilename)) {
        totalFiles++;
        std::ofstream individualLog = logger.createIndividualLogFile(externalFilename, "_validation");
        if (isCloneMode) {
            if (quickValidateBCFile(externalFilename)) {
                logger.logToIndividualLog(individualLog, "✓ Clone模式验证通过", true);
                validFiles++;
            } else {
                logger.logToIndividualLog(individualLog, "✗ Clone模式验证失败", true);
            }
        } else {
            if (quickValidateBCFileWithLog(externalFilename, individualLog)) {
                logger.logToIndividualLog(individualLog, "✓ 快速验证通过", true);
                validFiles++;
            } else {
                logger.logToIndividualLog(individualLog, "✗ 快速验证失败", true);
            }
        }
        individualLog.close();
    }

    // 检查高入度函数组
    std::string highInDegreeFilename = outputPrefix + "_group_high_in_degree.bc";
    if (llvm::sys::fs::exists(pathPrefix + highInDegreeFilename)) {
        totalFiles++;
        std::ofstream individualLog = logger.createIndividualLogFile(highInDegreeFilename, "_validation");
        if (isCloneMode) {
            if (quickValidateBCFile(highInDegreeFilename)) {
                logger.logToIndividualLog(individualLog, "✓ Clone模式验证通过", true);
                validFiles++;
            } else {
                logger.logToIndividualLog(individualLog, "✗ Clone模式验证失败", true);
            }
        } else {
            if (quickValidateBCFileWithLog(highInDegreeFilename, individualLog)) {
                logger.logToIndividualLog(individualLog, "✓ 快速验证通过", true);
                validFiles++;
            } else {
                logger.logToIndividualLog(individualLog, "✗ 快速验证失败", true);
            }
        }
        individualLog.close();
    }

    // 检查孤立函数组
    std::string isolatedFilename = outputPrefix + "_group_isolated.bc";
    if (llvm::sys::fs::exists(pathPrefix + isolatedFilename)) {
        totalFiles++;
        std::ofstream individualLog = logger.createIndividualLogFile(isolatedFilename, "_validation");
        if (isCloneMode) {
            if (quickValidateBCFile(isolatedFilename)) {
                logger.logToIndividualLog(individualLog, "✓ Clone模式验证通过", true);
                validFiles++;
            } else {
                logger.logToIndividualLog(individualLog, "✗ Clone模式验证失败", true);
            }
        } else {
            if (quickValidateBCFileWithLog(isolatedFilename, individualLog)) {
                logger.logToIndividualLog(individualLog, "✓ 快速验证通过", true);
                validFiles++;
            } else {
                logger.logToIndividualLog(individualLog, "✗ 快速验证失败", true);
            }
        }
        individualLog.close();
    }

    // 检查新的分组范围（组4-9）
    for (int i = 4; i <= 9; i++) {
        std::string filename = outputPrefix + "_group_" + std::to_string(i) + ".bc";

        if (!llvm::sys::fs::exists(pathPrefix + filename)) {
            continue;
        }

        totalFiles++;
        std::ofstream individualLog = logger.createIndividualLogFile(filename, "_validation");

        if (isCloneMode) {
            if (quickValidateBCFile(filename)) {
                logger.logToIndividualLog(individualLog, "✓ Clone模式验证通过", true);
                validFiles++;
            } else {
                logger.logToIndividualLog(individualLog, "✗ Clone模式验证失败", true);
            }
        } else {
            if (quickValidateBCFileWithLog(filename, individualLog)) {
                logger.logToIndividualLog(individualLog, "✓ 快速验证通过", true);
                validFiles++;
            } else {
                logger.logToIndividualLog(individualLog, "✗ 快速验证失败", true);
            }
        }
        individualLog.close();
    }

    logger.log("\n=== 批量验证结果 ===");
    logger.log("总计文件: " + std::to_string(totalFiles));
    logger.log("有效文件: " + std::to_string(validFiles));
    logger.log("无效文件: " + std::to_string(totalFiles - validFiles));
    logger.log("使用模式: " + std::string(isCloneMode ? "CLONE_MODE" : "MANUAL_MODE"));

    if (validFiles == totalFiles && totalFiles > 0) {
        logger.log("✓ 所有BC文件验证通过！");
    } else if (totalFiles > 0) {
        logger.logError("✗ 部分BC文件验证失败");
    } else {
        logger.log("未找到BC文件进行验证");
    }

    // 记录验证摘要到主日志
    logger.logToFile("批量验证完成: " + std::to_string(validFiles) + "/" + std::to_string(totalFiles) + " 个文件验证通过");
}

// 修改详细分析BC文件内容方法，添加独立日志支持
void BCVerifier::analyzeBCFileContent(const std::string& filename) {
    // 创建独立日志文件
    std::ofstream individualLog = logger.createIndividualLogFile(filename, "_analysis");

    logger.logToIndividualLog(individualLog, "开始详细分析BC文件内容: " + filename, true);

    llvm::LLVMContext tempContext;
    llvm::SMDiagnostic err;
    auto testModule = parseIRFile(config.workSpace + "output/" + filename, err, tempContext);

    if (!testModule) {
        logger.logToIndividualLog(individualLog, "错误: 无法分析BC文件内容: " + filename, true);
        individualLog.close();
        return;
    }

    int totalFunctions = 0;
    int declarationFunctions = 0;
    int definitionFunctions = 0;
    int globalVariables = 0;

    // 统计全局变量
    logger.logToIndividualLog(individualLog, "全局变量列表:");
    for (auto& global : testModule->globals()) {
        globalVariables++;
        logger.logToIndividualLog(individualLog, "  " + global.getName().str() +
                        " [链接: " + getLinkageString(global.getLinkage()) + "]");
    }

    logger.logToIndividualLog(individualLog, "模块中的函数列表:");
    for (auto& F : *testModule) {
        totalFunctions++;
        std::string funcType = F.isDeclaration() ? "声明" : "定义";
        std::string linkageStr = getLinkageString(F.getLinkage());
        std::string visibilityStr = getVisibilityString(F.getVisibility());

        logger.logToIndividualLog(individualLog, "  " + F.getName().str() +
                        " [" + funcType +
                        ", 链接:" + linkageStr +
                        ", 可见性:" + visibilityStr + "]");

        if (F.isDeclaration()) {
            declarationFunctions++;
        } else {
            definitionFunctions++;
        }
    }

    logger.logToIndividualLog(individualLog, "统计结果:", true);
    logger.logToIndividualLog(individualLog, "  全局变量: " + std::to_string(globalVariables), true);
    logger.logToIndividualLog(individualLog, "  总函数数: " + std::to_string(totalFunctions), true);
    logger.logToIndividualLog(individualLog, "  声明函数: " + std::to_string(declarationFunctions), true);
    logger.logToIndividualLog(individualLog, "  定义函数: " + std::to_string(definitionFunctions), true);

    individualLog.close();
}

// 修改后的重新生成BC文件方法，使用专门的无名函数修复
bool BCVerifier::recreateBCFileWithExternalLinkage(const std::unordered_set<llvm::Function*>& group,
                                    const std::unordered_set<std::string>& externalFuncNames,
                                    const std::string& filename,
                                    int groupIndex) {
    logger.logToFile("重新生成BC文件: " + filename + " (应用external链接)");
    logger.logToFile("需要修复的函数数量: " + std::to_string(externalFuncNames.size()));

    std::unordered_map<llvm::Function*, FunctionInfo>& functionMap = common.getFunctionMap();
    llvm::Module* module = common.getModule();

    // 统计无名函数数量
    int unnamedCount = 0;
    for (llvm::Function* F : group) {
        if (F && functionMap.count(F) && functionMap[F].isUnnamed()) {
            unnamedCount++;
        }
    }
    logger.logToFile("组内无名函数数量: " + std::to_string(unnamedCount));

    llvm::LLVMContext newContext;
    auto newModule = std::make_unique<llvm::Module>(filename, newContext);

    // 复制原始模块的基本属性
    newModule->setTargetTriple(module->getTargetTriple());
    newModule->setDataLayout(module->getDataLayout());

    // 首先创建所有函数（保持原始链接属性）
    std::unordered_map<std::string, llvm::Function*> newFunctions;

    for (llvm::Function* origF : group) {
        if (!origF) continue;

        std::string funcName = origF->getName().str();

        // 在新建上下文中重新创建函数类型
        std::vector<llvm::Type*> paramTypes;
        for (const auto& arg : origF->args()) {
            llvm::Type* argType = arg.getType();

            if (argType->isIntegerTy()) {
                paramTypes.push_back(llvm::Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
            } else if (argType->isPointerTy()) {
                llvm::PointerType* ptrType = llvm::cast<llvm::PointerType>(argType);
                unsigned addressSpace = ptrType->getAddressSpace();
                paramTypes.push_back(llvm::PointerType::get(newContext, addressSpace));
            } else if (argType->isVoidTy()) {
                paramTypes.push_back(llvm::Type::getVoidTy(newContext));
            } else if (argType->isFloatTy()) {
                paramTypes.push_back(llvm::Type::getFloatTy(newContext));
            } else if (argType->isDoubleTy()) {
                paramTypes.push_back(llvm::Type::getDoubleTy(newContext));
            } else {
                paramTypes.push_back(llvm::PointerType::get(newContext, 0));
            }
        }

        // 确定返回类型
        llvm::Type* returnType = origF->getReturnType();
        llvm::Type* newReturnType;

        if (returnType->isIntegerTy()) {
            newReturnType = llvm::Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
        } else if (returnType->isPointerTy()) {
            llvm::PointerType* ptrType = llvm::cast<llvm::PointerType>(returnType);
            unsigned addressSpace = ptrType->getAddressSpace();
            newReturnType = llvm::PointerType::get(newContext, addressSpace);
        } else if (returnType->isVoidTy()) {
            newReturnType = llvm::Type::getVoidTy(newContext);
        } else if (returnType->isFloatTy()) {
            newReturnType = llvm::Type::getFloatTy(newContext);
        } else if (returnType->isDoubleTy()) {
            newReturnType = llvm::Type::getDoubleTy(newContext);
        } else {
            newReturnType = llvm::PointerType::get(newContext, 0);
        }

        llvm::FunctionType* funcType = llvm::FunctionType::get(newReturnType, paramTypes, origF->isVarArg());

        // 使用原始链接属性创建函数
        llvm::Function* newF = llvm::Function::Create(
            funcType,
            origF->getLinkage(),
            origF->getName(),
            newModule.get()
        );

        // 复制其他属性
        newF->setCallingConv(origF->getCallingConv());
        newF->setVisibility(origF->getVisibility());
        newF->setDLLStorageClass(origF->getDLLStorageClass());

        newFunctions[funcName] = newF;

        // 记录函数类型信息
        std::string funcTypeInfo = functionMap[origF].isUnnamed() ?
                            "无名函数 [序号: " + std::to_string(functionMap[origF].sequenceNumber) + "]" :
                            "有名函数";
        logger.logToFile("创建" + funcTypeInfo + ": " + funcName +
                " [链接: " + functionMap[origF].getLinkageString() +
                ", 可见性: " + functionMap[origF].getVisibilityString() + "]");
    }

    // 使用专门的无名函数修复方法
    batchFixFunctionLinkageWithUnnamedSupport(*newModule, externalFuncNames);

    return common.writeBitcodeSafely(*newModule, filename);
}

// 新增：专门处理无名函数的修复方法
void BCVerifier::batchFixFunctionLinkageWithUnnamedSupport(llvm::Module& M, const std::unordered_set<std::string>& externalFuncNames) {
    logger.logToFile("批量修复函数链接属性（支持无名函数）...");

    int fixedCount = 0;
    int unnamedFixedCount = 0;  // 统计修复的无名函数数量

    for (auto& F : M) {
        std::string funcName = F.getName().str();

        if (externalFuncNames.find(funcName) != externalFuncNames.end()) {
            llvm::GlobalValue::LinkageTypes oldLinkage = F.getLinkage();

            // 只有当当前链接不是external时才修改
            if (oldLinkage != llvm::GlobalValue::ExternalLinkage) {
                F.setLinkage(llvm::GlobalValue::ExternalLinkage);
                F.setVisibility(llvm::GlobalValue::DefaultVisibility);

                FunctionInfo tempInfo(&F);  // 临时对象用于判断是否为无名函数
                // 检查是否为无名函数
                bool isUnnamed = tempInfo.isUnnamed();

                std::string funcType = isUnnamed ? "无名函数" : "有名函数";
                if (isUnnamed) {
                    unnamedFixedCount++;
                }

                logger.logToFile("修复" + funcType + ": " + funcName +
                        " [链接: " + getLinkageString(oldLinkage) +
                        " -> " + getLinkageString(F.getLinkage()) + "]");
                fixedCount++;
            }
        }
    }

    logger.logToFile("批量修复完成，共修复 " + std::to_string(fixedCount) + " 个函数的链接属性");
    logger.logToFile("其中无名函数: " + std::to_string(unnamedFixedCount) + " 个");
}