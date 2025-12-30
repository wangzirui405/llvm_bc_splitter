// workdirectory.h
#ifndef BC_SPLITTER_WORKDIRECTORY_H
#define BC_SPLITTER_WORKDIRECTORY_H

#include "common.h"
#include <filesystem>
#include <string>
#include <vector>

class BCWorkDir {

  private:
    Config config;

  public:
    BCWorkDir() = default;

    // 核心功能
    bool createWorkDirectory(const std::string &path);

    // 创建工作目录结构
    bool createWorkDirectoryStructure();

    // 清理配置文件
    void cleanupConfigFiles(const std::string &groupPrefix);

    // 检查路径是否以斜杠结尾
    bool endsWithSlash(const std::string &path);

    bool checkAllPaths();

    // 文件操作
    bool copyFileToWorkspace(const std::string &inputFile);
    bool copyFile(const std::string &source, const std::string &destination, bool overwrite = true);
};

#endif // BC_SPLITTER_WORKDIRECTORY_H