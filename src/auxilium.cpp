// auxilium.cpp
#include "core.h"
#include "common.h"
#include <algorithm>
#include <cctype>
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include <sstream>

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

    if (funcInfo.isCompilerGenerated()) compilerGenerated++;
}

void AttributeStats::addGlobalVariableInfo(const GlobalVariableInfo& globalVariableInfo) {
    // 统计链接属性
    switch (globalVariableInfo.linkage) {
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
    if (globalVariableInfo.dsoLocal) dsoLocalCount++;

    // 统计可见性
    if (globalVariableInfo.visibility == "Default") defaultVisibility++;
    else if (globalVariableInfo.visibility == "Hidden") hiddenVisibility++;
    else if (globalVariableInfo.visibility == "Protected") protectedVisibility++;

    // 统计声明/定义
    if (globalVariableInfo.isDeclaration) declarations++;
    if (globalVariableInfo.isDefinition) definitions++;

    // 统计有名/无名
    if (globalVariableInfo.isUnnamed()) unnamedGlobalVariables++;
    else namedGlobalVariables++;

    // 统计链接类型分组
    if (globalVariableInfo.isExternal) externalGlobalVariables++;
    if (globalVariableInfo.isInternal) internalGlobalVariables++;
    if (globalVariableInfo.isWeak) weakGlobalVariables++;
    if (globalVariableInfo.isLinkOnce) linkOnceGlobalVariables++;

    if (globalVariableInfo.isCompilerGenerated()) compilerGenerated++;
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

// // 添加调试消息
// void FunctionInfo::addDebugMessage(const std::string& message) {
//     debugMessages.push_back(message);
// }

// // 添加调试输出
// void FunctionInfo::addDebugOutput(const std::string& output) {
//     debugOutput += output + "\n";
// }

// // 获取调试输出
// std::string FunctionInfo::getDebugOutput() const {
//     return debugOutput;
// }

// // 获取调试消息
// std::vector<std::string> FunctionInfo::getDebugMessages() const {
//     return debugMessages;
// }

// // 清空调试信息
// void FunctionInfo::clearDebugInfo() {
//     debugOutput.clear();
//     debugMessages.clear();
// }

// void FunctionInfo::analyzeBasicBlocks(llvm::Function* func) {
//     if (!func) return;

//     // 清空调试信息
//     clearDebugInfo();

//     basicBlocks.clear();
//     allSuccessors.clear();
//     allPredecessors.clear();

//     std::stringstream ss;
//     ss << "=== DEBUG: Analyzing function " << func->getName().str() << " ===\n";
//     addDebugOutput(ss.str());
//     ss.str("");

//     int totalInstructions = 0;
//     int invokeCount = 0;
//     int callCount = 0;
//     int landingPadCount = 0;

//     // 第一遍：收集所有基本块信息
//     for (auto& BB : *func) {
//         std::string bbName;
//         if (BB.hasName()) {
//             bbName = BB.getName().str();
//         } else {
//             std::stringstream ss_name;
//             ss_name << "bb." << std::hex << (void*)&BB;
//             bbName = ss_name.str();
//         }

//         ss << "\n--- DEBUG: BasicBlock " << bbName << " ---\n";
//         addDebugOutput(ss.str());
//         ss.str("");

//         // 创建基本块信息
//         BasicBlockInfo bbInfo(bbName);

//         // 收集指令
//         int bbInstructionCount = 0;
//         for (auto& I : BB) {
//             totalInstructions++;
//             bbInstructionCount++;

//             // 存储指令指针
//             bbInfo.instructions.push_back(&I);

//             // 获取指令类型名
//             std::string instTypeName;
//             llvm::raw_string_ostream rso(instTypeName);
//             I.getType()->print(rso);

//             // 获取指令操作码名
//             std::string opcodeName = I.getOpcodeName();

//             // 构建指令信息字符串
//             ss << "  Inst " << bbInstructionCount << ": "
//                << opcodeName << " (Type: " << instTypeName << ")\n";

//             // 打印完整指令
//             std::string instStr;
//             llvm::raw_string_ostream instStream(instStr);
//             I.print(instStream);
//             ss << "    " << instStream.str() << "\n";

//             // 检查是否为特定类型的指令
//             if (llvm::isa<llvm::InvokeInst>(&I)) {
//                 invokeCount++;
//                 ss << "    [FOUND INVOKE INSTRUCTION]\n";

//                 // 存储到调试消息
//                 addDebugMessage("Found InvokeInst in block " + bbName);

//                 // 打印invoke详细信息
//                 auto* invoke = llvm::cast<llvm::InvokeInst>(&I);
//                 ss << "      Normal dest: ";
//                 if (invoke->getNormalDest()->hasName()) {
//                     ss << invoke->getNormalDest()->getName().str();
//                 } else {
//                     ss << "unnamed";
//                 }
//                 ss << "\n";

//                 ss << "      Unwind dest: ";
//                 if (invoke->getUnwindDest()->hasName()) {
//                     ss << invoke->getUnwindDest()->getName().str();
//                 } else {
//                     ss << "unnamed";
//                 }
//                 ss << "\n";

//                 // 检查被调用的是什么
//                 llvm::Value* calledValue = invoke->getCalledOperand();
//                 ss << "      Called value: ";
//                 if (calledValue->hasName()) {
//                     ss << calledValue->getName().str();
//                 } else {
//                     ss << "unnamed";
//                 }
//                 ss << " (Type: ";
//                 std::string calledTypeStr;
//                 llvm::raw_string_ostream cts(calledTypeStr);
//                 calledValue->getType()->print(cts);
//                 ss << cts.str() << ")\n";

//                 // 检查是否可以直接获取被调用的函数
//                 if (auto* calledFunc = invoke->getCalledFunction()) {
//                     ss << "      Direct call to: " << calledFunc->getName().str() << "\n";
//                     addDebugMessage("Invoke calls function: " + calledFunc->getName().str());
//                 } else {
//                     ss << "      Indirect call (no direct function)\n";
//                     addDebugMessage("Indirect invoke call in block " + bbName);
//                 }

//             } else if (llvm::isa<llvm::CallInst>(&I)) {
//                 callCount++;
//                 ss << "    [FOUND CALL INSTRUCTION]\n";
//                 addDebugMessage("Found CallInst in block " + bbName);
//             } else if (llvm::isa<llvm::LandingPadInst>(&I)) {
//                 landingPadCount++;
//                 ss << "    [FOUND LANDINGPAD INSTRUCTION]\n";
//                 addDebugMessage("Found LandingPadInst in block " + bbName);
//             }

//             // 检查是否为landingpad块
//             if (llvm::isa<llvm::LandingPadInst>(&I)) {
//                 bbInfo.isLandingPad = true;
//             } else if (llvm::isa<llvm::CleanupPadInst>(&I)) {
//                 bbInfo.isCleanupPad = true;
//             } else if (llvm::isa<llvm::CatchPadInst>(&I)) {
//                 bbInfo.isCatchPad = true;
//             }

//             // 添加到调试输出
//             addDebugOutput(ss.str());
//             ss.str("");
//         }

//         ss << "  Total instructions in this block: " << bbInstructionCount << "\n";
//         addDebugOutput(ss.str());
//         ss.str("");

//         // 收集后继基本块
//         if (llvm::Instruction* terminator = BB.getTerminator()) {
//             ss << "  Terminator: " << terminator->getOpcodeName() << "\n";

//             for (unsigned i = 0, e = terminator->getNumSuccessors(); i < e; ++i) {
//                 llvm::BasicBlock* succ = terminator->getSuccessor(i);
//                 if (succ) {
//                     std::string succName = succ->hasName() ? succ->getName().str() : "unnamed";
//                     bbInfo.successors.insert(succName);
//                     ss << "    Successor " << i << ": " << succName << "\n";
//                 }
//             }

//             addDebugOutput(ss.str());
//             ss.str("");
//         }

//         // 插入到映射中
//         basicBlocks.emplace(bbName, std::move(bbInfo));
//     }

//     // 第二步：建立前驱关系
//     for (auto& pair : basicBlocks) {
//         const std::string& bbName = pair.first;
//         BasicBlockInfo& bbInfo = pair.second;

//         for (const auto& succName : bbInfo.successors) {
//             auto it = basicBlocks.find(succName);
//             if (it != basicBlocks.end()) {
//                 it->second.predecessors.insert(bbName);
//             }
//         }
//     }

//     // 第三步：填充allSuccessors和allPredecessors集合
//     for (const auto& pair : basicBlocks) {
//         const BasicBlockInfo& bbInfo = pair.second;

//         allSuccessors.insert(bbInfo.successors.begin(), bbInfo.successors.end());
//         allPredecessors.insert(bbInfo.predecessors.begin(), bbInfo.predecessors.end());
//     }

//     // 构建摘要信息
//     ss << "\n=== DEBUG SUMMARY ===" << "\n";
//     ss << "Function: " << func->getName().str() << "\n";
//     ss << "Total basic blocks: " << basicBlocks.size() << "\n";
//     ss << "Total instructions: " << totalInstructions << "\n";
//     ss << "Call instructions: " << callCount << "\n";
//     ss << "Invoke instructions: " << invokeCount << "\n";
//     ss << "LandingPad instructions: " << landingPadCount << "\n";
//     ss << "====================\n";

//     // 添加摘要到调试信息
//     addDebugOutput(ss.str());

//     // 添加摘要到调试消息
//     addDebugMessage("BasicBlock analysis complete: " +
//                    std::to_string(invokeCount) + " invoke instructions found");
// }

// void FunctionInfo::analyzeInvokeInstructions(llvm::Function* func) {
//     if (!func) return;

//     invokeInstructions.clear();

//     std::stringstream ss;
//     ss << "=== DEBUG: Starting analyzeInvokeInstructions for function "
//        << func->getName().str() << " ===\n";
//     addDebugOutput(ss.str());
//     ss.str("");

//     int totalInstructions = 0;
//     int callBaseCount = 0;
//     int invokeCount = 0;
//     int callCount = 0;

//     for (auto& BB : *func) {
//         std::string bbName = BB.hasName() ? BB.getName().str() : "unnamed";
//         ss << "\n--- Processing BasicBlock: " << bbName << " ---\n";
//         addDebugOutput(ss.str());
//         ss.str("");

//         int bbInstIndex = 0;
//         for (auto& I : BB) {
//             totalInstructions++;
//             bbInstIndex++;

//             // 检查是否为CallBase
//             if (auto* callBase = llvm::dyn_cast<llvm::CallBase>(&I)) {
//                 callBaseCount++;

//                 std::string instStr;
//                 llvm::raw_string_ostream rso(instStr);
//                 I.print(rso);

//                 ss << "  [" << bbInstIndex << "] Found CallBase: "
//                    << I.getOpcodeName() << "\n";
//                 ss << "      " << rso.str() << "\n";

//                 // 检查是否为InvokeInst
//                 if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&I)) {
//                     invokeCount++;
//                     ss << "      [CONFIRMED: This is an InvokeInst]\n";

//                     // 添加调试消息
//                     addDebugMessage("Found InvokeInst in block " + bbName);

//                     // 收集invoke信息
//                     InvokeInfo info;
//                     info.invokeInst = invoke;
//                     info.calledValue = invoke->getCalledOperand();

//                     // 尝试确定被调用的函数
//                     if (auto* funcPtr = llvm::dyn_cast<llvm::Function>(info.calledValue)) {
//                         info.calledFunction = funcPtr;
//                         info.isIndirectCall = false;
//                         ss << "      Called function: " << funcPtr->getName().str() << "\n";
//                         addDebugMessage("Invoke calls function: " + funcPtr->getName().str());
//                     } else {
//                         info.isIndirectCall = true;
//                         ss << "      Indirect call via: ";
//                         if (info.calledValue->hasName()) {
//                             ss << info.calledValue->getName().str();
//                             addDebugMessage("Indirect invoke via: " +
//                                           std::string(info.calledValue->getName().str()));
//                         } else {
//                             ss << "unnamed value";
//                             addDebugMessage("Indirect invoke via unnamed value");
//                         }
//                         ss << "\n";
//                     }

//                     // 获取正常流程目标基本块
//                     llvm::BasicBlock* normalDest = invoke->getNormalDest();
//                     if (normalDest->hasName()) {
//                         info.normalTarget = normalDest->getName().str();
//                     }
//                     ss << "      Normal target: " << info.normalTarget << "\n";

//                     // 获取异常处理目标基本块
//                     llvm::BasicBlock* unwindDest = invoke->getUnwindDest();
//                     if (unwindDest->hasName()) {
//                         info.unwindTarget = unwindDest->getName().str();
//                     }
//                     ss << "      Unwind target: " << info.unwindTarget << "\n";

//                     // 获取函数类型信息
//                     llvm::FunctionType* funcType = invoke->getFunctionType();
//                     if (funcType) {
//                         info.returnType = funcType->getReturnType();
//                         ss << "      Return type: ";
//                         std::string returnTypeStr;
//                         llvm::raw_string_ostream rts(returnTypeStr);
//                         info.returnType->print(rts);
//                         ss << rts.str() << "\n";

//                         for (unsigned i = 0, e = funcType->getNumParams(); i < e; ++i) {
//                             info.argTypes.push_back(funcType->getParamType(i));
//                         }
//                         ss << "      Number of args: " << info.argTypes.size() << "\n";
//                     }

//                     invokeInstructions.push_back(info);

//                 } else if (llvm::isa<llvm::CallInst>(&I)) {
//                     callCount++;
//                     ss << "      [This is a CallInst, not InvokeInst]\n";

//                     // 对于call指令，检查是否可能是被误判的invoke模式
//                     if (instStr.find("invoke") != std::string::npos) {
//                         ss << "      [WARNING: Contains 'invoke' in string but is CallInst]\n";
//                         addDebugMessage("Warning: Instruction contains 'invoke' but is CallInst: " +
//                                        instStr.substr(0, 100));
//                     }
//                 }

//                 // 添加到调试输出
//                 addDebugOutput(ss.str());
//                 ss.str("");

//             } else {
//                 // 不是CallBase的指令
//                 std::string opcode = I.getOpcodeName();
//                 if (opcode == "invoke" || opcode.find("invoke") != std::string::npos) {
//                     ss << "  [" << bbInstIndex << "] WARNING: Opcode '" << opcode
//                        << "' suggests invoke but not recognized as CallBase\n";

//                     std::string instStr;
//                     llvm::raw_string_ostream rso(instStr);
//                     I.print(rso);
//                     ss << "      " << rso.str() << "\n";

//                     addDebugOutput(ss.str());
//                     addDebugMessage("Warning: Opcode '" + opcode +
//                                   "' suggests invoke but not recognized as CallBase");
//                     ss.str("");
//                 }
//             }
//         }
//     }

//     // 构建摘要信息
//     ss << "\n=== DEBUG SUMMARY ===" << "\n";
//     ss << "Function: " << func->getName().str() << "\n";
//     ss << "Total instructions: " << totalInstructions << "\n";
//     ss << "CallBase instructions: " << callBaseCount << "\n";
//     ss << "InvokeInst found: " << invokeCount << "\n";
//     ss << "CallInst found: " << callCount << "\n";
//     ss << "InvokeInstructions collected: " << invokeInstructions.size() << "\n";

//     if (invokeCount > 0 && invokeInstructions.empty()) {
//         ss << "WARNING: Found " << invokeCount << " InvokeInst but collected "
//            << invokeInstructions.size() << " in vector!\n";
//         addDebugMessage("ERROR: InvokeInst count mismatch!");
//     }

//     ss << "====================\n";

//     // 添加摘要到调试输出
//     addDebugOutput(ss.str());

//     // 添加摘要到调试消息
//     addDebugMessage("Invoke analysis complete: " +
//                    std::to_string(invokeInstructions.size()) +
//                    " invoke instructions collected");
// }

// void FunctionInfo::analyzeIndirectCalls(llvm::Function* func) {
//     if (!func) return;

//     indirectCalls.clear();

//     for (auto& BB : *func) {
//         for (auto& I : BB) {
//             llvm::CallBase* callBase = llvm::dyn_cast<llvm::CallBase>(&I);
//             if (callBase) {
//                 llvm::Value* calledValue = callBase->getCalledOperand();

//                 // 如果已经是直接函数调用，跳过
//                 if (llvm::isa<llvm::Function>(calledValue)) {
//                     continue;
//                 }

//                 // 检查是否为间接调用
//                 IndirectCallInfo info;
//                 info.callInst = callBase;
//                 info.calledValue = calledValue;

//                 // 检查是否为invoke指令
//                 if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(callBase)) {
//                     info.isInvoke = true;
//                     if (invoke->getNormalDest()->hasName()) {
//                         info.normalTarget = invoke->getNormalDest()->getName().str();
//                     }
//                     if (invoke->getUnwindDest()->hasName()) {
//                         info.unwindTarget = invoke->getUnwindDest()->getName().str();
//                     }
//                 }

//                 indirectCalls.push_back(info);
//             }
//         }
//     }
// }

// void FunctionInfo::buildControlFlowGraph(llvm::Function* func) {
//     if (!func) return;

//     // 首先分析基本块
//     analyzeBasicBlocks(func);
//     // 分析invoke指令
//     analyzeInvokeInstructions(func);

//     // 分析间接调用
//     analyzeIndirectCalls(func);

// }

// std::unordered_set<llvm::Function*> FunctionInfo::getAllInvokeTargets() const {
//     std::unordered_set<llvm::Function*> targets;

//     // 直接调用目标
//     for (const auto& info : invokeInstructions) {
//         if (info.calledFunction) {
//             targets.insert(info.calledFunction);
//         }
//     }

//     return targets;
// }

// // 新增函数：分析间接调用模式
// void FunctionInfo::analyzeIndirectCallPatterns(const std::unordered_map<llvm::Function*, FunctionInfo>& functionMap) {
//     std::ofstream report("analyzeIndirectCallPatterns.txt");
//     if (!report.is_open()) {
//         return;
//     }
//     report << "=== Indirect Call Analysis ===" << std::endl;

//     int totalIndirectCalls = 0;
//     int resolvedIndirectCalls = 0;
//     int unresolvedIndirectCalls = 0;
//     int indirectInvokes = 0;

//     for (const auto& pair : functionMap) {
//         const FunctionInfo& info = pair.second;

//         totalIndirectCalls += info.indirectCalls.size();

//         for (const auto& indirectInfo : info.indirectCalls) {
//             if (indirectInfo.isInvoke) {
//                 indirectInvokes++;
//             }
//         }
//     }

//     report << "=== Summary: ===" << std::endl;
//     report << "  Total indirect calls: " << totalIndirectCalls << std::endl;
//     report << "  Resolved indirect calls: " << resolvedIndirectCalls << std::endl;
//     report << "  Unresolved indirect calls: " << unresolvedIndirectCalls << std::endl;
//     report << "  Indirect invokes: " << indirectInvokes << std::endl;

//     report << "=== Details: ===" << std::endl;

//     for (const auto& pair : functionMap) {
//         const FunctionInfo& info = pair.second;
//         report << "*******-=-=-=-= Each Detail: =-=-=-=-********" << std::endl;
//         report << info.getBasicBlockInvokesAsString() << std::endl;
//         report << info.getInvokeDetailedInfo() << std::endl;
//         report << info.getInvokeInstructionsAsString() << std::endl;
//         report << info.getInvokeStats() << std::endl;
//         report << info.getCFGWithInvokesHighlighted() << std::endl;
//         report << "*******-=-=-=-= debug: =-=-=-=-********" << std::endl;
//         report << info.getDebugOutput() << std::endl;
//         for (const auto& msg : info.getDebugMessages()) {
//             report << msg << std::endl;
//         }
//         report << "*******-=-=-=-= Detail: =-=-=-=-********" << std::endl;
//     }

//     report.close();
// }

// std::string FunctionInfo::getInvokeInstructionsAsString() const {
//     std::stringstream ss;

//     if (invokeInstructions.empty()) {
//         ss << "Function " << displayName << " has no invoke instructions.\n";
//         return ss.str();
//     }

//     ss << "=== Invoke Instructions in Function: " << displayName << " ===\n";
//     ss << "Total invoke instructions: " << invokeInstructions.size() << "\n\n";

//     for (size_t i = 0; i < invokeInstructions.size(); ++i) {
//         const auto& invokeInfo = invokeInstructions[i];

//         ss << "Invoke #" << i + 1 << ":\n";

//         // 基本信息
//         ss << "  Location: ";
//         if (invokeInfo.invokeInst) {
//             if (invokeInfo.invokeInst->getParent() &&
//                 invokeInfo.invokeInst->getParent()->hasName()) {
//                 ss << "BasicBlock '"
//                    << invokeInfo.invokeInst->getParent()->getName().str() << "'";
//             } else {
//                 ss << "Unknown BasicBlock";
//             }
//         }
//         ss << "\n";

//         // 调用信息
//         ss << "  Called value: ";
//         if (invokeInfo.calledFunction) {
//             ss << "Function '" << invokeInfo.calledFunction->getName().str() << "'";
//         } else if (invokeInfo.calledValue) {
//             ss << "Value '" << invokeInfo.calledValue->getName().str() << "'";
//             if (invokeInfo.isIndirectCall) {
//                 ss << " (indirect call)";
//             }
//         } else {
//             ss << "Unknown";
//         }
//         ss << "\n";

//         // 目标信息
//         ss << "  Normal target: "
//            << (invokeInfo.normalTarget.empty() ? "none" : invokeInfo.normalTarget) << "\n";
//         ss << "  Unwind target: "
//            << (invokeInfo.unwindTarget.empty() ? "none" : invokeInfo.unwindTarget) << "\n";

//         // 函数类型信息
//         if (invokeInfo.returnType) {
//             std::string returnTypeStr;
//             llvm::raw_string_ostream rso(returnTypeStr);
//             invokeInfo.returnType->print(rso);
//             ss << "  Return type: " << rso.str() << "\n";
//         }

//         ss << "  Number of arguments: " << invokeInfo.argTypes.size() << "\n";

//         // 参数类型
//         if (!invokeInfo.argTypes.empty()) {
//             ss << "  Argument types:\n";
//             for (size_t j = 0; j < invokeInfo.argTypes.size(); ++j) {
//                 std::string typeStr;
//                 llvm::raw_string_ostream rso(typeStr);
//                 invokeInfo.argTypes[j]->print(rso);
//                 ss << "    Arg " << j << ": " << rso.str() << "\n";
//             }
//         }

//         ss << "\n";
//     }

//     return ss.str();
// }

// std::string FunctionInfo::getBasicBlockInvokesAsString() const {
//     std::stringstream ss;

//     ss << "=== BasicBlock Invoke Summary for Function: " << displayName << " ===\n\n";

//     // 按基本块组织invoke指令
//     std::unordered_map<std::string, std::vector<const InvokeInfo*>> bbToInvokes;

//     for (const auto& invokeInfo : invokeInstructions) {
//         std::string bbName;
//         if (invokeInfo.invokeInst && invokeInfo.invokeInst->getParent()) {
//             if (invokeInfo.invokeInst->getParent()->hasName()) {
//                 bbName = invokeInfo.invokeInst->getParent()->getName().str();
//             } else {
//                 bbName = "unnamed_block";
//             }
//         } else {
//             bbName = "unknown_block";
//         }
//         bbToInvokes[bbName].push_back(&invokeInfo);
//     }

//     // 输出每个基本块的invoke指令
//     for (const auto& pair : bbToInvokes) {
//         const std::string& bbName = pair.first;
//         const auto& invokes = pair.second;

//         ss << "BasicBlock: " << bbName << " (" << invokes.size()
//            << " invoke" << (invokes.size() != 1 ? "s" : "") << ")\n";

//         // 获取基本块信息
//         auto bbIt = basicBlocks.find(bbName);
//         if (bbIt != basicBlocks.end()) {
//             const auto& bbInfo = bbIt->second;

//             // 输出基本块属性
//             std::string attrs;
//             if (bbInfo.isLandingPad) attrs += "[LandingPad] ";
//             if (bbInfo.isCleanupPad) attrs += "[CleanupPad] ";
//             if (bbInfo.isCatchPad) attrs += "[CatchPad] ";
//             if (!attrs.empty()) {
//                 ss << "  Attributes: " << attrs << "\n";
//             }

//             // 输出前驱和后继
//             ss << "  Predecessors: ";
//             if (bbInfo.predecessors.empty()) {
//                 ss << "none";
//             } else {
//                 bool first = true;
//                 for (const auto& pred : bbInfo.predecessors) {
//                     if (!first) ss << ", ";
//                     ss << pred;
//                     first = false;
//                 }
//             }
//             ss << "\n";

//             ss << "  Successors: ";
//             if (bbInfo.successors.empty()) {
//                 ss << "none";
//             } else {
//                 bool first = true;
//                 for (const auto& succ : bbInfo.successors) {
//                     if (!first) ss << ", ";
//                     ss << succ;
//                     first = false;
//                 }
//             }
//             ss << "\n";
//         }

//         // 输出该基本块中的invoke指令
//         for (size_t i = 0; i < invokes.size(); ++i) {
//             const auto* invokeInfo = invokes[i];

//             ss << "  Invoke " << i + 1 << ":\n";

//             // 被调用的函数
//             ss << "    Calls: ";
//             if (invokeInfo->calledFunction) {
//                 ss << invokeInfo->calledFunction->getName().str();
//             } else if (invokeInfo->calledValue) {
//                 std::string calledStr;
//                 llvm::raw_string_ostream rso(calledStr);
//                 invokeInfo->calledValue->print(rso);
//                 ss << rso.str();
//             }
//             ss << "\n";

//             // 目标信息
//             ss << "    -> Normal: " << invokeInfo->normalTarget << "\n";
//             ss << "    -> Unwind: " << invokeInfo->unwindTarget << "\n";

//             // 如果是间接调用，显示可能的调用目标
//             if (invokeInfo->isIndirectCall) {
//                 // 查找相关的间接调用信息
//                 for (const auto& indirectInfo : indirectCalls) {
//                     if (indirectInfo.callInst == invokeInfo->invokeInst) {
//                         break;
//                     }
//                 }
//             }
//         }

//         ss << "\n";
//     }

//     return ss.str();
// }

// std::string FunctionInfo::getInvokeDetailedInfo() const {
//     std::stringstream ss;

//     if (invokeInstructions.empty()) {
//         return "No invoke instructions found.\n";
//     }

//     ss << "=== Detailed Invoke Analysis for Function: " << displayName << " ===\n\n";

//     for (size_t i = 0; i < invokeInstructions.size(); ++i) {
//         const auto& invokeInfo = invokeInstructions[i];

//         ss << "───────── Invoke Instruction " << i + 1
//            << "/" << invokeInstructions.size() << " ─────────\n";

//         // 指令位置
//         if (invokeInfo.invokeInst) {
//             ss << "Location: ";
//             if (invokeInfo.invokeInst->hasName()) {
//                 ss << "%" << invokeInfo.invokeInst->getName().str();
//             }

//             // 获取调试信息
//             if (auto loc = invokeInfo.invokeInst->getDebugLoc()) {
//                 ss << " at line " << loc.getLine();
//             }
//             ss << "\n";

//             // 完整指令文本
//             std::string instStr;
//             llvm::raw_string_ostream rso(instStr);
//             invokeInfo.invokeInst->print(rso);
//             ss << "Full instruction:\n" << rso.str() << "\n";
//         }

//         // 调用信息表格
//         ss << "\nCall Information:\n";
//         ss << std::left << std::setw(20) << "  Call type:"
//            << (invokeInfo.isIndirectCall ? "Indirect" : "Direct") << "\n";

//         if (invokeInfo.calledFunction) {
//             ss << std::left << std::setw(20) << "  Called function:"
//                << invokeInfo.calledFunction->getName().str() << "\n";
//         } else if (invokeInfo.calledValue) {
//             ss << std::left << std::setw(20) << "  Called value:"
//                << invokeInfo.calledValue->getName().str() << "\n";
//         }

//         ss << std::left << std::setw(20) << "  Normal target:"
//            << invokeInfo.normalTarget << "\n";
//         ss << std::left << std::setw(20) << "  Unwind target:"
//            << invokeInfo.unwindTarget << "\n";

//         // 函数签名信息
//         if (!invokeInfo.argTypes.empty()) {
//             ss << "\nFunction Signature:\n";

//             // 返回类型
//             if (invokeInfo.returnType) {
//                 std::string returnTypeStr;
//                 llvm::raw_string_ostream rso(returnTypeStr);
//                 invokeInfo.returnType->print(rso);
//                 ss << "  Return: " << returnTypeStr << "\n";
//             }

//             // 参数
//             ss << "  Parameters (" << invokeInfo.argTypes.size() << "):\n";
//             for (size_t j = 0; j < invokeInfo.argTypes.size(); ++j) {
//                 std::string paramTypeStr;
//                 llvm::raw_string_ostream rso(paramTypeStr);
//                 invokeInfo.argTypes[j]->print(rso);
//                 ss << "    " << j << ": " << paramTypeStr << "\n";
//             }
//         }

//         // 异常处理信息
//         ss << "\nException Handling:\n";

//         // 检查unwind目标是否为landingpad
//         if (!invokeInfo.unwindTarget.empty()) {
//             auto it = basicBlocks.find(invokeInfo.unwindTarget);
//             if (it != basicBlocks.end()) {
//                 const auto& unwindBB = it->second;

//                 ss << "  Unwind target '" << invokeInfo.unwindTarget << "' is ";
//                 if (unwindBB.isLandingPad) {
//                     ss << "a LandingPad block\n";

//                     // 分析landingpad中的调用
//                     int callCount = 0;
//                     int personalityCalls = 0;

//                     for (auto* inst : unwindBB.instructions) {
//                         if (llvm::isa<llvm::CallInst>(inst)) {
//                             callCount++;
//                             if (auto* call = llvm::dyn_cast<llvm::CallInst>(inst)) {
//                                 if (auto* func = call->getCalledFunction()) {
//                                     std::string name = func->getName().str();
//                                     if (name.find("personality") != std::string::npos) {
//                                         personalityCalls++;
//                                     }
//                                 }
//                             }
//                         }
//                     }

//                     ss << "    Contains " << callCount << " call(s), "
//                        << personalityCalls << " personality function call(s)\n";
//                 } else if (unwindBB.isCleanupPad) {
//                     ss << "a CleanupPad block\n";
//                 } else if (unwindBB.isCatchPad) {
//                     ss << "a CatchPad block\n";
//                 } else {
//                     ss << "a regular basic block\n";
//                 }
//             } else {
//                 ss << "  Unwind target '" << invokeInfo.unwindTarget
//                    << "' not found in function\n";
//             }
//         }

//         // 调用属性
//         if (invokeInfo.invokeInst) {
//             ss << "\nAttributes:\n";

//             auto attrs = invokeInfo.invokeInst->getAttributes();
//             if (!attrs.isEmpty()) {
//                 ss << "  " << attrs.getAsString(0) << "\n";
//             }

//             // 检查特殊属性
//             if (invokeInfo.invokeInst->hasFnAttr(llvm::Attribute::NoUnwind)) {
//                 ss << "  no-unwind\n";
//             }
//             if (invokeInfo.invokeInst->hasFnAttr(llvm::Attribute::NoReturn)) {
//                 ss << "  no-return\n";
//             }
//             if (invokeInfo.invokeInst->doesNotThrow()) {
//                 ss << "  does-not-throw\n";
//             }
//         }

//         ss << "\n";
//     }

//     return ss.str();
// }

// std::string FunctionInfo::getCFGWithInvokesHighlighted() const {
//     std::stringstream ss;

//     ss << "=== Control Flow Graph with Invoke Highlights for: "
//        << displayName << " ===\n\n";

//     // 构建基本块到invoke指令的映射
//     std::unordered_map<std::string, std::vector<std::string>> bbInvokeMap;

//     for (const auto& invokeInfo : invokeInstructions) {
//         std::string bbName;
//         if (invokeInfo.invokeInst && invokeInfo.invokeInst->getParent()) {
//             bbName = invokeInfo.invokeInst->getParent()->hasName()
//                 ? invokeInfo.invokeInst->getParent()->getName().str()
//                 : "unnamed";
//         } else {
//             bbName = "unknown";
//         }

//         // 创建invoke的简要表示
//         std::string invokeDesc;
//         if (invokeInfo.calledFunction) {
//             invokeDesc = "invoke " + invokeInfo.calledFunction->getName().str();
//         } else if (invokeInfo.calledValue && invokeInfo.calledValue->hasName()) {
//             invokeDesc = "invoke " + invokeInfo.calledValue->getName().str();
//         } else {
//             invokeDesc = "invoke (indirect)";
//         }

//         invokeDesc += " -> " + invokeInfo.normalTarget;
//         if (!invokeInfo.unwindTarget.empty()) {
//             invokeDesc += " [unwind: " + invokeInfo.unwindTarget + "]";
//         }

//         bbInvokeMap[bbName].push_back(invokeDesc);
//     }

//     // 输出CFG
//     for (const auto& pair : basicBlocks) {
//         const std::string& bbName = pair.first;
//         const auto& bbInfo = pair.second;

//         // 基本块标题
//         ss << "┌─ BasicBlock: " << bbName;

//         // 特殊块标记
//         std::vector<std::string> marks;
//         if (bbInfo.isLandingPad) marks.push_back("LandingPad");
//         if (bbInfo.isCleanupPad) marks.push_back("CleanupPad");
//         if (bbInfo.isCatchPad) marks.push_back("CatchPad");
//         if (bbInfo.predecessors.empty()) marks.push_back("Entry");
//         if (bbInfo.successors.empty()) marks.push_back("Exit");

//         if (!marks.empty()) {
//             ss << " [";
//             for (size_t i = 0; i < marks.size(); ++i) {
//                 if (i > 0) ss << ", ";
//                 ss << marks[i];
//             }
//             ss << "]";
//         }
//         ss << "\n";

//         // 前驱
//         ss << "│  Predecessors: ";
//         if (bbInfo.predecessors.empty()) {
//             ss << "(none)";
//         } else {
//             bool first = true;
//             for (const auto& pred : bbInfo.predecessors) {
//                 if (!first) ss << ", ";
//                 ss << pred;
//                 first = false;
//             }
//         }
//         ss << "\n";

//         // 该基本块中的invoke指令
//         auto it = bbInvokeMap.find(bbName);
//         if (it != bbInvokeMap.end()) {
//             ss << "│  Invoke Instructions (" << it->second.size() << "):\n";
//             for (const auto& invokeDesc : it->second) {
//                 ss << "│    • " << invokeDesc << "\n";
//             }
//         } else {
//             ss << "│  Invoke Instructions: 0\n";
//         }

//         // 后继
//         ss << "│  Successors: ";
//         if (bbInfo.successors.empty()) {
//             ss << "(none)";
//         } else {
//             bool first = true;
//             for (const auto& succ : bbInfo.successors) {
//                 if (!first) ss << ", ";
//                 ss << succ;
//                 first = false;
//             }
//         }
//         ss << "\n";

//         // 指令计数
//         ss << "│  Total instructions: " << bbInfo.instructions.size() << "\n";

//         // 结束行
//         ss << "└─\n";
//     }

//     return ss.str();
// }

// std::string FunctionInfo::getInvokeStats() const {
//     std::stringstream ss;

//     ss << "=== Invoke Statistics for Function: " << displayName << " ===\n\n";

//     int totalInvokes = invokeInstructions.size();
//     int directInvokes = 0;
//     int indirectInvokes = 0;
//     int invokesWithUnwind = 0;
//     int invokesWithoutUnwind = 0;

//     std::unordered_set<std::string> calledFunctions;
//     std::unordered_set<std::string> unwindTargets;
//     std::unordered_set<std::string> normalTargets;

//     for (const auto& invokeInfo : invokeInstructions) {
//         if (invokeInfo.isIndirectCall) {
//             indirectInvokes++;
//         } else {
//             directInvokes++;
//         }

//         if (invokeInfo.calledFunction) {
//             calledFunctions.insert(invokeInfo.calledFunction->getName().str());
//         }

//         if (!invokeInfo.unwindTarget.empty()) {
//             invokesWithUnwind++;
//             unwindTargets.insert(invokeInfo.unwindTarget);
//         } else {
//             invokesWithoutUnwind++;
//         }

//         if (!invokeInfo.normalTarget.empty()) {
//             normalTargets.insert(invokeInfo.normalTarget);
//         }
//     }

//     // 统计表格
//     ss << std::left << std::setw(30) << "Total invoke instructions:"
//        << totalInvokes << "\n";
//     ss << std::left << std::setw(30) << "Direct invokes:"
//        << directInvokes << " ("
//        << (totalInvokes > 0 ? (directInvokes * 100 / totalInvokes) : 0)
//        << "%)\n";
//     ss << std::left << std::setw(30) << "Indirect invokes:"
//        << indirectInvokes << " ("
//        << (totalInvokes > 0 ? (indirectInvokes * 100 / totalInvokes) : 0)
//        << "%)\n";
//     ss << std::left << std::setw(30) << "Invokes with unwind target:"
//        << invokesWithUnwind << "\n";
//     ss << std::left << std::setw(30) << "Invokes without unwind target:"
//        << invokesWithoutUnwind << "\n";
//     ss << std::left << std::setw(30) << "Unique called functions:"
//        << calledFunctions.size() << "\n";
//     ss << std::left << std::setw(30) << "Unique normal targets:"
//        << normalTargets.size() << "\n";
//     ss << std::left << std::setw(30) << "Unique unwind targets:"
//        << unwindTargets.size() << "\n";

//     // 如果存在间接调用，显示可能的调用目标统计
//     if (indirectInvokes > 0) {
//         ss << "\nIndirect Call Analysis:\n";
//         ss << "  Total indirect calls: " << indirectInvokes << "\n";
//     }

//     // 显示最常见的正常目标和异常目标
//     if (!normalTargets.empty() || !unwindTargets.empty()) {
//         ss << "\nTarget Analysis:\n";

//         if (!normalTargets.empty()) {
//             ss << "  Normal targets (" << normalTargets.size() << "): ";
//             bool first = true;
//             for (const auto& target : normalTargets) {
//                 if (!first) ss << ", ";
//                 ss << target;
//                 first = false;
//                 if (normalTargets.size() > 5 && std::distance(normalTargets.begin(), normalTargets.find(target)) >= 4) {
//                     ss << ", ...";
//                     break;
//                 }
//             }
//             ss << "\n";
//         }

//         if (!unwindTargets.empty()) {
//             ss << "  Unwind targets (" << unwindTargets.size() << "): ";
//             bool first = true;
//             for (const auto& target : unwindTargets) {
//                 if (!first) ss << ", ";
//                 ss << target;
//                 first = false;
//                 if (unwindTargets.size() > 5 && std::distance(unwindTargets.begin(), unwindTargets.find(target)) >= 4) {
//                     ss << ", ...";
//                     break;
//                 }
//             }
//             ss << "\n";
//         }
//     }

//     return ss.str();
// }