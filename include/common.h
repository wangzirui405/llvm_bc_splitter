#ifndef BC_SPLITTER_COMMON_H
#define BC_SPLITTER_COMMON_H

#include "core.h"
#include "logging.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
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
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

// 配置结构体
struct Config {
    const std::string workDir = "/Users/wangzirui/Desktop/libkn_so/reproduce_kn_shared_20251119_094034/";
    const std::string relativeDir =
        "private/var/folders/w7/w26y4gqn3t1f76kvj8r531dr0000gn/T/konan_temp6482467269771911962/";
    const std::string bcWorkDir = workDir + relativeDir;
    const std::string responseFile =
        "/Users/wangzirui/Desktop/libkn_so/reproduce_kn_shared_20251119_094034/response.txt";
    const std::string workSpace = "/Users/wangzirui/Desktop/libkn_so/test/workspace/";

    // 存储字符串集合
    llvm::SmallVector<std::string, 32> packageStrings;

    // 构造函数，初始化字符串集合
    Config() {
        std::vector<std::string> packages = {
            "androidx.compose.material",
            "com.tencent.compose.sample.mainpage.sectionItem",
            "kotlin.text.regex.AbstractCharClass.Companion.CharClasses",
            "androidx.compose.foundation.text",
            "androidx.compose.foundation.gestures",
            "androidx.compose.animation.core",
            "kotlinx.coroutines",
            "androidx.compose.runtime",
            "androidx.compose.foundation.layout",
            "androidx.compose.ui.platform",
            "androidx.compose.foundation",
            "androidx.compose.animation",
            "androidx.compose.ui.text",
            "androidx.compose.foundation.lazy.layout",
            "androidx.compose.ui.node",
            "androidx.compose.foundation.text.selection",
            "org.jetbrains.skia",
            "androidx.compose.ui.layout",
            "kotlin.collections",
            "androidx.compose.ui.interop",
            "androidx.compose.foundation.pager",
            "androidx.compose.ui.window",
            "androidx.compose.runtime.snapshots",
            "kotlin.text.regex",
            "com.tencent.compose.sample",
            "androidx.compose.ui.graphics",
            "androidx.compose.foundation.lazy",
            "androidx.compose.runtime.external.kotlinx.collections.immutable.implementations.immutableMap",
            "composesample.composeapp.generated.resources.Drawable0",
            "androidx.compose.ui.input.pointer"
            // "androidx.compose.ui.focus",
            // "androidx.compose.ui.semantics.SemanticsActions",
            // "androidx.compose.foundation.layout.IntrinsicMeasureBlocks",
            // "kotlinx.coroutines.channels",
            // "kotlinx.coroutines.internal",
            // "androidx.compose.runtime.internal.ComposableLambdaImpl",
            // "androidx.compose.ui.semantics",
            // "androidx.compose.ui.draw",
            // "com.tencent.compose.sample.mainpage",
            // "androidx.compose.runtime.Recomposer",
            // "kotlin.text.regex.AbstractCharClass",
            // "kotlin.native.internal",
            // "org.jetbrains.skia.paragraph",
            // "androidx.compose.foundation.gestures.AbstractDraggableNode",
            // "kotlinx.coroutines.flow",
            // "androidx.compose.material.AnchoredDraggableState",
            // "kotlinx.coroutines.flow.internal",
            // "com.tencent.compose.sample.mainpage.ComposableSingletons$DisplaySectionsKt",
            // "androidx.compose.ui.interop.arkc",
            // "androidx.compose.material.ripple.RippleAnimation",
            // "androidx.compose.runtime.external.kotlinx.collections.immutable.implementations.immutableList",
            // "kotlin.text",
            // "kotlinx.coroutines.channels.BufferedChannel",
            // "kotlinx.coroutines.JobSupport",
            // "androidx.compose.ui.scene.ComposeSceneMediator",
            // "androidx.compose.foundation.text.modifiers",
            // "androidx.compose.ui.platform.PlatformTextToolbar",
            // "androidx.compose.ui.scene",
            // "kotlin.sequences",
            // "androidx.compose.foundation.lazy.layout.LazyLayoutAnimation",
            // "androidx.compose.ui.text.font",
            // "com.tencent.compose.sample.mainpage.sectionItem.ComposableSingletons$CustomizeImageSnippetsKt",
            // "androidx.compose.runtime.changelist.Operation",
            // "androidx.compose.ui.text.input",
            // "androidx.compose.ui.text.platform",
            // "androidx.compose.ui",
            // "androidx.collection",
            // "androidx.compose.foundation.cupertino.CupertinoOverscrollEffect",
            // "androidx.compose.runtime.saveable",
            // "androidx.compose.animation.AnimatedContentTransitionScopeImpl",
            // "androidx.compose.ui.graphics.colorspace.Rgb",
            // "state_global$org.jetbrains.skia",
            // "kotlinx.coroutines.selects",
            // "androidx.compose.ui.window.ComposeArkUIViewContainer",
            // "androidx.compose.foundation.interaction",
            // "androidx.compose.foundation.gestures.ScrollingLogic",
            // "androidx.compose.foundation.gestures.AnimatedMouseWheelScrollPhysics",
            // "kotlinx.cinterop",
            // "androidx.compose.ui.semantics.SemanticsProperties",
            // "androidx.compose.animation.core.Transition",
            // "androidx.compose.foundation.relocation",
            // "androidx.compose.ui.scene.MultiLayerComposeSceneImpl.AttachedComposeSceneLayer",
            // "org.jetbrains.skiko",
            // "androidx.compose.ui.arkui",
            // "androidx.compose.ui.text.font.FontListFontFamilyTypefaceAdapter",
            // "androidx.compose.foundation.gestures.snapping",
            // "androidx.compose.ui.platform.FlushCoroutineDispatcher",
            // "androidx.compose.runtime.internal",
            // "androidx.compose.ui.scene.BaseComposeScene",
            // "com.tencent.compose.sample.mainpage.sectionItem.ComposableSingletons$DialogKt",
            // "kotlin.reflect",
            // "androidx.compose.material.ripple",
            // "androidx.compose.foundation.gestures.snapping.SnapFlingBehavior",
            // "androidx.compose.animation.EnterExitTransitionModifierNode",
            // "androidx.compose.foundation.gestures.PressGestureScopeImpl",
            // "androidx.compose.foundation.lazy.LazyListState",
            // "androidx.compose.ui.layout.LayoutNodeSubcompositionsState",
            // "androidx.compose.ui.node.NodeCoordinator",
            // "androidx.compose.material.TextFieldTransitionScope",
            // "androidx.compose.ui.graphics.vector"
        };

        // 将字符串插入到 StringSet 中
        for (const auto &package : packages) {
            packageStrings.push_back(package);
        }
    }
};

