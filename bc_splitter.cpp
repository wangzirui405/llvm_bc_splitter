#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>
#include <functional>
#include <cstdint>
#include <queue>

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

using namespace llvm;
using namespace std;

// 在 FunctionInfo 结构体中增强无名函数处理
struct FunctionInfo {
    string name;
    string displayName;
    Function* funcPtr = nullptr;
    int outDegree = 0;
    int inDegree = 0;
    int groupIndex = -1;
    bool isProcessed = false;
    int sequenceNumber = -1;  // 只有无名函数才有序号，有名函数为-1
    bool isUnnamed = false;
    unordered_set<Function*> calledFunctions;
    unordered_set<Function*> callerFunctions;

    FunctionInfo() = default;

    FunctionInfo(Function* func, int seqNum = -1) {
        funcPtr = func;
        name = func->getName().str();

        // 检测无名函数
        isUnnamed = name.empty() ||
                   name.find("__unnamed_") == 0 ||
                   name.find(".") == 0 ||
                   name.find("__") == 0 ||
                   name == "d" || name == "t" || name == "b" ||
                   isNumberString(name);

        if (isUnnamed) {
            // 只有无名函数才分配序号
            sequenceNumber = seqNum;
            displayName = "unnamed_" + to_string(seqNum) + "_" +
                         to_string(reinterpret_cast<uintptr_t>(func));
        } else {
            // 有名函数没有序号
            sequenceNumber = -1;
            displayName = name;
        }
    }

public:
    // 辅助函数：检查字符串是否全为数字
    static bool isNumberString(const string& str) {
        if (str.empty()) return false;
        for (char c : str) {
            if (!isdigit(c)) return false;
        }
        return true;
    }
};

class BCModuleSplitter {
private:
    LLVMContext context;
    unique_ptr<Module> module;
    unordered_map<Function*, FunctionInfo> functionMap;
    vector<Function*> functionPtrs;
    ofstream logFile;
    int totalGroups = 0;  // 新增：总分组数

    // 新增：存储需要调整为external的函数
    unordered_set<Function*> functionsNeedExternal;
    // 在 BCModuleSplitter 类中添加新的日志方法
private:
    // 新增：为每个BC文件创建独立日志文件
    ofstream createIndividualLogFile(const string& bcFilename, const string& suffix = "") {
        string logFilename = bcFilename;
        if (!suffix.empty()) {
            logFilename += suffix;
        }
        logFilename += ".log";

        ofstream individualLog;
        individualLog.open(logFilename, ios::out | ios::app);
        if (individualLog.is_open()) {
            individualLog << "=== BC文件验证日志: " << bcFilename << " ===" << endl;
        } else {
            logError("无法创建独立日志文件: " + logFilename);
        }
        return individualLog;
    }

    // 新增：向独立日志文件写入消息
    void logToIndividualLog(ofstream& individualLog, const string& message, bool echoToMain = false) {
        if (individualLog.is_open()) {
            individualLog << message << endl;
        }
        if (echoToMain) {
            logToFile(message);
        }
    }

public:
    BCModuleSplitter() {
        // 打开日志文件
        logFile.open("bc_splitter.log", ios::out | ios::app);
        if (!logFile.is_open()) {
            cerr << "警告: 无法打开日志文件" << endl;
        } else {
            logFile << "=== BC Splitter 日志开始 ===" << endl;
        }
    }

    ~BCModuleSplitter() {
        if (logFile.is_open()) {
            logFile << "=== BC Splitter 日志结束 ===" << endl;
            logFile.close();
        }
    }

    void log(const string& message) {
        if (logFile.is_open()) {
            logFile << message << endl;
        }
        cout << message << endl;
    }

    void logToFile(const string& message) {
        if (logFile.is_open()) {
            logFile << message << endl;
        }
    }

    void logError(const string& message) {
        if (logFile.is_open()) {
            logFile << "[ERROR] " + message << endl;
        }
        cerr << "[ERROR] " + message << endl;
    }

    bool loadBCFile(const string& filename) {
        log("加载BC文件: " + filename);
        SMDiagnostic err;
        module = parseIRFile(filename, err, context);

        if (!module) {
            logError("无法加载BC文件: " + filename);
            err.print("BCModuleSplitter", errs());
            return false;
        }

        log("成功加载模块: " + module->getModuleIdentifier());
        return true;
    }

    void analyzeFunctions() {
        log("开始分析函数调用关系...");

        // 收集所有函数，只为无名函数分配序号
        int unnamedSequenceNumber = 0;
        for (Function& func : *module) {
            if (func.isDeclaration()) continue;

            string funcName = func.getName().str();
            bool isUnnamed = funcName.empty() ||
                            funcName.find("__unnamed_") == 0 ||
                            funcName.find(".") == 0 ||
                            funcName.find("__") == 0 ||
                            funcName == "d" || funcName == "t" || funcName == "b" ||
                            FunctionInfo::isNumberString(funcName);

            // 只有无名函数才分配序号
            int seqNum = isUnnamed ? unnamedSequenceNumber++ : -1;

            auto result = functionMap.emplace(&func, FunctionInfo(&func, seqNum));
            if (result.second) {
                functionPtrs.push_back(&func);
            }
        }

        log("收集到 " + to_string(functionMap.size()) + " 个函数定义");
        log("其中无名函数数量: " + to_string(unnamedSequenceNumber));

        // 分析调用关系（保持不变）
        for (Function& func : *module) {
            if (func.isDeclaration()) continue;

            for (BasicBlock& bb : func) {
                for (Instruction& inst : bb) {
                    if (CallInst* callInst = dyn_cast<CallInst>(&inst)) {
                        Function* calledFunc = callInst->getCalledFunction();

                        if (!calledFunc && callInst->getNumOperands() > 0) {
                            Value* calledValue = callInst->getCalledOperand();
                            if (isa<Function>(calledValue)) {
                                calledFunc = cast<Function>(calledValue);
                            }
                        }

                        if (calledFunc && !calledFunc->isIntrinsic() && functionMap.count(calledFunc)) {
                            functionMap[&func].calledFunctions.insert(calledFunc);
                            functionMap[&func].outDegree++;

                            functionMap[calledFunc].callerFunctions.insert(&func);
                            functionMap[calledFunc].inDegree++;
                        }
                    }
                }
            }
        }

        log("分析完成，共分析 " + to_string(functionMap.size()) + " 个函数");
    }

    void printFunctionInfo() {
        logToFile("\n=== 函数调用关系分析 ===");
        for (const auto& pair : functionMap) {
            const FunctionInfo& info = pair.second;
            string groupInfo = info.groupIndex >= 0 ?
                " [组: " + to_string(info.groupIndex) + "]" : " [未分组]";
            string seqInfo = info.isUnnamed ?
                " [序号: " + to_string(info.sequenceNumber) + "]" : " [有名]";

            logToFile("函数: " + info.displayName +
                seqInfo +
                " [出度: " + to_string(info.outDegree) +
                ", 入度: " + to_string(info.inDegree) + "]" + groupInfo);
        }
    }

    // 获取高入度函数（入度大于500）
    vector<Function*> getHighInDegreeFunctions(int threshold = 500) {
        vector<Function*> highInDegreeFuncs;

        for (const auto& pair : functionMap) {
            if (pair.second.inDegree > threshold && !pair.second.isProcessed) {
                highInDegreeFuncs.push_back(pair.first);
            }
        }

        log("找到 " + to_string(highInDegreeFuncs.size()) + " 个入度大于 " + to_string(threshold) + " 的函数");
        return highInDegreeFuncs;
    }

    // 获取孤立函数（出度为0且入度为0）
    vector<Function*> getIsolatedFunctions() {
        vector<Function*> isolatedFuncs;

        for (const auto& pair : functionMap) {
            if (pair.second.outDegree == 0 && pair.second.inDegree == 0 && !pair.second.isProcessed) {
                isolatedFuncs.push_back(pair.first);
            }
        }

        log("找到 " + to_string(isolatedFuncs.size()) + " 个孤立函数（出度=0且入度=0）");
        return isolatedFuncs;
    }

    // 获取全局变量组
    unordered_set<GlobalVariable*> getGlobalVariables() {
        unordered_set<GlobalVariable*> globals;

        for (GlobalVariable& global : module->globals()) {
            globals.insert(&global);
        }

        log("找到 " + to_string(globals.size()) + " 个全局变量");
        return globals;
    }

