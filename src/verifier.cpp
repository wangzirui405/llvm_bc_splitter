// verifier.cpp
#include "verifier.h"
#include "common.h"
#include "core.h"
#include "logging.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cctype>
#include <sstream>

BCVerifier::BCVerifier(BCCommon &commonRef) : common(commonRef) {}

std::string BCVerifier::decodeEscapeSequences(llvm::StringRef escapedStr) {
    std::string result;
    for (size_t i = 0; i < escapedStr.size();) {
        // 处理转义序列的逻辑...
        // 保留原始逻辑
        result += escapedStr[i++];
    }
    return result;
}

std::string BCVerifier::getLinkageString(llvm::GlobalValue::LinkageTypes linkage) {
    switch (linkage) {
    case llvm::GlobalValue::ExternalLinkage:
        return "External";
    case llvm::GlobalValue::InternalLinkage:
        return "Internal";
    case llvm::GlobalValue::PrivateLinkage:
        return "Private";
    case llvm::GlobalValue::WeakAnyLinkage:
        return "WeakAny";
    case llvm::GlobalValue::WeakODRLinkage:
        return "WeakODR";
    case llvm::GlobalValue::CommonLinkage:
        return "Common";
    case llvm::GlobalValue::AppendingLinkage:
        return "Appending";
    case llvm::GlobalValue::ExternalWeakLinkage:
        return "ExternalWeak";
    case llvm::GlobalValue::AvailableExternallyLinkage:
        return "AvailableExternally";
    default:
        return "Unknown";
    }
}

std::string BCVerifier::getVisibilityString(llvm::GlobalValue::VisibilityTypes visibility) {
    switch (visibility) {
    case llvm::GlobalValue::DefaultVisibility:
        return "Default";
    case llvm::GlobalValue::HiddenVisibility:
        return "Hidden";
    case llvm::GlobalValue::ProtectedVisibility:
        return "Protected";
    default:
        return "Unknown";
    }
}

// 新增：带独立日志的构建符号名映射方法
void BCVerifier::buildGlobalValueNameMapsWithLog(const llvm::DenseSet<llvm::GlobalValue *> &group,
                                                 llvm::StringMap<llvm::GlobalValue *> &nameToGV,
                                                 llvm::StringMap<std::string> &escapedToOriginal,
                                                 std::ofstream &individualLog) {

    for (llvm::GlobalValue *GV : group) {
        if (!GV)
            continue;

        std::string originalName = GV->getName().str();
        nameToGV[originalName] = GV;

        logger.logToIndividualLog(individualLog, "组内符号: " + originalName +
                                                     " [链接: " + getLinkageString(GV->getLinkage()) +
                                                     ", 可见性: " + getVisibilityString(GV->getVisibility()) + "]");

        // 转义序列处理逻辑...
        if (originalName.find("§") != llvm::StringRef::npos) {
            std::string escapedName = originalName;
            size_t pos = 0;
            while ((pos = escapedName.find("§", pos)) != llvm::StringRef::npos) {
                escapedName.replace(pos, 2, "\\C2\\A7");
                pos += 6;
            }
            escapedToOriginal[escapedName] = originalName;
            logger.logToIndividualLog(individualLog, "  转义序列映射: " + escapedName + " -> " + originalName);
        }
    }
}

// 验证函数签名的完整性
bool BCVerifier::verifyFunctionSignature(llvm::Function *F) {
    if (!F)
        return false;

    llvm::FunctionType *funcType = F->getFunctionType();
    if (!funcType)
        return false;

    llvm::Type *returnType = funcType->getReturnType();
    if (!returnType)
        return false;

    for (llvm::Type *paramType : funcType->params()) {
        if (!paramType)
            return false;
    }

    return true;
}

