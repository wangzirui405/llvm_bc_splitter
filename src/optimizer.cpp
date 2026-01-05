// optimizer.cpp
#include "optimizer.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils.h"
#include <fstream>
#include <iostream>
#include <string>

namespace custom {

// ExampleCustomPass 实现 - 一个实际有用的自定义优化
llvm::PreservedAnalyses ExampleCustomPass::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    bool Changed = false;

    // 示例1: 删除未使用的全局变量
    for (auto it = M.global_begin(); it != M.global_end();) {
        llvm::GlobalVariable *GV = &*it++;
        if (GV->use_empty() && GV->hasLocalLinkage() && !GV->isDeclaration()) {
            GV->eraseFromParent();
            Changed = true;
        }
    }

    // 示例2: 简单的常量传播增强
    for (auto &F : M) {
        if (F.isDeclaration())
            continue;

        for (auto &BB : F) {
            for (auto &I : BB) {
                // 优化模式: 转换乘2为左移1位
                if (auto *BinOp = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
                    if (BinOp->getOpcode() == llvm::Instruction::Mul) {
                        if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(BinOp->getOperand(0))) {
                            if (CI->getValue() == 2) {
                                // mul 2, X -> shl X, 1
                                llvm::IRBuilder<> Builder(&I);
                                llvm::Value *Shl =
                                    Builder.CreateShl(BinOp->getOperand(1), llvm::ConstantInt::get(I.getType(), 1));
                                I.replaceAllUsesWith(Shl);
                                Changed = true;
                            }
                        } else if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(BinOp->getOperand(1))) {
                            if (CI->getValue() == 2) {
                                // mul X, 2 -> shl X, 1
                                llvm::IRBuilder<> Builder(&I);
                                llvm::Value *Shl =
                                    Builder.CreateShl(BinOp->getOperand(0), llvm::ConstantInt::get(I.getType(), 1));
                                I.replaceAllUsesWith(Shl);
                                Changed = true;
                            }
                        }
                    }
                }

                // 示例3: 删除冗余的位操作
                if (auto *BinOp = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
                    if (BinOp->getOpcode() == llvm::Instruction::And) {
                        // and X, 0 -> 0
                        if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(BinOp->getOperand(0))) {
                            if (CI->isZero()) {
                                I.replaceAllUsesWith(llvm::ConstantInt::get(I.getType(), 0));
                                Changed = true;
                            }
                        }
                        if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(BinOp->getOperand(1))) {
                            if (CI->isZero()) {
                                I.replaceAllUsesWith(llvm::ConstantInt::get(I.getType(), 0));
                                Changed = true;
                            }
                        }
                    }
                }
            }
        }
    }

    // 示例4: 清理调试信息（可选）
    if (Config.enable_debug) {
        logger.logToFile("ExampleCustomPass: Made " + std::string(Changed ? "changes" : "no changes") + " to module " +
                         M.getName().str());
    }

    return Changed ? llvm::PreservedAnalyses::none() : llvm::PreservedAnalyses::all();
}

// CustomOptimizer 实现
CustomOptimizer::CustomOptimizer(const custom::OptimizerConfig &Config) : Config(Config) {
    // 创建分析管理器
    LAM = std::make_unique<llvm::LoopAnalysisManager>();
    FAM = std::make_unique<llvm::FunctionAnalysisManager>();
    CGAM = std::make_unique<llvm::CGSCCAnalysisManager>();
    MAM = std::make_unique<llvm::ModuleAnalysisManager>();
}

void CustomOptimizer::initializeAnalysisManagers(llvm::Module &M) {
    // 清空之前的分析结果
    LAM->clear();
    FAM->clear();
    CGAM->clear();
    MAM->clear();

    // 注册标准分析
    PB.registerModuleAnalyses(*MAM);
    PB.registerCGSCCAnalyses(*CGAM);
    PB.registerFunctionAnalyses(*FAM);
    PB.registerLoopAnalyses(*LAM);
    PB.crossRegisterProxies(*LAM, *FAM, *CGAM, *MAM);
}

void CustomOptimizer::addPass(std::unique_ptr<CustomPass> Pass, bool before_o2) {
    if (before_o2) {
        PrePasses.push_back(std::move(Pass));
    } else {
        PostPasses.push_back(std::move(Pass));
    }
}

void CustomOptimizer::addLambdaPass(CustomPassFunc Func, const std::string &Name, bool before_o2) {
    addPass(std::make_unique<LambdaCustomPass>(std::move(Func), Name), before_o2);
}

void CustomOptimizer::clearPasses() {
    PrePasses.clear();
    PostPasses.clear();
}

bool CustomOptimizer::runOptimization(llvm::Module &M) {
    try {
        // 初始化分析管理器
        initializeAnalysisManagers(M);

        // 构建 ModulePassManager
        llvm::ModulePassManager MPM;

        // 阶段1: 在 O2 之前运行的自定义 Pass
        if (Config.run_before_o2) {
            for (const auto &Pass : PrePasses) {
                if (Config.enable_debug) {
                    logger.logToFile("Running pre-O2 pass: " + Pass->getName());
                }

                // 将自定义 Pass 包装到适配器中
                // MPM.addPass();
            }
        }

        // 阶段2: LLVM O2 优化管道
        if (Config.enable_debug) {
            logger.logToFile("Running LLVM O2 optimization pipeline");
        }

        if (auto Err = PB.parsePassPipeline(MPM, "objc-arc-contract")) {
            logger.logError("[Optimizer] Could not parse pipeline: createObjCARCContractPass");
            return false;
        }

        MPM.addPass(
            PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2, llvm::ThinOrFullLTOPhase::FullLTOPostLink));

        if (Config.enable_debug) {
            logger.logToFile("[Optimizer] Running LLVM O2 optimization pipeline (end)");
        }

        // 阶段3: 在 O2 之后运行的自定义 Pass
        if (Config.run_after_o2) {
            for (const auto &Pass : PostPasses) {
                if (Config.enable_debug) {
                    logger.logToFile("Running post-O2 pass: " + Pass->getName());
                }

                // MPM.addPass();
            }
        }

        // 运行优化
        if (Config.enable_debug) {
            logger.logToFile("[Optimizer] Starting optimization pipeline execution");
        }
        MPM.run(M, *MAM);
        if (Config.enable_debug) {
            logger.logToFile("[Optimizer] Optimization pipeline execution finished");
        }

        return true;
    } catch (const std::exception &e) {
        logger.logToFile("Optimization failed: " + std::string(e.what()));
        return false;
    }
}

// 工具函数实现
bool optimizeModule(llvm::Module &M, const std::string &OutputFilename, const custom::OptimizerConfig &Config) {
    CustomOptimizer Optimizer(Config);

    // 添加示例自定义 Pass
    Optimizer.addPass(std::make_unique<ExampleCustomPass>(), Config.run_before_o2);

    if (!Optimizer.runOptimization(M)) {
        return false;
    }

    return true;
}

} // namespace custom