// logging.cpp
#include "logging.h"
#include "common.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

Logger::Logger() {
    Config config;
    logFile.open(config.workSpace + "logs/bc_splitter.log", std::ios::out | std::ios::app);
    if (!logFile.is_open())
        std::cerr << "警告: 无法打开日志文件" << std::endl;
}

Logger::~Logger() {
    if (logFile.is_open())
        logFile.close();
}

void Logger::log(llvm::StringRef message) {
    if (logFile.is_open()) {
        logFile << message.str() << std::endl;
    }
    std::cout << message.str() << std::endl;
}

void Logger::logError(llvm::StringRef message) {
    std::string errorMsg = "[ERROR] " + message.str();
    if (logFile.is_open()) {
        logFile << errorMsg << std::endl;
    }
    std::cerr << errorMsg << std::endl;
}

void Logger::logWarning(llvm::StringRef message) {
    std::string warnMsg = "[WARNING] " + message.str();
    if (logFile.is_open()) {
        logFile << warnMsg << std::endl;
    }
    std::cout << warnMsg << std::endl;
}

void Logger::logToFile(llvm::StringRef message) {
    if (logFile.is_open()) {
        logFile << message.str() << std::endl;
    }
}

std::ofstream Logger::createIndividualLogFile(llvm::StringRef bcFilename, llvm::StringRef suffix) {
    Config config;
    std::string logFilename = config.workSpace + "logs/" + bcFilename.str();

    if (!suffix.empty()) {
        logFilename += suffix;
    }
    logFilename += ".log";

    std::ofstream individualLog;
    individualLog.open(logFilename, std::ios::out | std::ios::app);
    if (individualLog.is_open()) {
        individualLog << "=== BC文件验证日志: " << bcFilename.str() << " ===" << std::endl;
    } else {
        logError("无法创建独立日志文件: " + logFilename);
    }
    return individualLog;
}

void Logger::logToIndividualLog(std::ofstream &individualLog, llvm::StringRef message, bool echoToMain) {
    if (individualLog.is_open()) {
        individualLog << message.str() << std::endl;
    }
    if (echoToMain) {
        logToFile(message);
    }
}