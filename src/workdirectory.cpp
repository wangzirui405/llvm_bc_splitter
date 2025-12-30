// workdirectory.cpp
#include "workdirectory.h"
#include <filesystem>
#include <iostream>
#include <regex>

bool BCWorkDir::createWorkDirectory(const std::string &path) {
    try {
        // 创建目录（递归创建）
        if (std::filesystem::create_directories(path)) {
            std::cout << "工作目录创建成功: " << path << std::endl;
            return true;
        } else if (std::filesystem::exists(path)) {
            std::cout << "工作目录已存在: " << path << std::endl;
            return true;
        } else {
            std::cerr << "无法创建工作目录: " << path << std::endl;
            return false;
        }
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return false;
    }
}

bool BCWorkDir::createWorkDirectoryStructure() {
    std::string workDir = config.workSpace;
    if (std::filesystem::exists(workDir) && std::filesystem::is_directory(workDir)) {
        std::cout << "有历史记录,需要清理... " << std::endl;
        std::filesystem::remove_all(workDir);
    }

    std::cout << "创建BCSplitter工作目录结构..." << std::endl;

    // 创建基础目录
    if (!createWorkDirectory(workDir)) {
        return false;
    }

    // 子目录列表
    std::vector<std::string> subDirs = {"input", "output", "temp", "logs", "config"};

    // 创建子目录
    for (const auto &dir : subDirs) {
        std::string fullPath = workDir + dir;
        if (!createWorkDirectory(fullPath)) {
            std::cerr << "创建子目录失败: " << dir << std::endl;
            return false;
        }
    }

    std::cout << "✓ 工作目录结构创建完成: " << workDir << std::endl;
    return true;
}

void BCWorkDir::cleanupConfigFiles(const std::string &groupPrefix) {
    try {
        // 清理两个目录
        std::vector<std::string> dirs = {config.workDir, config.bcWorkDir};

        for (const auto &dir : dirs) {
            if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
                std::cerr << "Directory does not exist: " << dir << std::endl;
                continue;
            }

            std::string prefixPattern = groupPrefix + ".*";
            std::regex pattern1(prefixPattern);
            std::regex pattern2(R"(response_group_[0-9]_no_dep\.txt$)");
            std::regex pattern3(R"(response_group_[0-9]_with_dep\.txt$)");
            std::regex pattern4(R"(libkn.*\.so$)");

            for (const auto &entry : std::filesystem::directory_iterator(dir)) {
                if (std::filesystem::is_regular_file(entry.status())) {
                    std::string filename = entry.path().filename().string();

                    // 检查是否匹配需要删除的模式
                    if (std::regex_match(filename, pattern1) || std::regex_match(filename, pattern2) ||
                        std::regex_match(filename, pattern3) || std::regex_match(filename, pattern4)) {

                        std::cout << "Deleting: " << entry.path() << std::endl;
                        std::filesystem::remove(entry.path());
                    }
                }
            }
        }
        if (std::filesystem::exists(config.workSpace) && std::filesystem::is_directory(config.workSpace)) {
            std::cout << "Deleting workSpace... " << std::endl;
            std::filesystem::remove_all(config.workSpace);
        }

        std::cout << "Cleanup completed." << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Error during cleanup: " << e.what() << std::endl;
    }
}

bool BCWorkDir::endsWithSlash(const std::string &path) { return !path.empty() && path.back() == '/'; }

bool BCWorkDir::checkAllPaths() {
    bool result = true;
    std::cout << "=== 检查所有路径是否以'/'结尾 ===" << std::endl;

    if (!endsWithSlash(config.workDir)) {
        std::cout << "1. workDir: " << config.workDir << " - "
                  << "✗" << std::endl;
        result = false;
    }

    if (!endsWithSlash(config.workDir)) {
        std::cout << "2. relativeDir: " << config.relativeDir << " - "
                  << "✗" << std::endl;
        result = false;
    }
    if (!endsWithSlash(config.workDir)) {
        std::cout << "3. bcWorkDir: " << config.bcWorkDir << " - "
                  << "✗" << std::endl;
        result = false;
    }
    if (!endsWithSlash(config.workDir)) {
        std::cout << "4. workSpace: " << config.workSpace << " - "
                  << "✗" << std::endl;
        result = false;
    }

    return result;
}

bool BCWorkDir::copyFileToWorkspace(const std::string &inputFile) {
    try {
        // 确保目标目录存在
        std::string targetDir = config.workSpace + "input/";
        if (!createWorkDirectory(targetDir)) {
            std::cerr << "无法创建目标目录: " << targetDir << std::endl;
            return false;
        }

        // 提取文件名
        std::string destination = targetDir + inputFile;

        // 复制文件
        return copyFile(inputFile, destination);

    } catch (const std::exception &e) {
        std::cerr << "复制文件到工作空间失败: " << e.what() << std::endl;
        return false;
    }
}

bool BCWorkDir::copyFile(const std::string &source, const std::string &destination, bool overwrite) {
    try {
        // 检查源文件是否存在
        if (!std::filesystem::exists(source) || !std::filesystem::is_regular_file(source)) {
            std::cerr << "源文件不存在: " << source << std::endl;
            return false;
        }

        // 确保目标目录存在
        std::filesystem::path destPath(destination);
        if (destPath.has_parent_path()) {
            std::filesystem::create_directories(destPath.parent_path());
        }

        // 设置复制选项
        std::filesystem::copy_options options = std::filesystem::copy_options::none;
        if (overwrite) {
            options |= std::filesystem::copy_options::overwrite_existing;
        }

        // 执行复制
        std::filesystem::copy_file(source, destination, options);

        std::cout << "文件复制成功: " << source << " -> " << destination << std::endl;
        return true;

    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "文件复制失败: " << e.what() << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "未知错误: " << e.what() << std::endl;
        return false;
    }
}