// 组信息结构体
struct GroupInfo {
    int groupId;
    std::string bcFile;
    bool hasKonanCxaDemangle = false;
    llvm::DenseSet<int> dependencies;

    GroupInfo(int id, std::string bc, bool special)
        : groupId(id), bcFile(bc), hasKonanCxaDemangle(special), dependencies() {}
    void printDetails() const;
};

class GlobalValueNameMatcher {
  private:
    // 缓存函数名列表
    llvm::StringMap<llvm::GlobalValue *> nameCache;
    std::mutex cacheMutex;
    bool cacheValid = false;

  public:
    GlobalValueNameMatcher() = default;
    ~GlobalValueNameMatcher() = default;

    // 重建缓存
    void rebuildCache(const llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> &globalValueMap);

    // 使缓存失效
    void invalidateCache();

    // 检查缓存是否有效
    bool isCacheValid() const { return cacheValid; }

    // 获取缓存大小
    size_t getCacheSize() const { return nameCache.size(); }

    // 检查字符串是否包含任何函数名
    bool containsGlobalValueName(llvm::StringRef str);

    // 获取匹配的所有函数信息
    llvm::StringMap<llvm::GlobalValue *> getMatchingGlobalValues(llvm::StringRef str);
};

class BCCommon {
  private:
    std::unique_ptr<llvm::Module> module;
    llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> globalValueMap;
    llvm::SmallVector<GroupInfo *, 32> fileMap;
    // 符号组
    llvm::SmallVector<llvm::DenseSet<llvm::GlobalValue *>, 32> globalValuesAllGroups;
    llvm::LLVMContext *context;
    Config config;
    // 存储循环调用组
    llvm::SmallVector<llvm::DenseSet<llvm::GlobalValue *>, 32> cyclicGroups;
    // 函数到所属循环组的映射（一个函数可能属于多个组）
    llvm::DenseMap<llvm::GlobalValue *, llvm::SmallVector<int, 32>> globalValueToGroupMap;
    Logger logger;
    GlobalValueNameMatcher GlobalValueNameMatcher;