// 新增：简化验证方法
bool BCVerifier::quickValidateBCFile(llvm::StringRef filename) {
    llvm::LLVMContext tempContext;
    llvm::SMDiagnostic err;
    auto testModule = parseIRFile(config.workSpace + "output/" + filename.str(), err, tempContext);

    if (!testModule) {
        logger.logError("快速验证失败 - 无法加载: " + filename.str());
        return false;
    }

    std::string verifyResult;
    llvm::raw_string_ostream rso(verifyResult);
    bool moduleValid = !verifyModule(*testModule, &rso);

    if (moduleValid) {
        logger.log("✓ 快速验证通过: " + filename.str());
    } else {
        logger.logError("快速验证失败: " + filename.str());
        logger.logError("错误详情: " + rso.str());
    }

    return moduleValid;
}

// 新增：带独立日志的快速验证方法
bool BCVerifier::quickValidateBCFileWithLog(llvm::StringRef filename, std::ofstream &individualLog) {
    logger.logToIndividualLog(individualLog, "快速验证BC文件: " + filename.str());

    llvm::LLVMContext tempContext;
    llvm::SMDiagnostic err;
    auto testModule = parseIRFile(config.workSpace + "output/" + filename.str(), err, tempContext);

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
// 修改 analyzeVerifierErrorsWithLog 符号中的映射构建部分
llvm::StringSet<> BCVerifier::analyzeVerifierErrorsWithLog(llvm::StringRef verifyOutput,
                                                           const llvm::DenseSet<llvm::GlobalValue *> &group,
                                                           std::ofstream &individualLog) {
    llvm::StringSet<> globalValuesNeedExternal;

    logger.logToIndividualLog(individualLog, "分析verifier错误输出...");
    logger.logToIndividualLog(individualLog, "Verifier输出长度: " + std::to_string(verifyOutput.size()));

    // 构建符号名映射和序号映射（只包含无名符号）
    llvm::StringMap<llvm::GlobalValue *> nameToGV;
    llvm::DenseMap<int, llvm::GlobalValue *> sequenceToGV; // 只包含无名符号的序号映射
    llvm::StringMap<std::string> escapedToOriginal;
    auto &globalValueMap = common.getGlobalValueMap();

    // 新增：记录所有无名符号的原始名称和序号
    std::vector<std::pair<int, std::string>> unnamedGlobalValues;

    // 构建映射
    for (llvm::GlobalValue *GV : group) {
        if (!GV)
            continue;

        std::string originalName = GV->getName().str();
        nameToGV[originalName] = GV;

        // 只记录无名符号的序号映射
        if (globalValueMap.count(GV) && globalValueMap[GV].isUnnamed()) {
            int seqNum = globalValueMap[GV].sequenceNumber;
            if (seqNum >= 0) {
                sequenceToGV[seqNum] = GV;
                // 记录无名符号信息
                unnamedGlobalValues.emplace_back(seqNum, originalName);
                logger.logToIndividualLog(individualLog,
                                          "无名符号序号映射: " + std::to_string(seqNum) + " -> " + originalName);
            }
        }

        // 记录符号信息
        std::string seqInfo = globalValueMap[GV].isUnnamed()
                                  ? " [序号: " + std::to_string(globalValueMap[GV].sequenceNumber) + "]"
                                  : " [有名符号]";
        logger.logToIndividualLog(individualLog, "组内符号: " + originalName + seqInfo +
                                                     " [链接: " + globalValueMap[GV].getLinkageString() +
                                                     ", 可见性: " + globalValueMap[GV].getVisibilityString() + "]");

        // 转义序列处理
        if (originalName.find("§") != llvm::StringRef::npos) {
            std::string escapedName = originalName;
            size_t pos = 0;
            while ((pos = escapedName.find("§", pos)) != llvm::StringRef::npos) {
                escapedName.replace(pos, 2, "\\C2\\A7");
                pos += 6;
            }
            escapedToOriginal[escapedName] = originalName;
            logger.logToIndividualLog(individualLog, "  转义序列映射: " + escapedName + " -> " + originalName);
        }
    }

    // 输出无名符号统计信息
    if (!unnamedGlobalValues.empty()) {
        logger.logToIndividualLog(individualLog,
                                  "组内无名符号统计: 共 " + std::to_string(unnamedGlobalValues.size()) + " 个无名符号");
        for (const auto &unnamed : unnamedGlobalValues) {
            logger.logToIndividualLog(individualLog, "  序号 " + std::to_string(unnamed.first) + ": " + unnamed.second);
        }
    }

    // 专门处理 "Global is external, but doesn't have external or weak linkage!" 错误
    std::string searchPattern = "Global is external, but doesn't have external or weak linkage!";
    size_t patternLength = searchPattern.length();
    size_t searchPos = 0;
    int errorCount = 0;
    int unnamedMatchCount = 0; // 新增：统计通过序号匹配的无名符号数量

    while ((searchPos = verifyOutput.find(searchPattern, searchPos)) != llvm::StringRef::npos) {
        errorCount++;

        // 找到错误描述后的符号指针信息
        size_t ptrPos = verifyOutput.find("ptr @", searchPos + patternLength);
        if (ptrPos != llvm::StringRef::npos) {
            size_t nameStart = ptrPos + 5; // "ptr @" 的长度

            // 检查符号名是否带引号
            bool isQuoted = (nameStart < verifyOutput.size() && verifyOutput[nameStart] == '"');
            std::string extractedName;

            if (isQuoted) {
                // 处理带引号的符号名
                size_t quoteEnd = verifyOutput.find('"', nameStart + 1);
                if (quoteEnd != llvm::StringRef::npos) {
                    extractedName = verifyOutput.substr(nameStart + 1, quoteEnd - nameStart - 1);
                    logger.logToIndividualLog(individualLog, "发现带引号的符号名: \"" + extractedName + "\"");
                }
            } else {
                // 处理不带引号的符号名
                size_t nameEnd = verifyOutput.find_first_of(" \n\r\t,;", nameStart);
                if (nameEnd == llvm::StringRef::npos)
                    nameEnd = verifyOutput.size();

                extractedName = verifyOutput.substr(nameStart, nameEnd - nameStart);
                logger.logToIndividualLog(individualLog, "发现不带引号的符号名: " + extractedName);
            }

            if (!extractedName.empty()) {
                bool foundMatch = false;

                // 尝试1: 直接匹配符号名
                if (nameToGV.find(extractedName) != nameToGV.end()) {
                    globalValuesNeedExternal.insert(extractedName);
                    logger.logToIndividualLog(individualLog,
                                              "直接匹配到符号 [" + std::to_string(errorCount) + "]: " + extractedName);
                    foundMatch = true;
                }

                // 尝试2: 序号匹配（只针对无名符号）
                if (!foundMatch) {
                    try {
                        int sequenceNum = std::stoi(extractedName);
                        if (sequenceToGV.find(sequenceNum) != sequenceToGV.end()) {
                            llvm::GlobalValue *unnamedF = sequenceToGV[sequenceNum];
                            std::string actualName = unnamedF->getName().str();
                            globalValuesNeedExternal.insert(actualName);
                            unnamedMatchCount++; // 统计无名符号匹配
                            logger.logToIndividualLog(individualLog,
                                                      "通过序号匹配到无名符号 [" + std::to_string(errorCount) +
                                                          "]: " + actualName + " (序号: " + extractedName + ")");
                            foundMatch = true;
                        }
                    } catch (const std::exception &e) {
                        // 不是数字，继续其他匹配方式
                    }
                }

                // 尝试3: 转义序列解码匹配
                if (!foundMatch) {
                    std::string decodedName = decodeEscapeSequences(extractedName);
                    if (decodedName != extractedName && nameToGV.find(decodedName) != nameToGV.end()) {
                        globalValuesNeedExternal.insert(decodedName);
                        logger.logToIndividualLog(individualLog, "通过转义解码匹配到符号 [" +
                                                                     std::to_string(errorCount) + "]: " + decodedName +
                                                                     " (原始: " + extractedName + ")");
                        foundMatch = true;
                    }
                }

                // 尝试4: 预定义的转义映射
                if (!foundMatch && escapedToOriginal.find(extractedName) != escapedToOriginal.end()) {
                    std::string originalName = escapedToOriginal[extractedName];
                    if (nameToGV.find(originalName) != nameToGV.end()) {
                        globalValuesNeedExternal.insert(originalName);
                        logger.logToIndividualLog(individualLog, "通过转义映射匹配到符号 [" +
                                                                     std::to_string(errorCount) + "]: " + originalName +
                                                                     " (转义: " + extractedName + ")");
                        foundMatch = true;
                    }
                }

                // 尝试5: 部分匹配（处理可能的转义序列变体）
                if (!foundMatch) {
                    for (const auto &entry : nameToGV) {
                        llvm::StringRef candidateName = entry.getKey();

                        // 检查提取的名称是否是候选名称的转义版本
                        std::string escapedCandidate = candidateName.str();
                        size_t pos = 0;
                        while ((pos = escapedCandidate.find("§", pos)) != llvm::StringRef::npos) {
                            escapedCandidate.replace(pos, 2, "\\C2\\A7");
                            pos += 6;
                        }

                        if (extractedName == escapedCandidate) {
                            globalValuesNeedExternal.insert(candidateName.str());
                            logger.logToIndividualLog(
                                individualLog, "通过转义转换匹配到符号 [" + std::to_string(errorCount) +
                                                   "]: " + candidateName.str() + " (转义: " + extractedName + ")");
                            foundMatch = true;
                            break;
                        }
                    }
                }

                if (!foundMatch) {
                    logger.logToIndividualLog(individualLog, "无法匹配符号: " + extractedName);

                    // 记录详细信息用于调试
                    size_t debugStart = (ptrPos > 50) ? ptrPos - 50 : 0;
                    size_t debugLength = std::min(verifyOutput.size() - debugStart, size_t(150));
                    logger.logToIndividualLog(individualLog,
                                              "  附近文本: " + verifyOutput.str().substr(debugStart, debugLength));

                    // 新增：如果是数字但未匹配，可能是无名符号序号超出范围
                    try {
                        int sequenceNum = std::stoi(extractedName);
                        logger.logToIndividualLog(individualLog, "  注意: 序号 " + extractedName +
                                                                     " 可能是无名符号，但未在组内找到对应符号");
                        logger.logToIndividualLog(individualLog,
                                                  "  组内无名符号序号范围: " +
                                                      (unnamedGlobalValues.empty()
                                                           ? "无无名符号"
                                                           : std::to_string(unnamedGlobalValues.front().first) + " - " +
                                                                 std::to_string(unnamedGlobalValues.back().first)));
                    } catch (const std::exception &e) {
                        // 不是数字，忽略
                    }
                }
            }

            // 移动到下一个可能的位置
            searchPos = ptrPos + 1;
            if (searchPos >= verifyOutput.size())
                break;
        } else {
            // 没有找到ptr @，移动到下一个位置
            searchPos += patternLength;
        }
    }

    // 新增：输出匹配统计信息
    logger.logToIndividualLog(individualLog, "匹配统计:");
    logger.logToIndividualLog(individualLog, "  总错误数: " + std::to_string(errorCount));
    logger.logToIndividualLog(individualLog, "  总匹配符号数: " + std::to_string(globalValuesNeedExternal.size()));
    logger.logToIndividualLog(individualLog, "  通过序号匹配的无名符号数: " + std::to_string(unnamedMatchCount));
    logger.logToIndividualLog(individualLog, "  组内无名符号总数: " + std::to_string(unnamedGlobalValues.size()));

    // 如果仍然没有找到，但存在链接错误，标记所有组内符号
    if (globalValuesNeedExternal.empty() && errorCount > 0) {
        logger.logToIndividualLog(individualLog, "检测到 " + std::to_string(errorCount) +
                                                     " 个链接错误但匹配失败，标记所有组内符号需要external");
        for (const auto &entry : nameToGV) {
            globalValuesNeedExternal.insert(entry);
        }
    }

    // 原有的其他错误模式检测（作为补充）
    llvm::StringSet<> supplementalPatterns = {"has private linkage",  "has internal linkage", "visibility not default",
                                              "linkage not external", "invalid linkage",      "undefined reference"};

    for (const auto &entry : supplementalPatterns) {
        llvm::StringRef pattern = entry.getKey();
        if (verifyOutput.find(pattern) != llvm::StringRef::npos) {
            logger.logToIndividualLog(individualLog, "发现补充错误模式: " + std::to_string(pattern.size()));

            // 尝试匹配组内符号
            for (const auto &entry : nameToGV) {
                llvm::StringRef funcName = entry.getKey();
                if (verifyOutput.find(funcName) != llvm::StringRef::npos) {
                    globalValuesNeedExternal.insert(funcName.str());
                    logger.logToIndividualLog(individualLog, "通过补充模式匹配到符号: " + funcName.str());
                }
            }
        }
    }

    logger.logToIndividualLog(individualLog, "分析完成，找到 " + std::to_string(globalValuesNeedExternal.size()) +
                                                 " 个需要external的符号");

    // 输出详细信息
    if (!globalValuesNeedExternal.empty()) {
        logger.logToIndividualLog(individualLog, "需要external的符号列表:");
        auto &globalValueMap = common.getGlobalValueMap();
        for (const auto &entry : globalValuesNeedExternal) {
            llvm::StringRef GVNameRef = entry.getKey();
            llvm::GlobalValue *GV = nameToGV[GVNameRef.str()];
            if (GV) {
                std::string seqInfo = globalValueMap[GV].isUnnamed()
                                          ? ", 序号: " + std::to_string(globalValueMap[GV].sequenceNumber)
                                          : ", 有名符号";
                logger.logToIndividualLog(
                    individualLog, "  " + GVNameRef.str() + " [当前链接: " + globalValueMap[GV].getLinkageString() +
                                       ", 可见性: " + globalValueMap[GV].getVisibilityString() + seqInfo + "]");
            }
        }
    }

    return globalValuesNeedExternal;
}

// 新增：验证并修复BC文件的方法
// 添加独立日志支持
bool BCVerifier::verifyAndFixBCFile(llvm::StringRef filename,
                                    const llvm::DenseSet<llvm::GlobalValue *> &expectedGroup) {
    // 创建独立日志文件
    std::ofstream individualLog = logger.createIndividualLogFile(filename, "_verify");

    logger.logToIndividualLog(individualLog, "开始验证并修复BC文件: " + filename.str(), true);

    llvm::LLVMContext verifyContext;
    llvm::SMDiagnostic err;
    auto loadedModule = parseIRFile(config.workSpace + "output/" + filename.str(), err, verifyContext);
    auto &globalValueMap = common.getGlobalValueMap();

    if (!loadedModule) {
        logger.logToIndividualLog(individualLog, "错误: 无法加载验证的BC文件: " + filename.str(), true);
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

        // 检查2: 验证符号数量
        int functionCount = 0;

        for (auto &F : *loadedModule) {
            if (!F.isDeclaration()) {
                functionCount++;
            }
        }

        logger.logToIndividualLog(individualLog, "实际符号数量: " + std::to_string(functionCount));
        logger.logToIndividualLog(individualLog, "期望符号数量: " + std::to_string(expectedGroup.size()));

        if (functionCount != expectedGroup.size()) {
            logger.logToIndividualLog(individualLog,
                                      "错误: 符号数量不匹配: 期望 " + std::to_string(expectedGroup.size()) + ", 实际 " +
                                          std::to_string(functionCount),
                                      true);
            individualLog.close();
            return false;
        }

        logger.logToIndividualLog(individualLog, "✓ 符号数量验证通过: " + std::to_string(functionCount) + " 个符号",
                                  true);

        // 检查3: 验证符号签名完整性
        bool allSignaturesValid = true;
        llvm::StringSet<> expectedNames;

        for (llvm::GlobalValue *expected : expectedGroup) {
            if (expected) {
                expectedNames.insert(expected->getName().str());
            }
        }

        for (auto &F : *loadedModule) {
            if (F.isDeclaration())
                continue;

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

        // 特别记录无名符号信息用于调试
        logger.logToIndividualLog(individualLog, "组内无名符号信息:");
        for (llvm::GlobalValue *GV : expectedGroup) {
            if (GV && globalValueMap.count(GV) && globalValueMap[GV].isUnnamed()) {
                const GlobalValueInfo &info = globalValueMap[GV];
                logger.logToIndividualLog(individualLog, "  无名符号: " + info.displayName +
                                                             " [序号: " + std::to_string(info.sequenceNumber) +
                                                             ", 实际名称: " + GV->getName().str() + "]");
            }
        }

        // 分析错误信息，识别需要external链接的符号
        llvm::StringSet<> externalGVNames = analyzeVerifierErrorsWithLog(rso.str(), expectedGroup, individualLog);

        if (!externalGVNames.empty()) {
            logger.logToIndividualLog(individualLog,
                                      "发现需要修复的符号数量: " + std::to_string(externalGVNames.size()), true);

            // 重新生成BC文件，将需要external的符号设置为external链接
            std::string fixedFilename = filename.str() + ".fixed.bc";
            if (recreateBCFileWithExternalLinkage(expectedGroup, externalGVNames, fixedFilename, -1)) {
                logger.logToIndividualLog(individualLog, "重新生成修复后的BC文件: " + fixedFilename, true);

                // 验证修复后的文件
                if (quickValidateBCFileWithLog(fixedFilename, individualLog)) {
                    logger.logToIndividualLog(individualLog, "✓ 修复后的BC文件验证通过", true);

                    // 替换原文件
                    llvm::sys::fs::remove(filename);
                    llvm::sys::fs::rename(fixedFilename, filename);
                    logger.logToIndividualLog(individualLog, "已替换原文件: " + filename.str(), true);

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
            logger.logToIndividualLog(individualLog, "无法识别需要修复的具体符号", true);
            individualLog.close();
            return false;
        }
    }
}

// 修改批量验证方法，为每个文件创建独立日志
void BCVerifier::validateAllBCFiles(llvm::StringRef outputPrefix, bool isCloneMode) {
    logger.log("\n=== 开始批量验证所有BC文件 ===");

    int totalFiles = 0;
    int validFiles = 0;
    auto &globalValuesAllGroups = common.getGlobalValuesAllGroups();
    std::string pathPrefix = config.workSpace + "output/";

    // 检查
    for (int i = 0; i < globalValuesAllGroups.size(); i++) {
        if (globalValuesAllGroups[i].empty())
            continue;

        std::string filename =
            outputPrefix.str() + (totalFiles == 0 ? "_publicGroup.bc" : "_group_" + std::to_string(totalFiles) + ".bc");

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
    logger.logToFile("批量验证完成: " + std::to_string(validFiles) + "/" + std::to_string(totalFiles) +
                     " 个文件验证通过");
}

// 修改详细分析BC文件内容方法，添加独立日志支持
void BCVerifier::analyzeBCFileContent(llvm::StringRef filename) {
    // 创建独立日志文件
    std::ofstream individualLog = logger.createIndividualLogFile(filename, "_analysis");

    logger.logToIndividualLog(individualLog, "开始详细分析BC文件内容: " + filename.str(), true);

    llvm::LLVMContext tempContext;
    llvm::SMDiagnostic err;
    auto testModule = parseIRFile(config.workSpace + "output/" + filename.str(), err, tempContext);

    if (!testModule) {
        logger.logToIndividualLog(individualLog, "错误: 无法分析BC文件内容: " + filename.str(), true);
        individualLog.close();
        return;
    }

    int totalGlobalValues = 0;
    int declarationGlobalValues = 0;
    int definitionGlobalValues = 0;
    int globalVariables = 0;

    // 统计全局变量
    logger.logToIndividualLog(individualLog, "全局变量列表:");
    for (auto &global : testModule->globals()) {
        globalVariables++;
        logger.logToIndividualLog(individualLog, "  " + global.getName().str() +
                                                     " [链接: " + getLinkageString(global.getLinkage()) + "]");
    }

    logger.logToIndividualLog(individualLog, "模块中的符号列表:");
    for (auto &F : *testModule) {
        totalGlobalValues++;
        std::string funcType = F.isDeclaration() ? "声明" : "定义";
        std::string linkageStr = getLinkageString(F.getLinkage());
        std::string visibilityStr = getVisibilityString(F.getVisibility());

        logger.logToIndividualLog(individualLog, "  " + F.getName().str() + " [" + funcType + ", 链接:" + linkageStr +
                                                     ", 可见性:" + visibilityStr + "]");

        if (F.isDeclaration()) {
            declarationGlobalValues++;
        } else {
            definitionGlobalValues++;
        }
    }

    logger.logToIndividualLog(individualLog, "统计结果:", true);
    logger.logToIndividualLog(individualLog, "  全局变量: " + std::to_string(globalVariables), true);
    logger.logToIndividualLog(individualLog, "  总符号数: " + std::to_string(totalGlobalValues), true);
    logger.logToIndividualLog(individualLog, "  声明符号: " + std::to_string(declarationGlobalValues), true);
    logger.logToIndividualLog(individualLog, "  定义符号: " + std::to_string(definitionGlobalValues), true);

    individualLog.close();
}

// 修改后的重新生成BC文件方法，使用专门的无名符号修复
bool BCVerifier::recreateBCFileWithExternalLinkage(const llvm::DenseSet<llvm::GlobalValue *> &group,
                                                   const llvm::StringSet<> &externalGVNames, llvm::StringRef filename,
                                                   int groupIndex) {
    logger.logToFile("重新生成BC文件: " + filename.str() + " (应用external链接)");
    logger.logToFile("需要修复的符号数量: " + std::to_string(externalGVNames.size()));

    auto &globalValueMap = common.getGlobalValueMap();
    llvm::Module *M = common.getModule();

    // 统计无名符号数量
    int unnamedCount = 0;
    for (llvm::GlobalValue *GV : group) {
        if (GV && globalValueMap.count(GV) && globalValueMap[GV].isUnnamed()) {
            unnamedCount++;
        }
    }
    logger.logToFile("组内无名符号数量: " + std::to_string(unnamedCount));

    llvm::LLVMContext newContext;
    auto newM = std::make_unique<llvm::Module>(filename, newContext);

    // 复制原始模块的基本属性
    newM->setTargetTriple(M->getTargetTriple());
    newM->setDataLayout(M->getDataLayout());

    // 首先创建所有符号（保持原始链接属性）

    for (llvm::GlobalValue *orig : group) {
        if (!orig)
            continue;

        if (auto *origF = llvm::dyn_cast<llvm::Function>(orig)) {
            std::string funcName = origF->getName().str();

            // 在新建上下文中重新创建符号类型
            std::vector<llvm::Type *> paramTypes;
            for (const auto &arg : origF->args()) {
                llvm::Type *argType = arg.getType();

                if (argType->isIntegerTy()) {
                    paramTypes.push_back(llvm::Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
                } else if (argType->isPointerTy()) {
                    llvm::PointerType *ptrType = llvm::cast<llvm::PointerType>(argType);
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
            llvm::Type *returnType = origF->getReturnType();
            llvm::Type *newReturnType;

            if (returnType->isIntegerTy()) {
                newReturnType = llvm::Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
            } else if (returnType->isPointerTy()) {
                llvm::PointerType *ptrType = llvm::cast<llvm::PointerType>(returnType);
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

            llvm::FunctionType *funcType = llvm::FunctionType::get(newReturnType, paramTypes, origF->isVarArg());

            // 使用原始链接属性创建符号
            llvm::Function *newF = llvm::Function::Create(funcType, origF->getLinkage(), origF->getName(), newM.get());

            // 复制其他属性
            newF->setCallingConv(origF->getCallingConv());
            newF->setVisibility(origF->getVisibility());
            newF->setDLLStorageClass(origF->getDLLStorageClass());

            // 记录符号类型信息
            std::string funcTypeInfo =
                globalValueMap[origF].isUnnamed()
                    ? "无名符号 [序号: " + std::to_string(globalValueMap[origF].sequenceNumber) + "]"
                    : "有名符号";
            logger.logToFile("创建" + funcTypeInfo + ": " + funcName +
                             " [链接: " + globalValueMap[origF].getLinkageString() +
                             ", 可见性: " + globalValueMap[origF].getVisibilityString() + "]");

        } else if (auto *origGV = llvm::dyn_cast<llvm::GlobalVariable>(orig)) {
            llvm::GlobalVariable *newGlobal = new llvm::GlobalVariable(
                *newM, origGV->getValueType(), origGV->isConstant(), origGV->getLinkage(), origGV->getInitializer(),
                origGV->getName(), nullptr, llvm::GlobalVariable::NotThreadLocal, origGV->getAddressSpace());

            // 设置可见性
            newGlobal->setVisibility(origGV->getVisibility());

            logger.logToFile("复制全局变量声明: " + newGlobal->getName().str());
        }
    }

    // 使用专门的无名符号修复方法
    batchFixGlobalValueLinkageWithUnnamedSupport(*newM, externalGVNames);

    return common.writeBitcodeSafely(*newM, filename);
}

// 新增：专门处理无名符号的修复方法
void BCVerifier::batchFixGlobalValueLinkageWithUnnamedSupport(llvm::Module &M,
                                                              const llvm::StringSet<> &externalGVNames) {
    logger.logToFile("批量修复符号链接属性（支持无名符号）...");

    int fixedCount = 0;
    int unnamedFixedCount = 0; // 统计修复的无名符号数量

    for (auto &F : M) {
        std::string funcName = F.getName().str();

        if (externalGVNames.find(funcName) != externalGVNames.end()) {
            llvm::GlobalValue::LinkageTypes oldLinkage = F.getLinkage();

            // 只有当当前链接不是external时才修改
            if (oldLinkage != llvm::GlobalValue::ExternalLinkage) {
                F.setLinkage(llvm::GlobalValue::ExternalLinkage);
                F.setVisibility(llvm::GlobalValue::DefaultVisibility);

                GlobalValueInfo tempInfo(&F); // 临时对象用于判断是否为无名符号
                // 检查是否为无名符号
                bool isUnnamed = tempInfo.isUnnamed();

                std::string funcType = isUnnamed ? "无名符号" : "有名符号";
                if (isUnnamed) {
                    unnamedFixedCount++;
                }

                logger.logToFile("修复" + funcType + ": " + funcName + " [链接: " + getLinkageString(oldLinkage) +
                                 " -> " + getLinkageString(F.getLinkage()) + "]");
                fixedCount++;
            }
        }
    }

    logger.logToFile("批量修复完成，共修复 " + std::to_string(fixedCount) + " 个符号的链接属性");
    logger.logToFile("其中无名符号: " + std::to_string(unnamedFixedCount) + " 个");
}