    vector<Function*> getTopFunctions(int topN) {
        vector<pair<Function*, int>> scores;

        for (const auto& pair : functionMap) {
            // 只考虑未处理的函数
            if (!pair.second.isProcessed) {
                int totalScore = pair.second.outDegree + pair.second.inDegree;
                scores.emplace_back(pair.first, totalScore);
            }
        }

        std::sort(scores.begin(), scores.end(),
             [](const pair<Function*, int>& a, const pair<Function*, int>& b) -> bool {
                 return a.second > b.second;
             });

        vector<Function*> result;
        logToFile("\n=== 函数排名前" + to_string(topN) + " ===");
        for (int i = 0; i < min(topN, (int)scores.size()); i++) {
            result.push_back(scores[i].first);
            string funcName = functionMap[scores[i].first].displayName;
            logToFile("排名 " + to_string(i + 1) + ": " + funcName +
                " (总分: " + to_string(scores[i].second) + ")");
        }

        return result;
    }

    // 优化版：获取函数组，避免包含已处理的函数
    unordered_set<Function*> getFunctionGroup(Function* func) {
        unordered_set<Function*> group;
        if (!func || functionMap.find(func) == functionMap.end()) {
            logError("尝试获取无效函数的组");
            return group;
        }

        const FunctionInfo& info = functionMap[func];

        // 只添加未处理的函数
        if (!info.isProcessed) {
            group.insert(func);
        }

        // 添加被调用的函数（仅未处理）
        for (Function* called : info.calledFunctions) {
            if (called && functionMap.count(called) && !functionMap[called].isProcessed) {
                group.insert(called);
            }
        }

        // 添加调用者函数（仅未处理）
        for (Function* caller : info.callerFunctions) {
            if (caller && functionMap.count(caller) && !functionMap[caller].isProcessed) {
                group.insert(caller);
            }
        }

        return group;
    }

    // 递归获取高入度函数的所有出度函数
    unordered_set<Function*> getHighInDegreeWithOutDegreeFunctions(const unordered_set<Function*>& highInDegreeFuncs) {
        unordered_set<Function*> completeSet;
        queue<Function*> toProcess;

        // 初始添加所有高入度函数
        for (Function* func : highInDegreeFuncs) {
            if (!func || functionMap[func].isProcessed) continue;
            completeSet.insert(func);
            toProcess.push(func);
        }

        if (toProcess.empty()) {
            logToFile("已无需进行扩展，均已记录，总数为: " + to_string(highInDegreeFuncs.size()));
            return highInDegreeFuncs;
        }

        // 广度优先遍历所有出度函数
        while (!toProcess.empty()) {
            Function* current = toProcess.front();
            toProcess.pop();

            // 获取当前函数的所有出度函数
            const FunctionInfo& info = functionMap[current];
            for (Function* called : info.calledFunctions) {
                if (!called || functionMap[called].isProcessed) continue;

                // 如果函数不在集合中，添加到集合和队列
                if (completeSet.find(called) == completeSet.end()) {
                    completeSet.insert(called);
                    toProcess.push(called);
                    logToFile("  添加出度函数: " + functionMap[called].displayName +
                             " [被 " + functionMap[current].displayName + " 调用]");
                }
            }
        }

        return completeSet;
    }

    // 创建全局变量组BC文件
    bool createGlobalVariablesBCFile(const unordered_set<GlobalVariable*>& globals, const string& filename) {
        logToFile("创建全局变量BC文件: " + filename);

        LLVMContext newContext;
        auto newModule = make_unique<Module>("global_variables", newContext);

        // 复制原始模块的基本属性
        newModule->setTargetTriple(module->getTargetTriple());
        newModule->setDataLayout(module->getDataLayout());

        // 复制全局变量 - 使用声明而非定义
        for (GlobalVariable* origGlobal : globals) {
            if (!origGlobal) continue;

            // 对于全局变量，我们创建外部声明而不是定义
            // 这样可以避免链接属性问题
            GlobalVariable* newGlobal = new GlobalVariable(
                *newModule,
                origGlobal->getValueType(),
                false, // 设为非常量，避免初始化问题
                GlobalValue::ExternalLinkage, // 使用外部链接
                nullptr, // 没有初始值
                origGlobal->getName(),
                nullptr,
                GlobalVariable::NotThreadLocal,
                origGlobal->getAddressSpace()
            );

            // 设置可见性
            newGlobal->setVisibility(origGlobal->getVisibility());

            logToFile("复制全局变量声明: " + origGlobal->getName().str());
        }

        return writeBitcodeSafely(*newModule, filename);
    }

