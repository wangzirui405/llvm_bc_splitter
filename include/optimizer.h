// optimizer.h
#ifndef BC_SPLITTER_OPTIMIZER_H
#define BC_SPLITTER_OPTIMIZER_H

#include "logging.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include <functional>
#include <string>
#include <vector>

namespace custom {

// 优化器配置
struct OptimizerConfig {
    bool run_before_o2 = false; // 在 O2 优化前运行自定义 Pass
    bool run_after_o2 = false;  // 在 O2 优化后运行自定义 Pass
    bool enable_debug = true;   // 启用调试输出

    static OptimizerConfig Default() { return OptimizerConfig{false, false, true}; }
};

// 自定义 Pass 基类接口
class CustomPass {
  public:
    Logger logger;

    virtual ~CustomPass() = default;
    virtual llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) = 0;
    virtual std::string getName() const = 0;
};

// 预定义的自定义 Pass 示例
class ExampleCustomPass : public CustomPass {
  private:
    llvm::PassBuilder PB;
    custom::OptimizerConfig Config;

  public:
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) override;
    std::string getName() const override { return "ExampleCustomPass"; }
};

// 函数类型的自定义 Pass
using CustomPassFunc = std::function<llvm::PreservedAnalyses(llvm::Module &, llvm::ModuleAnalysisManager &)>;

class LambdaCustomPass : public CustomPass {
  private:
    CustomPassFunc Func;
    std::string Name;

  public:
    LambdaCustomPass(CustomPassFunc Func, const std::string &Name = "LambdaPass") : Func(std::move(Func)), Name(Name) {}

    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) override { return Func(M, AM); }

    std::string getName() const override { return Name; }
};

// 主要优化器类
class CustomOptimizer {
  private:
    llvm::PassBuilder PB;
    custom::OptimizerConfig Config;
    Logger logger;

    // 分析管理器
    std::unique_ptr<llvm::LoopAnalysisManager> LAM;
    std::unique_ptr<llvm::FunctionAnalysisManager> FAM;
    std::unique_ptr<llvm::CGSCCAnalysisManager> CGAM;
    std::unique_ptr<llvm::ModuleAnalysisManager> MAM;

    // 自定义 Pass 列表
    std::vector<std::unique_ptr<CustomPass>> PrePasses;
    std::vector<std::unique_ptr<CustomPass>> PostPasses;

    void initializeAnalysisManagers(llvm::Module &M);

  public:
    CustomOptimizer(const custom::OptimizerConfig &Config = custom::OptimizerConfig::Default());

    // 添加自定义 Pass
    void addPass(std::unique_ptr<CustomPass> Pass, bool before_o2 = false);

    // 添加 Lambda Pass
    void addLambdaPass(CustomPassFunc Func, const std::string &Name = "LambdaPass", bool before_o2 = false);

    // 清空所有自定义 Pass
    void clearPasses();

    // 运行优化（包含 O2 和自定义 Pass）
    bool runOptimization(llvm::Module &M);

    // 设置配置
    void setConfig(const custom::OptimizerConfig &NewConfig) { Config = NewConfig; }

    // 获取配置
    const custom::OptimizerConfig &getConfig() const { return Config; }
};

// 工具函数：优化并写入 bitcode 文件
bool optimizeModule(llvm::Module &M, const std::string &OutputFilename,
                    const custom::OptimizerConfig &Config = custom::OptimizerConfig::Default());

} // namespace custom

#endif // BC_SPLITTER_OPTIMIZER_H