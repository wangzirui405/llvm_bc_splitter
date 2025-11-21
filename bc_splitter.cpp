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
#include "llvm/IRReader/IRReader.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;
using namespace std;

// 函数信息结构体
struct FunctionInfo {
    string name;
    string displayName;
    Function* funcPtr = nullptr;
    int outDegree = 0;
    int inDegree = 0;
    unordered_set<Function*> calledFunctions;
    unordered_set<Function*> callerFunctions;
    
    FunctionInfo() = default;
    
    FunctionInfo(Function* func) {
        funcPtr = func;
        name = func->getName().str();
        
        // 处理无名函数
        if (name.empty() || 
            name.find("__unnamed_") == 0 || 
            name.find(".") == 0 || 
            name.find("__") == 0 || 
            name == "d" || name == "t" || name == "b") {
            displayName = "unnamed_" + to_string(reinterpret_cast<uintptr_t>(func));
        } else {
            displayName = name;
        }
    }
};

class BCModuleSplitter {
private:
    LLVMContext context;
    unique_ptr<Module> module;
    unordered_map<Function*, FunctionInfo> functionMap;
    vector<Function*> functionPtrs;
    ofstream logFile;

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
        
        // 收集所有函数
        for (Function& func : *module) {
            if (func.isDeclaration()) continue;
            
            auto result = functionMap.emplace(&func, FunctionInfo(&func));
            if (result.second) {
                functionPtrs.push_back(&func);
            }
        }

        log("收集到 " + to_string(functionMap.size()) + " 个函数定义");