    // 创建高入度函数组BC文件（包含完整的出度链）
    bool createHighInDegreeBCFile(const unordered_set<Function*>& highInDegreeFuncs, const string& filename) {
        logToFile("创建高入度函数BC文件: " + filename + " (包含完整出度链)");

        // 首先获取完整的高入度函数组（包含所有出度函数）
        unordered_set<Function*> completeHighInDegreeSet = getHighInDegreeWithOutDegreeFunctions(highInDegreeFuncs);

        LLVMContext newContext;
        auto newModule = make_unique<Module>("high_in_degree_functions", newContext);

        // 复制原始模块的基本属性
        newModule->setTargetTriple(module->getTargetTriple());
        newModule->setDataLayout(module->getDataLayout());

        // 复制高入度函数签名（包括所有出度函数）
        for (Function* origFunc : completeHighInDegreeSet) {
            if (!origFunc) continue;

            // 创建函数类型
            vector<Type*> paramTypes;
            for (const auto& arg : origFunc->args()) {
                Type* argType = arg.getType();

                if (argType->isIntegerTy()) {
                    paramTypes.push_back(Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
                } else if (argType->isPointerTy()) {
                    PointerType* ptrType = cast<PointerType>(argType);
                    unsigned addressSpace = ptrType->getAddressSpace();
                    paramTypes.push_back(PointerType::get(newContext, addressSpace));
                } else if (argType->isVoidTy()) {
                    paramTypes.push_back(Type::getVoidTy(newContext));
                } else if (argType->isFloatTy()) {
                    paramTypes.push_back(Type::getFloatTy(newContext));
                } else if (argType->isDoubleTy()) {
                    paramTypes.push_back(Type::getDoubleTy(newContext));
                } else {
                    paramTypes.push_back(PointerType::get(newContext, 0));
                }
            }

            // 确定返回类型
            Type* returnType = origFunc->getReturnType();
            Type* newReturnType;

            if (returnType->isIntegerTy()) {
                newReturnType = Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
            } else if (returnType->isPointerTy()) {
                PointerType* ptrType = cast<PointerType>(returnType);
                unsigned addressSpace = ptrType->getAddressSpace();
                newReturnType = PointerType::get(newContext, addressSpace);
            } else if (returnType->isVoidTy()) {
                newReturnType = Type::getVoidTy(newContext);
            } else if (returnType->isFloatTy()) {
                newReturnType = Type::getFloatTy(newContext);
            } else if (returnType->isDoubleTy()) {
                newReturnType = Type::getDoubleTy(newContext);
            } else {
                newReturnType = PointerType::get(newContext, 0);
            }

            FunctionType* funcType = FunctionType::get(newReturnType, paramTypes, origFunc->isVarArg());

            Function* newFunc = Function::Create(
                funcType,
                origFunc->getLinkage(),
                origFunc->getName(),
                newModule.get()
            );

            newFunc->setCallingConv(origFunc->getCallingConv());
            newFunc->setVisibility(origFunc->getVisibility());
            newFunc->setDLLStorageClass(origFunc->getDLLStorageClass());

            // 标记函数已处理
            if (functionMap.count(origFunc)) {
                functionMap[origFunc].groupIndex = 1; // 高入度函数组编号为1
                functionMap[origFunc].isProcessed = true;
            }

            // 记录函数类型（高入度函数或出度函数）
            string funcTypeStr = (highInDegreeFuncs.find(origFunc) != highInDegreeFuncs.end()) ?
                "高入度函数" : "出度函数";

            logToFile("复制" + funcTypeStr + ": " + origFunc->getName().str() +
                     " [入度: " + to_string(functionMap[origFunc].inDegree) +
                     ", 出度: " + to_string(functionMap[origFunc].outDegree) +
                     ", 链接: " + getLinkageString(origFunc->getLinkage()) + "]");
        }

        return writeBitcodeSafely(*newModule, filename);
    }

    // 创建孤立函数组BC文件
    bool createIsolatedFunctionsBCFile(const unordered_set<Function*>& isolatedFuncs, const string& filename) {
        logToFile("创建孤立函数BC文件: " + filename);

        LLVMContext newContext;
        auto newModule = make_unique<Module>("isolated_functions", newContext);

        // 复制原始模块的基本属性
        newModule->setTargetTriple(module->getTargetTriple());
        newModule->setDataLayout(module->getDataLayout());

        // 复制孤立函数签名
        for (Function* origFunc : isolatedFuncs) {
            if (!origFunc) continue;

            // 创建函数类型
            vector<Type*> paramTypes;
            for (const auto& arg : origFunc->args()) {
                Type* argType = arg.getType();

                if (argType->isIntegerTy()) {
                    paramTypes.push_back(Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
                } else if (argType->isPointerTy()) {
                    PointerType* ptrType = cast<PointerType>(argType);
                    unsigned addressSpace = ptrType->getAddressSpace();
                    paramTypes.push_back(PointerType::get(newContext, addressSpace));
                } else if (argType->isVoidTy()) {
                    paramTypes.push_back(Type::getVoidTy(newContext));
                } else if (argType->isFloatTy()) {
                    paramTypes.push_back(Type::getFloatTy(newContext));
                } else if (argType->isDoubleTy()) {
                    paramTypes.push_back(Type::getDoubleTy(newContext));
                } else {
                    paramTypes.push_back(PointerType::get(newContext, 0));
                }
            }

            // 确定返回类型
            Type* returnType = origFunc->getReturnType();
            Type* newReturnType;

            if (returnType->isIntegerTy()) {
                newReturnType = Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
            } else if (returnType->isPointerTy()) {
                PointerType* ptrType = cast<PointerType>(returnType);
                unsigned addressSpace = ptrType->getAddressSpace();
                newReturnType = PointerType::get(newContext, addressSpace);
            } else if (returnType->isVoidTy()) {
                newReturnType = Type::getVoidTy(newContext);
            } else if (returnType->isFloatTy()) {
                newReturnType = Type::getFloatTy(newContext);
            } else if (returnType->isDoubleTy()) {
                newReturnType = Type::getDoubleTy(newContext);
            } else {
                newReturnType = PointerType::get(newContext, 0);
            }

            FunctionType* funcType = FunctionType::get(newReturnType, paramTypes, origFunc->isVarArg());

            Function* newFunc = Function::Create(
                funcType,
                origFunc->getLinkage(),
                origFunc->getName(),
                newModule.get()
            );

            newFunc->setCallingConv(origFunc->getCallingConv());
            newFunc->setVisibility(origFunc->getVisibility());
            newFunc->setDLLStorageClass(origFunc->getDLLStorageClass());

            // 标记函数已处理
            if (functionMap.count(origFunc)) {
                functionMap[origFunc].groupIndex = 2; // 孤立函数组编号为2
                functionMap[origFunc].isProcessed = true;
            }

            logToFile("复制孤立函数: " + origFunc->getName().str() +
                     " [出度=0, 入度=0, 链接: " + getLinkageString(origFunc->getLinkage()) + "]");
        }

        return writeBitcodeSafely(*newModule, filename);
    }

    // 优化的BC文件创建方法，正确处理链接属性
    bool createBCFileWithSignatures(const unordered_set<Function*>& group, const string& filename, int groupIndex) {
        logToFile("创建BC文件: " + filename + " (组 " + to_string(groupIndex) + ")");

        // 使用全新的上下文
        LLVMContext newContext;
        auto newModule = make_unique<Module>(filename, newContext);

        // 复制原始模块的基本属性
        newModule->setTargetTriple(module->getTargetTriple());
        newModule->setDataLayout(module->getDataLayout());

        // 为每个函数创建签名并保持正确的链接属性
        for (Function* origFunc : group) {
            if (!origFunc) continue;

            // 在新建上下文中重新创建函数类型
            vector<Type*> paramTypes;
            for (const auto& arg : origFunc->args()) {
                Type* argType = arg.getType();

                // 将类型映射到新上下文中的对应类型
                if (argType->isIntegerTy()) {
                    paramTypes.push_back(Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
                } else if (argType->isPointerTy()) {
                    PointerType* ptrType = cast<PointerType>(argType);
                    unsigned addressSpace = ptrType->getAddressSpace();
                    paramTypes.push_back(PointerType::get(newContext, addressSpace));
                } else if (argType->isVoidTy()) {
                    paramTypes.push_back(Type::getVoidTy(newContext));
                } else if (argType->isFloatTy()) {
                    paramTypes.push_back(Type::getFloatTy(newContext));
                } else if (argType->isDoubleTy()) {
                    paramTypes.push_back(Type::getDoubleTy(newContext));
                } else {
                    paramTypes.push_back(PointerType::get(newContext, 0));
                }
            }

            // 确定返回类型
            Type* returnType = origFunc->getReturnType();
            Type* newReturnType;

            if (returnType->isIntegerTy()) {
                newReturnType = Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
            } else if (returnType->isPointerTy()) {
                PointerType* ptrType = cast<PointerType>(returnType);
                unsigned addressSpace = ptrType->getAddressSpace();
                newReturnType = PointerType::get(newContext, addressSpace);
            } else if (returnType->isVoidTy()) {
                newReturnType = Type::getVoidTy(newContext);
            } else if (returnType->isFloatTy()) {
                newReturnType = Type::getFloatTy(newContext);
            } else if (returnType->isDoubleTy()) {
                newReturnType = Type::getDoubleTy(newContext);
            } else {
                newReturnType = PointerType::get(newContext, 0);
            }

            // 创建函数类型
            FunctionType* funcType = FunctionType::get(newReturnType, paramTypes, origFunc->isVarArg());

            // 创建函数并保持正确的链接属性
            Function* newFunc = Function::Create(
                funcType,
                origFunc->getLinkage(),  // 使用原始链接属性
                origFunc->getName(),
                newModule.get()
            );

            // 复制重要属性
            newFunc->setCallingConv(origFunc->getCallingConv());
            newFunc->setVisibility(origFunc->getVisibility());  // 保持可见性
            newFunc->setDLLStorageClass(origFunc->getDLLStorageClass());

            // 标记函数在原始模块中的分组信息
            if (functionMap.count(origFunc)) {
                functionMap[origFunc].groupIndex = groupIndex;
                functionMap[origFunc].isProcessed = true;
            }

            logToFile("复制函数: " + origFunc->getName().str() +
                     " [链接: " + getLinkageString(origFunc->getLinkage()) +
                     ", 可见性: " + getVisibilityString(origFunc->getVisibility()) + "]");
        }

        // 写入bitcode
        return writeBitcodeSafely(*newModule, filename);
    }

    // 获取链接属性字符串表示
    string getLinkageString(GlobalValue::LinkageTypes linkage) {
        switch (linkage) {
            case GlobalValue::ExternalLinkage: return "External";
            case GlobalValue::InternalLinkage: return "Internal";
            case GlobalValue::PrivateLinkage: return "Private";
            case GlobalValue::WeakAnyLinkage: return "WeakAny";
            case GlobalValue::WeakODRLinkage: return "WeakODR";
            case GlobalValue::CommonLinkage: return "Common";
            case GlobalValue::AppendingLinkage: return "Appending";
            case GlobalValue::ExternalWeakLinkage: return "ExternalWeak";
            case GlobalValue::AvailableExternallyLinkage: return "AvailableExternally";
            default: return "Unknown(" + to_string(linkage) + ")";
        }
    }

    // 获取可见性字符串表示
    string getVisibilityString(GlobalValue::VisibilityTypes visibility) {
        switch (visibility) {
            case GlobalValue::DefaultVisibility: return "Default";
            case GlobalValue::HiddenVisibility: return "Hidden";
            case GlobalValue::ProtectedVisibility: return "Protected";
            default: return "Unknown(" + to_string(visibility) + ")";
        }
    }

    // 安全的bitcode写入方法
    bool writeBitcodeSafely(Module& mod, const string& filename) {
        logToFile("安全写入bitcode: " + filename);

        error_code ec;
        raw_fd_ostream outFile(filename, ec, sys::fs::OF_None);
        if (ec) {
            logError("无法创建文件: " + filename + " - " + ec.message());
            return false;
        }

        try {
            WriteBitcodeToFile(mod, outFile);
            outFile.close();
            log("成功写入: " + filename);
            return true;
        } catch (const std::exception& e) {
            logError("写入bitcode时发生异常: " + string(e.what()));
            sys::fs::remove(filename);
            return false;
        }
    }

    // 新增：转义序列解码函数
    string decodeEscapeSequences(const string& escapedStr) {
        string result;
        for (size_t i = 0; i < escapedStr.length(); ) {
            if (escapedStr[i] == '\\' && i + 3 < escapedStr.length() &&
                isxdigit(escapedStr[i+1]) && isxdigit(escapedStr[i+2])) {
                // 处理 \XX 格式的转义序列
                string hexStr = escapedStr.substr(i+1, 2);
                try {
                    char decodedChar = static_cast<char>(std::stoi(hexStr, nullptr, 16));
                    result += decodedChar;
                    i += 3; // 跳过转义序列
                } catch (...) {
                    // 转换失败，保持原样
                    result += escapedStr[i];
                    i++;
                }
            } else if (escapedStr[i] == '\\' && i + 1 < escapedStr.length()) {
                // 处理其他转义序列
                switch (escapedStr[i+1]) {
                    case '\\': result += '\\'; break;
                    case '"': result += '"'; break;
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    default:
                        result += escapedStr[i];
                        i++;
                        continue;
                }
                i += 2;
            } else {
                result += escapedStr[i];
                i++;
            }
        }
        return result;
    }

    // 新增：带独立日志的构建函数名映射方法
    void buildFunctionNameMapsWithLog(const unordered_set<Function*>& group,
                                    unordered_map<string, Function*>& nameToFunc,
                                    unordered_map<string, string>& escapedToOriginal,
                                    ofstream& individualLog) {

        for (Function* func : group) {
            if (!func) continue;

            string originalName = func->getName().str();
            nameToFunc[originalName] = func;

            logToIndividualLog(individualLog, "组内函数: " + originalName +
                            " [链接: " + getLinkageString(func->getLinkage()) +
                            ", 可见性: " + getVisibilityString(func->getVisibility()) + "]");

            // 转义序列处理逻辑...
            if (originalName.find("§") != string::npos) {
                string escapedName = originalName;
                size_t pos = 0;
                while ((pos = escapedName.find("§", pos)) != string::npos) {
                    escapedName.replace(pos, 2, "\\C2\\A7");
                    pos += 6;
                }
                escapedToOriginal[escapedName] = originalName;
                logToIndividualLog(individualLog, "  转义序列映射: " + escapedName + " -> " + originalName);
            }
        }
    }

    // 完整的带独立日志的分析验证错误方法
    // 修改 analyzeVerifierErrorsWithLog 函数中的映射构建部分
    unordered_set<string> analyzeVerifierErrorsWithLog(const string& verifyOutput,
                                                    const unordered_set<Function*>& group,
                                                    ofstream& individualLog) {
        unordered_set<string> functionsNeedExternal;

        logToIndividualLog(individualLog, "分析verifier错误输出...");
        logToIndividualLog(individualLog, "Verifier输出长度: " + to_string(verifyOutput.length()));

        // 构建函数名映射和序号映射（只包含无名函数）
        unordered_map<string, Function*> nameToFunc;
        unordered_map<int, Function*> sequenceToFunc;  // 只包含无名函数的序号映射
        unordered_map<string, string> escapedToOriginal;

        // 新增：记录所有无名函数的原始名称和序号
        vector<pair<int, string>> unnamedFunctions;

        // 构建映射
        for (Function* func : group) {
            if (!func) continue;

            string originalName = func->getName().str();
            nameToFunc[originalName] = func;

            // 只记录无名函数的序号映射
            if (functionMap.count(func) && functionMap[func].isUnnamed) {
                int seqNum = functionMap[func].sequenceNumber;
                if (seqNum >= 0) {
                    sequenceToFunc[seqNum] = func;
                    // 记录无名函数信息
                    unnamedFunctions.emplace_back(seqNum, originalName);
                    logToIndividualLog(individualLog, "无名函数序号映射: " + to_string(seqNum) + " -> " + originalName);
                }
            }

            // 记录函数信息
            string seqInfo = functionMap[func].isUnnamed ?
                            " [序号: " + to_string(functionMap[func].sequenceNumber) + "]" :
                            " [有名函数]";
            logToIndividualLog(individualLog, "组内函数: " + originalName +
                            seqInfo +
                            " [链接: " + getLinkageString(func->getLinkage()) +
                            ", 可见性: " + getVisibilityString(func->getVisibility()) + "]");

            // 转义序列处理
            if (originalName.find("§") != string::npos) {
                string escapedName = originalName;
                size_t pos = 0;
                while ((pos = escapedName.find("§", pos)) != string::npos) {
                    escapedName.replace(pos, 2, "\\C2\\A7");
                    pos += 6;
                }
                escapedToOriginal[escapedName] = originalName;
                logToIndividualLog(individualLog, "  转义序列映射: " + escapedName + " -> " + originalName);
            }
        }

        // 输出无名函数统计信息
        if (!unnamedFunctions.empty()) {
            logToIndividualLog(individualLog, "组内无名函数统计: 共 " + to_string(unnamedFunctions.size()) + " 个无名函数");
            for (const auto& unnamed : unnamedFunctions) {
                logToIndividualLog(individualLog, "  序号 " + to_string(unnamed.first) + ": " + unnamed.second);
            }
        }

        // 专门处理 "Global is external, but doesn't have external or weak linkage!" 错误
        string searchPattern = "Global is external, but doesn't have external or weak linkage!";
        size_t patternLength = searchPattern.length();
        size_t searchPos = 0;
        int errorCount = 0;
        int unnamedMatchCount = 0;  // 新增：统计通过序号匹配的无名函数数量

        while ((searchPos = verifyOutput.find(searchPattern, searchPos)) != string::npos) {
            errorCount++;

            // 找到错误描述后的函数指针信息
            size_t ptrPos = verifyOutput.find("ptr @", searchPos + patternLength);
            if (ptrPos != string::npos) {
                size_t nameStart = ptrPos + 5; // "ptr @" 的长度

                // 检查函数名是否带引号
                bool isQuoted = (nameStart < verifyOutput.length() && verifyOutput[nameStart] == '"');
                string extractedName;

                if (isQuoted) {
                    // 处理带引号的函数名
                    size_t quoteEnd = verifyOutput.find('"', nameStart + 1);
                    if (quoteEnd != string::npos) {
                        extractedName = verifyOutput.substr(nameStart + 1, quoteEnd - nameStart - 1);
                        logToIndividualLog(individualLog, "发现带引号的函数名: \"" + extractedName + "\"");
                    }
                } else {
                    // 处理不带引号的函数名
                    size_t nameEnd = verifyOutput.find_first_of(" \n\r\t,;", nameStart);
                    if (nameEnd == string::npos) nameEnd = verifyOutput.length();

                    extractedName = verifyOutput.substr(nameStart, nameEnd - nameStart);
                    logToIndividualLog(individualLog, "发现不带引号的函数名: " + extractedName);
                }

                if (!extractedName.empty()) {
                    bool foundMatch = false;

                    // 尝试1: 直接匹配函数名
                    if (nameToFunc.find(extractedName) != nameToFunc.end()) {
                        functionsNeedExternal.insert(extractedName);
                        logToIndividualLog(individualLog, "直接匹配到函数 [" + to_string(errorCount) + "]: " + extractedName);
                        foundMatch = true;
                    }

                    // 尝试2: 序号匹配（只针对无名函数）
                    if (!foundMatch) {
                        try {
                            int sequenceNum = std::stoi(extractedName);
                            if (sequenceToFunc.find(sequenceNum) != sequenceToFunc.end()) {
                                Function* unnamedFunc = sequenceToFunc[sequenceNum];
                                string actualName = unnamedFunc->getName().str();
                                functionsNeedExternal.insert(actualName);
                                unnamedMatchCount++;  // 统计无名函数匹配
                                logToIndividualLog(individualLog, "通过序号匹配到无名函数 [" + to_string(errorCount) + "]: " +
                                                actualName + " (序号: " + extractedName + ")");
                                foundMatch = true;
                            }
                        } catch (const std::exception& e) {
                            // 不是数字，继续其他匹配方式
                        }
                    }

                    // 尝试3: 转义序列解码匹配
                    if (!foundMatch) {
                        string decodedName = decodeEscapeSequences(extractedName);
                        if (decodedName != extractedName && nameToFunc.find(decodedName) != nameToFunc.end()) {
                            functionsNeedExternal.insert(decodedName);
                            logToIndividualLog(individualLog, "通过转义解码匹配到函数 [" + to_string(errorCount) + "]: " +
                                            decodedName + " (原始: " + extractedName + ")");
                            foundMatch = true;
                        }
                    }

                    // 尝试4: 预定义的转义映射
                    if (!foundMatch && escapedToOriginal.find(extractedName) != escapedToOriginal.end()) {
                        string originalName = escapedToOriginal[extractedName];
                        if (nameToFunc.find(originalName) != nameToFunc.end()) {
                            functionsNeedExternal.insert(originalName);
                            logToIndividualLog(individualLog, "通过转义映射匹配到函数 [" + to_string(errorCount) + "]: " +
                                            originalName + " (转义: " + extractedName + ")");
                            foundMatch = true;
                        }
                    }

                    // 尝试5: 部分匹配（处理可能的转义序列变体）
                    if (!foundMatch) {
                        for (const auto& pair : nameToFunc) {
                            const string& candidateName = pair.first;

                            // 检查提取的名称是否是候选名称的转义版本
                            string escapedCandidate = candidateName;
                            size_t pos = 0;
                            while ((pos = escapedCandidate.find("§", pos)) != string::npos) {
                                escapedCandidate.replace(pos, 2, "\\C2\\A7");
                                pos += 6;
                            }

                            if (extractedName == escapedCandidate) {
                                functionsNeedExternal.insert(candidateName);
                                logToIndividualLog(individualLog, "通过转义转换匹配到函数 [" + to_string(errorCount) + "]: " +
                                                candidateName + " (转义: " + extractedName + ")");
                                foundMatch = true;
                                break;
                            }
                        }
                    }

                    if (!foundMatch) {
                        logToIndividualLog(individualLog, "无法匹配函数: " + extractedName);

                        // 记录详细信息用于调试
                        size_t debugStart = (ptrPos > 50) ? ptrPos - 50 : 0;
                        size_t debugLength = min(verifyOutput.length() - debugStart, size_t(150));
                        logToIndividualLog(individualLog, "  附近文本: " + verifyOutput.substr(debugStart, debugLength));

                        // 新增：如果是数字但未匹配，可能是无名函数序号超出范围
                        try {
                            int sequenceNum = std::stoi(extractedName);
                            logToIndividualLog(individualLog, "  注意: 序号 " + extractedName + " 可能是无名函数，但未在组内找到对应函数");
                            logToIndividualLog(individualLog, "  组内无名函数序号范围: " +
                                            (unnamedFunctions.empty() ? "无无名函数" :
                                            to_string(unnamedFunctions.front().first) + " - " +
                                            to_string(unnamedFunctions.back().first)));
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
        logToIndividualLog(individualLog, "匹配统计:");
        logToIndividualLog(individualLog, "  总错误数: " + to_string(errorCount));
        logToIndividualLog(individualLog, "  总匹配函数数: " + to_string(functionsNeedExternal.size()));
        logToIndividualLog(individualLog, "  通过序号匹配的无名函数数: " + to_string(unnamedMatchCount));
        logToIndividualLog(individualLog, "  组内无名函数总数: " + to_string(unnamedFunctions.size()));

        // 如果仍然没有找到，但存在链接错误，标记所有组内函数
        if (functionsNeedExternal.empty() && errorCount > 0) {
            logToIndividualLog(individualLog, "检测到 " + to_string(errorCount) + " 个链接错误但匹配失败，标记所有组内函数需要external");
            for (const auto& pair : nameToFunc) {
                functionsNeedExternal.insert(pair.first);
            }
        }

        // 原有的其他错误模式检测（作为补充）
        vector<string> supplementalPatterns = {
            "has private linkage",
            "has internal linkage",
            "visibility not default",
            "linkage not external",
            "invalid linkage",
            "undefined reference"
        };

        for (const string& pattern : supplementalPatterns) {
            if (verifyOutput.find(pattern) != string::npos) {
                logToIndividualLog(individualLog, "发现补充错误模式: " + pattern);

                // 尝试匹配组内函数
                for (const auto& pair : nameToFunc) {
                    const string& funcName = pair.first;
                    if (verifyOutput.find(funcName) != string::npos) {
                        functionsNeedExternal.insert(funcName);
                        logToIndividualLog(individualLog, "通过补充模式匹配到函数: " + funcName);
                    }
                }
            }
        }

        logToIndividualLog(individualLog, "分析完成，找到 " + to_string(functionsNeedExternal.size()) + " 个需要external的函数");

        // 输出详细信息
        if (!functionsNeedExternal.empty()) {
            logToIndividualLog(individualLog, "需要external的函数列表:");
            for (const string& funcName : functionsNeedExternal) {
                Function* func = nameToFunc[funcName];
                if (func) {
                    string seqInfo = functionMap[func].isUnnamed ?
                                    ", 序号: " + to_string(functionMap[func].sequenceNumber) :
                                    ", 有名函数";
                    logToIndividualLog(individualLog, "  " + funcName +
                            " [当前链接: " + getLinkageString(func->getLinkage()) +
                            ", 可见性: " + getVisibilityString(func->getVisibility()) + seqInfo + "]");
                }
            }
        }

        return functionsNeedExternal;
    }

    // 新增：专门处理无名函数的修复方法
    void batchFixFunctionLinkageWithUnnamedSupport(Module& mod, const unordered_set<string>& externalFuncNames) {
        logToFile("批量修复函数链接属性（支持无名函数）...");

        int fixedCount = 0;
        int unnamedFixedCount = 0;  // 统计修复的无名函数数量

        for (auto& func : mod) {
            string funcName = func.getName().str();

            if (externalFuncNames.find(funcName) != externalFuncNames.end()) {
                GlobalValue::LinkageTypes oldLinkage = func.getLinkage();

                // 只有当当前链接不是external时才修改
                if (oldLinkage != GlobalValue::ExternalLinkage) {
                    func.setLinkage(GlobalValue::ExternalLinkage);
                    func.setVisibility(GlobalValue::DefaultVisibility);

                    // 检查是否为无名函数
                    bool isUnnamed = funcName.empty() ||
                                    funcName.find("__unnamed_") == 0 ||
                                    funcName.find(".") == 0 ||
                                    funcName.find("__") == 0 ||
                                    funcName == "d" || funcName == "t" || funcName == "b" ||
                                    (!funcName.empty() && std::all_of(funcName.begin(), funcName.end(), [](char c) { return isdigit(c); }));

                    string funcType = isUnnamed ? "无名函数" : "有名函数";
                    if (isUnnamed) {
                        unnamedFixedCount++;
                    }

                    logToFile("修复" + funcType + ": " + funcName +
                            " [链接: " + getLinkageString(oldLinkage) +
                            " -> " + getLinkageString(func.getLinkage()) + "]");
                    fixedCount++;
                }
            }
        }

        logToFile("批量修复完成，共修复 " + to_string(fixedCount) + " 个函数的链接属性");
        logToFile("其中无名函数: " + to_string(unnamedFixedCount) + " 个");
    }

    // 修改后的重新生成BC文件方法，使用专门的无名函数修复
    bool recreateBCFileWithExternalLinkage(const unordered_set<Function*>& group,
                                        const unordered_set<string>& externalFuncNames,
                                        const string& filename,
                                        int groupIndex) {
        logToFile("重新生成BC文件: " + filename + " (应用external链接)");
        logToFile("需要修复的函数数量: " + to_string(externalFuncNames.size()));

        // 统计无名函数数量
        int unnamedCount = 0;
        for (Function* func : group) {
            if (func && functionMap.count(func) && functionMap[func].isUnnamed) {
                unnamedCount++;
            }
        }
        logToFile("组内无名函数数量: " + to_string(unnamedCount));

        LLVMContext newContext;
        auto newModule = make_unique<Module>(filename, newContext);

        // 复制原始模块的基本属性
        newModule->setTargetTriple(module->getTargetTriple());
        newModule->setDataLayout(module->getDataLayout());

        // 首先创建所有函数（保持原始链接属性）
        unordered_map<string, Function*> newFunctions;

        for (Function* origFunc : group) {
            if (!origFunc) continue;

            string funcName = origFunc->getName().str();

            // 在新建上下文中重新创建函数类型
            vector<Type*> paramTypes;
            for (const auto& arg : origFunc->args()) {
                Type* argType = arg.getType();

                if (argType->isIntegerTy()) {
                    paramTypes.push_back(Type::getIntNTy(newContext, argType->getIntegerBitWidth()));
                } else if (argType->isPointerTy()) {
                    PointerType* ptrType = cast<PointerType>(argType);
                    unsigned addressSpace = ptrType->getAddressSpace();
                    paramTypes.push_back(PointerType::get(newContext, addressSpace));
                } else if (argType->isVoidTy()) {
                    paramTypes.push_back(Type::getVoidTy(newContext));
                } else if (argType->isFloatTy()) {
                    paramTypes.push_back(Type::getFloatTy(newContext));
                } else if (argType->isDoubleTy()) {
                    paramTypes.push_back(Type::getDoubleTy(newContext));
                } else {
                    paramTypes.push_back(PointerType::get(newContext, 0));
                }
            }

            // 确定返回类型
            Type* returnType = origFunc->getReturnType();
            Type* newReturnType;

            if (returnType->isIntegerTy()) {
                newReturnType = Type::getIntNTy(newContext, returnType->getIntegerBitWidth());
            } else if (returnType->isPointerTy()) {
                PointerType* ptrType = cast<PointerType>(returnType);
                unsigned addressSpace = ptrType->getAddressSpace();
                newReturnType = PointerType::get(newContext, addressSpace);
            } else if (returnType->isVoidTy()) {
                newReturnType = Type::getVoidTy(newContext);
            } else if (returnType->isFloatTy()) {
                newReturnType = Type::getFloatTy(newContext);
            } else if (returnType->isDoubleTy()) {
                newReturnType = Type::getDoubleTy(newContext);
            } else {
                newReturnType = PointerType::get(newContext, 0);
            }

            FunctionType* funcType = FunctionType::get(newReturnType, paramTypes, origFunc->isVarArg());

            // 使用原始链接属性创建函数
            Function* newFunc = Function::Create(
                funcType,
                origFunc->getLinkage(),
                origFunc->getName(),
                newModule.get()
            );

            // 复制其他属性
            newFunc->setCallingConv(origFunc->getCallingConv());
            newFunc->setVisibility(origFunc->getVisibility());
            newFunc->setDLLStorageClass(origFunc->getDLLStorageClass());

            newFunctions[funcName] = newFunc;

            // 记录函数类型信息
            string funcTypeInfo = functionMap[origFunc].isUnnamed ?
                                "无名函数 [序号: " + to_string(functionMap[origFunc].sequenceNumber) + "]" :
                                "有名函数";
            logToFile("创建" + funcTypeInfo + ": " + funcName +
                    " [链接: " + getLinkageString(origFunc->getLinkage()) +
                    ", 可见性: " + getVisibilityString(origFunc->getVisibility()) + "]");
        }

        // 使用专门的无名函数修复方法
        batchFixFunctionLinkageWithUnnamedSupport(*newModule, externalFuncNames);

        return writeBitcodeSafely(*newModule, filename);
    }

    // 修改后的拆分方法 - 按照指定数量范围分组
    void splitBCFiles(const string& outputPrefix) {
        log("\n开始拆分BC文件...");

        int fileCount = 0;

        // 步骤1: 首先处理全局变量组
        unordered_set<GlobalVariable*> globals = getGlobalVariables();

        if (!globals.empty()) {
            string filename = outputPrefix + "_group_globals.bc";

            if (createGlobalVariablesBCFile(globals, filename)) {
                log("✓ 全局变量BC文件创建成功: " + filename);
                fileCount++;
            }
        }

        // 步骤2: 处理高入度函数组（包含完整的出度链）
        vector<Function*> highInDegreeFuncs = getHighInDegreeFunctions(500);

        if (!highInDegreeFuncs.empty()) {
            unordered_set<Function*> highInDegreeSet(highInDegreeFuncs.begin(), highInDegreeFuncs.end());
            string filename = outputPrefix + "_group_high_in_degree.bc";

            if (createHighInDegreeBCFile(highInDegreeSet, filename)) {
                unordered_set<Function*> completeHighInDegreeSet = getHighInDegreeWithOutDegreeFunctions(highInDegreeSet);
                if (verifyAndFixBCFile(filename, completeHighInDegreeSet)) {
                    log("✓ 高入度函数BC文件验证通过: " + filename);

                    // 显示高入度函数组的详细信息
                    log("高入度函数组详情:");
                    int highInDegreeCount = 0;
                    int outDegreeCount = 0;
                    for (Function* func : completeHighInDegreeSet) {
                        if (highInDegreeSet.find(func) != highInDegreeSet.end()) {
                            log("  高入度函数: " + functionMap[func].displayName +
                                " [入度: " + to_string(functionMap[func].inDegree) + "]");
                            highInDegreeCount++;
                        } else {
                            log("  出度函数: " + functionMap[func].displayName +
                                " [被高入度函数调用]");
                            outDegreeCount++;
                        }
                    }
                    log("  高入度函数: " + to_string(highInDegreeCount) + " 个");
                    log("  出度函数: " + to_string(outDegreeCount) + " 个");
                } else {
                    logError("✗ 高入度函数BC文件验证失败: " + filename);
                }
                fileCount++;
            }
        }

        // 步骤3: 处理孤立函数组（出度=0且入度=0）
        vector<Function*> isolatedFuncs = getIsolatedFunctions();

        if (!isolatedFuncs.empty()) {
            unordered_set<Function*> isolatedSet(isolatedFuncs.begin(), isolatedFuncs.end());
            string filename = outputPrefix + "_group_isolated.bc";

            if (createIsolatedFunctionsBCFile(isolatedSet, filename)) {
                if (verifyAndFixBCFile(filename, isolatedSet)) {
                    log("✓ 孤立函数BC文件验证通过: " + filename);
                    log("孤立函数组详情:");
                    for (Function* func : isolatedSet) {
                        log("  孤立函数: " + functionMap[func].displayName);
                    }
                    log("  总计: " + to_string(isolatedSet.size()) + " 个孤立函数");
                } else {
                    logError("✗ 孤立函数BC文件验证失败: " + filename);
                }
                fileCount++;
            }
        }

        // 步骤4: 按照指定数量范围分组
        log("开始按照指定数量范围分组...");

        // 获取所有未处理的函数并按总分排序
        vector<pair<Function*, int>> remainingFunctions;
        for (const auto& pair : functionMap) {
            if (!pair.second.isProcessed) {
                int totalScore = pair.second.outDegree + pair.second.inDegree;
                remainingFunctions.emplace_back(pair.first, totalScore);
            }
        }

        // 按总分降序排序
        std::sort(remainingFunctions.begin(), remainingFunctions.end(),
            [](const pair<Function*, int>& a, const pair<Function*, int>& b) -> bool {
                return a.second > b.second;
            });

        log("剩余未处理函数数量: " + to_string(remainingFunctions.size()));

        // 定义分组范围
        vector<pair<int, int>> groupRanges = {
            {0, 100},      // 第3组: 前100个
            {100, 400},    // 第4组: 101-400
            {400, 1000},   // 第5组: 401-1000
            {1000, 2000},  // 第6组: 1001-2000
            {2000, 5000},  // 第7组: 2001-5000
            {5000, -1}     // 第8组: 5001-剩余所有
        };

        int groupIndex = 3; // 从组3开始

        for (const auto& range : groupRanges) {
            int start = range.first;
            int end = range.second;

            // 收集该范围内的函数
            unordered_set<Function*> group;
            for (int i = start; i < remainingFunctions.size(); i++) {
                if (end != -1 && i >= end) break;

                Function* func = remainingFunctions[i].first;
                if (!func || functionMap[func].isProcessed) continue;

                group.insert(func);
            }

            if (group.empty()) {
                log("组 " + to_string(groupIndex) + " 没有函数，跳过");
                groupIndex++;
                continue;
            }

            log("处理组 " + to_string(groupIndex) + "，范围 [" +
                to_string(start) + "-" + (end == -1 ? "剩余所有" : to_string(end)) +
                "]，包含 " + to_string(group.size()) + " 个函数");

            // 创建BC文件
            string filename = outputPrefix + "_group_" + to_string(groupIndex) + ".bc";
            if (createBCFileWithSignatures(group, filename, groupIndex)) {
                // 验证并修复生成的BC文件
                if (verifyAndFixBCFile(filename, group)) {
                    log("✓ BC文件验证通过: " + filename);

                    // 显示组中的前几个函数作为示例
                    string funcList = "  前10个函数: ";
                    int count = 0;
                    for (Function* f : group) {
                        if (count >= 10) break;
                        funcList += functionMap[f].displayName + " ";
                        count++;
                    }
                    if (group.size() > 10) {
                        funcList += "... (共" + to_string(group.size()) + "个)";
                    }
                    log(funcList);
                } else {
                    logError("✗ BC文件验证失败: " + filename);
                }

                fileCount++;
            } else {
                logError("✗ 创建BC文件失败: " + filename);
            }

            groupIndex++;
        }

        totalGroups = fileCount;

        log("\n=== 拆分完成 ===");
        log("共生成 " + to_string(fileCount) + " 个分组BC文件");

        // 统计处理情况
        int processedCount = 0;
        for (const auto& pair : functionMap) {
            if (pair.second.isProcessed) {
                processedCount++;
            }
        }
        log("已处理 " + to_string(processedCount) + " / " + to_string(functionMap.size()) + " 个函数");

        if (processedCount < functionMap.size()) {
            logWarning("警告: 有 " + to_string(functionMap.size() - processedCount) + " 个函数未被处理");

            // 显示未处理的函数
            log("未处理函数列表:");
            int count = 0;
            for (const auto& pair : functionMap) {
                if (!pair.second.isProcessed) {
                    log("  " + pair.second.displayName +
                        " [出度: " + to_string(pair.second.outDegree) +
                        ", 入度: " + to_string(pair.second.inDegree) + "]");
                    count++;
                    if (count >= 20) {  // 限制输出数量
                        log("  ... (还有 " + to_string(functionMap.size() - processedCount - count) + " 个函数)");
                        break;
                    }
                }
            }
        }
    }

    void logWarning(const string& message) {
        if (logFile.is_open()) {
            logFile << "[WARNING] " + message << endl;
        }
        cout << "[WARNING] " + message << endl;
    }

    // 新增：验证并修复BC文件的方法
        // 添加独立日志支持
    bool verifyAndFixBCFile(const string& filename, const unordered_set<Function*>& expectedGroup) {
        // 创建独立日志文件
        ofstream individualLog = createIndividualLogFile(filename, "_verify");

        logToIndividualLog(individualLog, "开始验证并修复BC文件: " + filename, true);

        LLVMContext verifyContext;
        SMDiagnostic err;
        auto loadedModule = parseIRFile(filename, err, verifyContext);

        if (!loadedModule) {
            logToIndividualLog(individualLog, "错误: 无法加载验证的BC文件: " + filename, true);
            err.print("BCVerifier", errs());
            individualLog.close();
            return false;
        }

        // 检查1: 验证模块完整性
        string verifyResult;
        raw_string_ostream rso(verifyResult);
        bool moduleValid = !verifyModule(*loadedModule, &rso);

        if (moduleValid) {
            logToIndividualLog(individualLog, "✓ 模块完整性验证通过", true);

            // 检查2: 验证函数数量
            int functionCount = 0;
            unordered_set<string> actualFunctionNames;

            for (auto& func : *loadedModule) {
                functionCount++;
                actualFunctionNames.insert(func.getName().str());
            }

            logToIndividualLog(individualLog, "实际函数数量: " + to_string(functionCount));
            logToIndividualLog(individualLog, "期望函数数量: " + to_string(expectedGroup.size()));

            if (functionCount != expectedGroup.size()) {
                logToIndividualLog(individualLog, "错误: 函数数量不匹配: 期望 " +
                                to_string(expectedGroup.size()) + ", 实际 " + to_string(functionCount), true);
                individualLog.close();
                return false;
            }

            logToIndividualLog(individualLog, "✓ 函数数量验证通过: " + to_string(functionCount) + " 个函数", true);

            // 检查3: 验证函数签名完整性
            bool allSignaturesValid = true;
            unordered_set<string> expectedNames;

            for (Function* expectedFunc : expectedGroup) {
                if (expectedFunc) {
                    expectedNames.insert(expectedFunc->getName().str());
                }
            }

            for (auto& func : *loadedModule) {
                string funcName = func.getName().str();

                if (expectedNames.find(funcName) == expectedNames.end()) {
                    logToIndividualLog(individualLog, "警告: 发现未预期的函数: " + funcName);
                    allSignaturesValid = false;
                    continue;
                }

                if (!verifyFunctionSignature(&func)) {
                    logToIndividualLog(individualLog, "错误: 函数签名不完整: " + funcName);
                    allSignaturesValid = false;
                }
            }

            if (!allSignaturesValid) {
                logToIndividualLog(individualLog, "错误: 函数签名验证失败", true);
                individualLog.close();
                return false;
            }

            logToIndividualLog(individualLog, "✓ 函数签名验证通过", true);
            individualLog.close();
            return true;
        } else {
            // 模块验证失败，尝试分析错误并修复
            logToIndividualLog(individualLog, "模块验证失败，尝试分析错误并修复...", true);
            logToIndividualLog(individualLog, "验证错误详情: " + rso.str());

            // 特别记录无名函数信息用于调试
            logToIndividualLog(individualLog, "组内无名函数信息:");
            for (Function* func : expectedGroup) {
                if (func && functionMap.count(func) && functionMap[func].isUnnamed) {
                    const FunctionInfo& info = functionMap[func];
                    logToIndividualLog(individualLog, "  无名函数: " + info.displayName +
                                    " [序号: " + to_string(info.sequenceNumber) +
                                    ", 实际名称: " + func->getName().str() + "]");
                }
            }

            // 分析错误信息，识别需要external链接的函数
            unordered_set<string> externalFuncNames = analyzeVerifierErrorsWithLog(rso.str(), expectedGroup, individualLog);

            if (!externalFuncNames.empty()) {
                logToIndividualLog(individualLog, "发现需要修复的函数数量: " + to_string(externalFuncNames.size()), true);

                // 重新生成BC文件，将需要external的函数设置为external链接
                string fixedFilename = filename + ".fixed.bc";
                if (recreateBCFileWithExternalLinkage(expectedGroup, externalFuncNames, fixedFilename, -1)) {
                    logToIndividualLog(individualLog, "重新生成修复后的BC文件: " + fixedFilename, true);

                    // 验证修复后的文件
                    if (quickValidateBCFileWithLog(fixedFilename, individualLog)) {
                        logToIndividualLog(individualLog, "✓ 修复后的BC文件验证通过", true);

                        // 替换原文件
                        sys::fs::remove(filename);
                        sys::fs::rename(fixedFilename, filename);
                        logToIndividualLog(individualLog, "已替换原文件: " + filename, true);

                        individualLog.close();
                        return true;
                    } else {
                        logToIndividualLog(individualLog, "✗ 修复后的BC文件仍然验证失败", true);
                        individualLog.close();
                        return false;
                    }
                } else {
                    logToIndividualLog(individualLog, "✗ 重新生成BC文件失败", true);
                    individualLog.close();
                    return false;
                }
            } else {
                logToIndividualLog(individualLog, "无法识别需要修复的具体函数", true);
                individualLog.close();
                return false;
            }
        }
    }

    // 验证函数签名的完整性
    bool verifyFunctionSignature(Function* func) {
        if (!func) return false;

        FunctionType* funcType = func->getFunctionType();
        if (!funcType) return false;

        Type* returnType = funcType->getReturnType();
        if (!returnType) return false;

        for (Type* paramType : funcType->params()) {
            if (!paramType) return false;
        }

        return true;
    }


    // 新增：带独立日志的快速验证方法
    bool quickValidateBCFileWithLog(const string& filename, ofstream& individualLog) {
        logToIndividualLog(individualLog, "快速验证BC文件: " + filename);

        LLVMContext tempContext;
        SMDiagnostic err;
        auto testModule = parseIRFile(filename, err, tempContext);

        if (!testModule) {
            logToIndividualLog(individualLog, "错误: 无法加载BC文件进行快速验证");
            return false;
        }

        string verifyResult;
        raw_string_ostream rso(verifyResult);
        bool moduleValid = !verifyModule(*testModule, &rso);

        if (moduleValid) {
            logToIndividualLog(individualLog, "✓ 快速验证通过");
        } else {
            logToIndividualLog(individualLog, "✗ 快速验证失败");
            logToIndividualLog(individualLog, "验证错误: " + rso.str());
        }

        return moduleValid;
    }

    // 修改详细分析BC文件内容方法，添加独立日志支持
    void analyzeBCFileContent(const string& filename) {
        // 创建独立日志文件
        ofstream individualLog = createIndividualLogFile(filename, "_analysis");

        logToIndividualLog(individualLog, "开始详细分析BC文件内容: " + filename, true);

        LLVMContext tempContext;
        SMDiagnostic err;
        auto testModule = parseIRFile(filename, err, tempContext);

        if (!testModule) {
            logToIndividualLog(individualLog, "错误: 无法分析BC文件内容: " + filename, true);
            individualLog.close();
            return;
        }

        int totalFunctions = 0;
        int declarationFunctions = 0;
        int definitionFunctions = 0;
        int globalVariables = 0;

        // 统计全局变量
        logToIndividualLog(individualLog, "全局变量列表:");
        for (auto& global : testModule->globals()) {
            globalVariables++;
            logToIndividualLog(individualLog, "  " + global.getName().str() +
                            " [链接: " + getLinkageString(global.getLinkage()) + "]");
        }

        logToIndividualLog(individualLog, "模块中的函数列表:");
        for (auto& func : *testModule) {
            totalFunctions++;
            string funcType = func.isDeclaration() ? "声明" : "定义";
            string linkageStr = getLinkageString(func.getLinkage());
            string visibilityStr = getVisibilityString(func.getVisibility());

            logToIndividualLog(individualLog, "  " + func.getName().str() +
                            " [" + funcType +
                            ", 链接:" + linkageStr +
                            ", 可见性:" + visibilityStr + "]");

            if (func.isDeclaration()) {
                declarationFunctions++;
            } else {
                definitionFunctions++;
            }
        }

        logToIndividualLog(individualLog, "统计结果:", true);
        logToIndividualLog(individualLog, "  全局变量: " + to_string(globalVariables), true);
        logToIndividualLog(individualLog, "  总函数数: " + to_string(totalFunctions), true);
        logToIndividualLog(individualLog, "  声明函数: " + to_string(declarationFunctions), true);
        logToIndividualLog(individualLog, "  定义函数: " + to_string(definitionFunctions), true);

        individualLog.close();
    }

    // 修改批量验证方法，为每个文件创建独立日志
    void validateAllBCFiles(const string& outputPrefix) {
        log("\n=== 开始批量验证所有BC文件 ===");

        int totalFiles = 0;
        int validFiles = 0;

        // 检查全局变量组
        string globalsFilename = outputPrefix + "_group_globals.bc";
        if (sys::fs::exists(globalsFilename)) {
            totalFiles++;
            ofstream individualLog = createIndividualLogFile(globalsFilename, "_validation");
            if (quickValidateBCFileWithLog(globalsFilename, individualLog)) {
                logToIndividualLog(individualLog, "✓ 快速验证通过", true);
                validFiles++;
            } else {
                logToIndividualLog(individualLog, "✗ 快速验证失败", true);
            }
            individualLog.close();
        }

        // 检查高入度函数组
        string highInDegreeFilename = outputPrefix + "_group_high_in_degree.bc";
        if (sys::fs::exists(highInDegreeFilename)) {
            totalFiles++;
            ofstream individualLog = createIndividualLogFile(highInDegreeFilename, "_validation");
            if (quickValidateBCFileWithLog(highInDegreeFilename, individualLog)) {
                logToIndividualLog(individualLog, "✓ 快速验证通过", true);
                validFiles++;
            } else {
                logToIndividualLog(individualLog, "✗ 快速验证失败", true);
            }
            individualLog.close();
        }

        // 检查孤立函数组
        string isolatedFilename = outputPrefix + "_group_isolated.bc";
        if (sys::fs::exists(isolatedFilename)) {
            totalFiles++;
            ofstream individualLog = createIndividualLogFile(isolatedFilename, "_validation");
            if (quickValidateBCFileWithLog(isolatedFilename, individualLog)) {
                logToIndividualLog(individualLog, "✓ 快速验证通过", true);
                validFiles++;
            } else {
                logToIndividualLog(individualLog, "✗ 快速验证失败", true);
            }
            individualLog.close();
        }

        // 检查新的分组范围（组3-8）
        for (int i = 3; i <= 8; i++) {
            string filename = outputPrefix + "_group_" + to_string(i) + ".bc";

            if (!sys::fs::exists(filename)) {
                continue;
            }

            totalFiles++;
            ofstream individualLog = createIndividualLogFile(filename, "_validation");

            if (quickValidateBCFileWithLog(filename, individualLog)) {
                logToIndividualLog(individualLog, "✓ 快速验证通过", true);
                validFiles++;
            } else {
                logToIndividualLog(individualLog, "✗ 快速验证失败", true);
            }
            individualLog.close();
        }

        log("\n=== 批量验证结果 ===");
        log("总计文件: " + to_string(totalFiles));
        log("有效文件: " + to_string(validFiles));
        log("无效文件: " + to_string(totalFiles - validFiles));

        if (validFiles == totalFiles && totalFiles > 0) {
            log("✓ 所有BC文件验证通过！");
        } else if (totalFiles > 0) {
            logError("✗ 部分BC文件验证失败");
        } else {
            log("未找到BC文件进行验证");
        }

        // 记录验证摘要到主日志
        logToFile("批量验证完成: " + to_string(validFiles) + "/" + to_string(totalFiles) + " 个文件验证通过");
    }

    // 更新后的生成分组报告方法
    void generateGroupReport(const string& outputPrefix) {
        string reportFile = outputPrefix + "_group_report.txt";
        ofstream report(reportFile);

        if (!report.is_open()) {
            logError("无法创建分组报告文件: " + reportFile);
            return;
        }

        report << "=== BC文件分组报告 ===" << endl;
        report << "总函数数: " << functionMap.size() << endl;
        report << "总分组数: " << totalGroups << endl << endl;

        // 按组统计
        unordered_map<int, vector<string>> groupFunctions;
        int ungroupedCount = 0;

        for (const auto& pair : functionMap) {
            const FunctionInfo& info = pair.second;
            if (info.groupIndex >= 0) {
                groupFunctions[info.groupIndex].push_back(info.displayName +
                    " (入度:" + to_string(info.inDegree) +
                    ", 出度:" + to_string(info.outDegree) + ")");
            } else {
                ungroupedCount++;
            }
        }

        report << "=== 分组详情 ===" << endl;

        // 全局变量组（组0）
        report << "组 0 (全局变量):" << endl;
        unordered_set<GlobalVariable*> globals = getGlobalVariables();
        for (GlobalVariable* global : globals) {
            if (global) {
                report << "  " << global->getName().str() << endl;
            }
        }
        report << endl;

        // 高入度函数组（组1）
        if (groupFunctions.count(1)) {
            report << "组 1 (高入度函数):" << endl;
            for (const string& funcName : groupFunctions[1]) {
                report << "  " << funcName << endl;
            }
            report << endl;
        }

        // 孤立函数组（组2）
        if (groupFunctions.count(2)) {
            report << "组 2 (孤立函数):" << endl;
            for (const string& funcName : groupFunctions[2]) {
                report << "  " << funcName << endl;
            }
            report << endl;
        }

        // 新的分组策略（组3-8）
        vector<string> groupDescriptions = {
            "前100个函数",
            "101-400个函数",
            "401-1000个函数",
            "1001-2000个函数",
            "2001-5000个函数",
            "5001-剩余所有函数"
        };

        for (int i = 3; i <= 8; i++) {
            if (groupFunctions.count(i)) {
                report << "组 " << i << " (" << groupDescriptions[i-3] << "):" << endl;
                report << "  函数数量: " << groupFunctions[i].size() << endl;

                // 只显示前20个函数作为示例
                int showCount = min(20, (int)groupFunctions[i].size());
                for (int j = 0; j < showCount; j++) {
                    report << "  " << groupFunctions[i][j] << endl;
                }
                if (groupFunctions[i].size() > showCount) {
                    report << "  ... (还有 " << (groupFunctions[i].size() - showCount) << " 个函数)" << endl;
                }
                report << endl;
            }
        }

        if (ungroupedCount > 0) {
            report << "=== 未分组函数 ===" << endl;
            report << "未分组函数数量: " << ungroupedCount << endl;
            int showCount = min(50, ungroupedCount);
            int count = 0;
            for (const auto& pair : functionMap) {
                if (pair.second.groupIndex < 0) {
                    report << "  " << pair.second.displayName <<
                        " (入度:" << pair.second.inDegree <<
                        ", 出度:" << pair.second.outDegree << ")" << endl;
                    count++;
                    if (count >= showCount) break;
                }
            }
            if (ungroupedCount > showCount) {
                report << "  ... (还有 " << (ungroupedCount - showCount) << " 个函数)" << endl;
            }
        }

        report.close();
        log("分组报告已生成: " + reportFile);
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "用法: " << argv[0] << " <输入.bc> <输出前缀>" << endl;
        return 1;
    }

    string inputFile = argv[1];
    string outputPrefix = argv[2];

    cout << "BC文件拆分工具启动..." << endl;
    cout << "输入文件: " << inputFile << endl;
    cout << "输出前缀: " << outputPrefix << endl;

    try {
        BCModuleSplitter splitter;

        if (!splitter.loadBCFile(inputFile)) {
            cerr << "无法加载BC文件: " << inputFile << endl;
            return 1;
        }

        splitter.analyzeFunctions();
        splitter.printFunctionInfo();
        splitter.splitBCFiles(outputPrefix);

        // 执行批量验证
        splitter.validateAllBCFiles(outputPrefix);

        // 生成分组报告
        splitter.generateGroupReport(outputPrefix);

        // 详细分析生成的BC文件内容
        string globalsFilename = outputPrefix + "_group_globals.bc";
        if (sys::fs::exists(globalsFilename)) {
            splitter.analyzeBCFileContent(globalsFilename);
        }

        string highInDegreeFilename = outputPrefix + "_group_high_in_degree.bc";
        if (sys::fs::exists(highInDegreeFilename)) {
            splitter.analyzeBCFileContent(highInDegreeFilename);
        }

        string isolatedFilename = outputPrefix + "_group_isolated.bc";
        if (sys::fs::exists(isolatedFilename)) {
            splitter.analyzeBCFileContent(isolatedFilename);
        }

        for (int i = 3; i <= 12; i++) {
            string filename = outputPrefix + "_group_" + to_string(i) + ".bc";
            if (sys::fs::exists(filename)) {
                splitter.analyzeBCFileContent(filename);
            }
        }

        splitter.log("程序执行完成");
    } catch (const std::exception& e) {
        cerr << "程序执行过程中发生异常: " << e.what() << endl;
        return 1;
    }

    return 0;
}