  public:
    BCCommon();
    ~BCCommon();

    // 获取器
    llvm::Module *getModule() const { return module.get(); }
    llvm::SmallVector<GroupInfo *, 32> &getFileMap() { return fileMap; }
    const llvm::SmallVector<GroupInfo *, 32> &getFileMap() const { return fileMap; }
    llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> &getGlobalValueMap() { return globalValueMap; }
    const llvm::DenseMap<llvm::GlobalValue *, GlobalValueInfo> &getGlobalValueMap() const { return globalValueMap; }
    llvm::SmallVector<llvm::DenseSet<llvm::GlobalValue *>, 32> &getGlobalValuesAllGroups() {
        return globalValuesAllGroups;
    }
    const llvm::SmallVector<llvm::DenseSet<llvm::GlobalValue *>, 32> &getGlobalValuesAllGroups() const {
        return globalValuesAllGroups;
    }
    llvm::LLVMContext *getContext() const { return context; }

    // 设置器
    void setModule(std::unique_ptr<llvm::Module> M) { module = std::move(M); }
    void setContext(llvm::LLVMContext *newContext) { context = newContext; }

    // 辅助方法
    bool hasModule() const { return module != nullptr; }
    size_t getGlobalValueCount() const { return globalValueMap.size(); }
    bool writeBitcodeSafely(llvm::Module &M, llvm::StringRef filename);
    std::string renameUnnamedGlobalValues(llvm::StringRef filename);
    static bool matchesPattern(llvm::StringRef filename, llvm::StringRef pattern);
    bool copyByPattern(llvm::StringRef pattern);
    static bool isNumberString(llvm::StringRef str);
    static llvm::SmallVector<int, 32>
    convertIndexToFiltered(const llvm::SmallVector<llvm::DenseSet<llvm::GlobalValue *>, 32> &globalValuesAllGroups);

    // 清空数据
    void clear();
    void findCyclicGroups();
    llvm::DenseSet<llvm::GlobalValue *> getCyclicGroupsContainingGlobalValue(llvm::GlobalValue *GV);
    llvm::SmallVector<llvm::SmallSetVector<int, 32>, 32> getGroupDependencies();

    // 函数名匹配相关方法
    bool containsGlobalValueNameInString(llvm::StringRef str);
    llvm::StringSet<> getMatchingGlobalValueNames(llvm::StringRef str);
    llvm::DenseSet<llvm::GlobalValue *> getMatchingGlobalValues(llvm::StringRef str);
    llvm::GlobalValue *getFirstMatchingGlobalValue(llvm::StringRef str);

    // 缓存管理
    void rebuildGlobalValueNameCache();
    void invalidateGlobalValueNameCache();
    bool isGlobalValueNameCacheValid() const;
    size_t getGlobalValueNameCacheSize() const;
    void collectGlobalValuesFromConstant(llvm::Constant *C, llvm::DenseSet<llvm::GlobalValue *> &globalValueSet);
    void analyzeCallRelations();
    llvm::GlobalValue *findGlobalValueFromUser(llvm::User *U);

  private:
    // 确保缓存有效的内部方法
    void ensureCacheValid();
};

#endif // BC_SPLITTER_COMMON_H