        // 分析调用关系
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
            logToFile("函数: " + info.displayName + 
                " [出度: " + to_string(info.outDegree) + 
                ", 入度: " + to_string(info.inDegree) + "]");
        }
    }

    vector<Function*> getTopFunctions(int topN) {
        vector<pair<Function*, int>> scores;
        
        for (const auto& pair : functionMap) {
            int totalScore = pair.second.outDegree + pair.second.inDegree;
            scores.emplace_back(pair.first, totalScore);
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

    unordered_set<Function*> getFunctionGroup(Function* func) {
        unordered_set<Function*> group;
        if (!func || functionMap.find(func) == functionMap.end()) {
            logError("尝试获取无效函数的组");
            return group;
        }
        
        const FunctionInfo& info = functionMap[func];
        
        group.insert(func);
        
        for (Function* called : info.calledFunctions) {
            if (called && functionMap.count(called)) {
                group.insert(called);
            }
        }
        
        for (Function* caller : info.callerFunctions) {
            if (caller && functionMap.count(caller)) {
                group.insert(caller);
            }
        }
        
        return group;
    }

    // 修复的替代方案：兼容 LLVM 21.1.5 API
    bool createBCFileWithSignatures(const unordered_set<Function*>& group, const string& filename) {
        logToFile("创建带签名的BC文件: " + filename);
        
        // 使用全新的上下文避免任何污染
        LLVMContext newContext;
        auto newModule = make_unique<Module>(filename, newContext);
        
        // 复制原始模块的基本属性
        newModule->setTargetTriple(module->getTargetTriple());
        newModule->setDataLayout(module->getDataLayout());
        
        // 为每个函数创建签名
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
                    // 对于指针类型，创建通用指针类型
                    PointerType* ptrType = cast<PointerType>(argType);
                    unsigned addressSpace = ptrType->getAddressSpace();
                    // 使用 i8* 作为通用指针类型
                    paramTypes.push_back(PointerType::get(newContext, addressSpace));
                } else if (argType->isVoidTy()) {
                    paramTypes.push_back(Type::getVoidTy(newContext));
                } else if (argType->isFloatTy()) {
                    paramTypes.push_back(Type::getFloatTy(newContext));
                } else if (argType->isDoubleTy()) {
                    paramTypes.push_back(Type::getDoubleTy(newContext));
                } else {
                    // 对于其他类型，使用 i8* 作为通用类型
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
            
            // 创建函数
            Function* newFunc = Function::Create(
                funcType,
                GlobalValue::ExternalLinkage, // 使用外部链接避免复杂复制
                origFunc->getName(),
                newModule.get()
            );
            
            // 复制基本属性
            newFunc->setCallingConv(origFunc->getCallingConv());
            newFunc->setVisibility(origFunc->getVisibility());
            newFunc->setDLLStorageClass(origFunc->getDLLStorageClass());
            
            logToFile("复制函数签名: " + origFunc->getName().str());
        }
        
        // 写入bitcode
        return writeBitcodeSafely(*newModule, filename);
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
            // 使用最简单的bitcode写入选项
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

    // 简化的拆分方法
    void splitBCFiles(const string& outputPrefix) {
        vector<Function*> topFunctions = getTopFunctions(10);
        unordered_set<Function*> processedFunctions;
        int fileCount = 0;

        log("\n开始拆分BC文件...");

        for (Function* func : topFunctions) {
            if (fileCount >= 10) break;
            
            if (!func) {
                logError("遇到空函数指针");
                continue;
            }
            
            if (processedFunctions.find(func) != processedFunctions.end()) {
                continue;
            }

            unordered_set<Function*> group = getFunctionGroup(func);
            if (group.empty()) {
                continue;
            }

            // 检查是否已处理
            bool alreadyProcessed = true;
            for (Function* f : group) {
                if (processedFunctions.find(f) == processedFunctions.end()) {
                    alreadyProcessed = false;
                    break;
                }
            }
            if (alreadyProcessed) {
                continue;
            }

            log("处理组 " + to_string(fileCount) + "，包含 " + to_string(group.size()) + " 个函数");

            // 创建BC文件
            string filename = outputPrefix + "_group_" + to_string(fileCount) + ".bc";
            if (createBCFileWithSignatures(group, filename)) {
                // 验证生成的BC文件
                if (verifyBCFile(filename, group)) {
                    log("✓ BC文件验证通过: " + filename);
                } else {
                    logError("✗ BC文件验证失败: " + filename);
                    // 可以选择删除验证失败的文件
                    // sys::fs::remove(filename);
                }
                
                // 标记已处理
                for (Function* f : group) {
                    if (f) processedFunctions.insert(f);
                }
                
                // 显示组中的函数
                string funcList = "  包含函数: ";
                int count = 0;
                for (Function* f : group) {
                    if (count >= 5) {
                        funcList += "...";
                        break;
                    }
                    funcList += functionMap[f].displayName + " ";
                    count++;
                }
                logToFile(funcList);
                
                fileCount++;
            }
        }

        log("\n=== 拆分完成 ===");
        log("共生成 " + to_string(fileCount) + " 个分组BC文件");
        log("已处理 " + to_string(processedFunctions.size()) + " / " + to_string(functionMap.size()) + " 个函数");
    }

    // 验证BC文件的方法 - 修复版本
    bool verifyBCFile(const string& filename, const unordered_set<Function*>& expectedGroup) {
        logToFile("验证BC文件: " + filename);
        
        LLVMContext verifyContext;
        SMDiagnostic err;
        auto loadedModule = parseIRFile(filename, err, verifyContext);
        
        if (!loadedModule) {
            logError("无法加载验证的BC文件: " + filename);
            err.print("BCVerifier", errs());
            return false;
        }
        
        // 检查1: 验证模块完整性
        string verifyResult;
        raw_string_ostream rso(verifyResult);
        bool moduleValid = !verifyModule(*loadedModule, &rso);
        
        if (!moduleValid) {
            logError("模块验证失败: " + filename);
            logToFile("验证错误: " + rso.str());
            return false;
        }
        
        logToFile("✓ 模块完整性验证通过");
        
        // 检查2: 验证函数数量 - 修复统计方法
        int functionCount = 0;
        unordered_set<string> actualFunctionNames;
        
        for (auto& func : *loadedModule) {
            // 统计所有函数，包括声明
            functionCount++;
            actualFunctionNames.insert(func.getName().str());
            
            // 调试信息：显示前几个函数名
            if (functionCount <= 5) {
                logToFile("  找到函数: " + func.getName().str() + 
                    " [声明: " + string(func.isDeclaration() ? "是" : "否") + "]");
            }
        }
        
        logToFile("实际函数数量: " + to_string(functionCount));
        logToFile("期望函数数量: " + to_string(expectedGroup.size()));
        
        if (functionCount != expectedGroup.size()) {
            logError("函数数量不匹配: 期望 " + to_string(expectedGroup.size()) + 
                    ", 实际 " + to_string(functionCount));
            
            // 详细调试信息：检查缺失的函数
            int missingCount = 0;
            for (Function* expectedFunc : expectedGroup) {
                if (expectedFunc) {
                    string expectedName = expectedFunc->getName().str();
                    if (actualFunctionNames.find(expectedName) == actualFunctionNames.end()) {
                        if (missingCount < 10) { // 只显示前10个缺失的函数
                            logToFile("  缺失函数: " + expectedName);
                        }
                        missingCount++;
                    }
                }
            }
            
            if (missingCount > 0) {
                logToFile("共缺失 " + to_string(missingCount) + " 个函数");
            }
            
            // 检查多余的函数
            unordered_set<string> expectedNames;
            for (Function* expectedFunc : expectedGroup) {
                if (expectedFunc) {
                    expectedNames.insert(expectedFunc->getName().str());
                }
            }
            
            int extraCount = 0;
            for (const string& actualName : actualFunctionNames) {
                if (expectedNames.find(actualName) == expectedNames.end()) {
                    if (extraCount < 10) { // 只显示前10个多余的函数
                        logToFile("  多余函数: " + actualName);
                    }
                    extraCount++;
                }
            }
            
            if (extraCount > 0) {
                logToFile("共多余 " + to_string(extraCount) + " 个函数");
            }
            
            return false;
        }
        
        logToFile("✓ 函数数量验证通过: " + to_string(functionCount) + " 个函数");
        
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
            
            // 检查函数是否在预期组中
            if (expectedNames.find(funcName) == expectedNames.end()) {
                logToFile("发现未预期的函数: " + funcName);
                allSignaturesValid = false;
                continue;
            }
            
            // 检查函数签名完整性
            if (!verifyFunctionSignature(&func)) {
                logToFile("函数签名不完整: " + funcName);
                allSignaturesValid = false;
            }
        }
        
        if (!allSignaturesValid) {
            logToFile("函数签名验证失败");
            return false;
        }
        
        logToFile("✓ 函数签名验证通过");
        
        // 检查4: 验证bitcode可读性
        if (!verifyBitcodeReadability(filename)) {
            logToFile("Bitcode可读性验证失败");
            return false;
        }
        
        logToFile("✓ Bitcode可读性验证通过");
        
        return true;
    }
    
    // 验证函数签名的完整性
    bool verifyFunctionSignature(Function* func) {
        if (!func) return false;
        
        // 检查函数类型
        FunctionType* funcType = func->getFunctionType();
        if (!funcType) return false;
        
        // 检查返回类型
        Type* returnType = funcType->getReturnType();
        if (!returnType) return false;
        
        // 检查参数类型
        for (Type* paramType : funcType->params()) {
            if (!paramType) return false;
        }
        
        return true;
    }
    
    // 验证bitcode文件的可读性
    bool verifyBitcodeReadability(const string& filename) {
        ErrorOr<unique_ptr<MemoryBuffer>> fileOrErr = MemoryBuffer::getFile(filename);
        if (error_code ec = fileOrErr.getError()) {
            logToFile("无法读取bitcode文件: " + ec.message());
            return false;
        }
        
        MemoryBufferRef bufferRef = (*fileOrErr)->getMemBufferRef();
        LLVMContext tempContext;
        
        Expected<unique_ptr<Module>> moduleOrErr = parseBitcodeFile(bufferRef, tempContext);
        if (!moduleOrErr) {
            logToFile("解析bitcode文件失败");
            handleAllErrors(moduleOrErr.takeError(), [&](ErrorInfoBase& EIB) {
                logToFile("Bitcode解析错误: " + EIB.message());
            });
            return false;
        }
        
        return true;
    }
    
    // 批量验证所有生成的BC文件
    void validateAllBCFiles(const string& outputPrefix) {
        log("\n=== 开始批量验证所有BC文件 ===");
        
        int totalFiles = 0;
        int validFiles = 0;
        
        for (int i = 0; i < 10; i++) {
            string filename = outputPrefix + "_group_" + to_string(i) + ".bc";
            
            if (!sys::fs::exists(filename)) {
                continue; // 文件不存在，跳过
            }
            
            totalFiles++;
            
            if (quickValidateBCFile(filename)) {
                logToFile("✓ 快速验证通过: " + filename);
                validFiles++;
            } else {
                logToFile("✗ 快速验证失败: " + filename);
            }
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
    }
    
    // 快速验证BC文件
    bool quickValidateBCFile(const string& filename) {
        LLVMContext tempContext;
        SMDiagnostic err;
        auto testModule = parseIRFile(filename, err, tempContext);
        
        if (!testModule) {
            return false;
        }
        
        // 快速完整性检查
        string verifyResult;
        raw_string_ostream rso(verifyResult);
        bool moduleValid = !verifyModule(*testModule, &rso);
        
        return moduleValid;
    }
    
    // 新增：详细分析BC文件内容
    void analyzeBCFileContent(const string& filename) {
        logToFile("\n详细分析BC文件: " + filename);
        
        LLVMContext tempContext;
        SMDiagnostic err;
        auto testModule = parseIRFile(filename, err, tempContext);
        
        if (!testModule) {
            logToFile("无法分析BC文件内容: " + filename);
            return;
        }
        
        int totalFunctions = 0;
        int declarationFunctions = 0;
        int definitionFunctions = 0;
        
        logToFile("模块中的函数列表:");
        for (auto& func : *testModule) {
            totalFunctions++;
            string funcType = func.isDeclaration() ? "声明" : "定义";
            logToFile("  " + func.getName().str() + " [" + funcType + "]");
            
            if (func.isDeclaration()) {
                declarationFunctions++;
            } else {
                definitionFunctions++;
            }
        }
        
        logToFile("统计结果:");
        logToFile("  总函数数: " + to_string(totalFunctions));
        logToFile("  声明函数: " + to_string(declarationFunctions));
        logToFile("  定义函数: " + to_string(definitionFunctions));
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
        
        // 详细分析生成的BC文件内容
        for (int i = 0; i < 10; i++) {
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