// logging.cpp
#include "logging.h"
#include "common.h"
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

Logger::Logger() {
    Config config;
    logFile.open(config.workSpace + "logs/bc_splitter.log", std::ios::out | std::ios::app);
    if (!logFile.is_open()) std::cerr << "警告: 无法打开日志文件" << std::endl;
}

Logger::~Logger() {
    if (logFile.is_open()) logFile.close();
}

void Logger::log(const std::string& message) {
    if (logFile.is_open()) {
        logFile << message << std::endl;
    }
    std::cout << message << std::endl;
}

void Logger::logError(const std::string& message) {
    std::string errorMsg = "[ERROR] " + message;
    if (logFile.is_open()) {
        logFile << errorMsg << std::endl;
    }
    std::cerr << errorMsg << std::endl;
}

void Logger::logWarning(const std::string& message) {
    std::string warnMsg = "[WARNING] " + message;
    if (logFile.is_open()) {
        logFile << warnMsg << std::endl;
    }
    std::cout << warnMsg << std::endl;
}

void Logger::logToFile(const std::string& message) {
    if (logFile.is_open()) {
        logFile << message << std::endl;
    }
}

std::ofstream Logger::createIndividualLogFile(const std::string& bcFilename, const std::string& suffix) {
    Config config;
    std::string logFilename = config.workSpace + "logs/" + bcFilename;

    if (!suffix.empty()) {
        logFilename += suffix;
    }
    logFilename += ".log";

    std::ofstream individualLog;
    individualLog.open(logFilename, std::ios::out | std::ios::app);
    if (individualLog.is_open()) {
        individualLog << "=== BC文件验证日志: " << bcFilename << " ===" << std::endl;
    } else {
        logError("无法创建独立日志文件: " + logFilename);
    }
    return individualLog;
}

void Logger::logToIndividualLog(std::ofstream& individualLog, const std::string& message, bool echoToMain) {
    if (individualLog.is_open()) {
        individualLog << message << std::endl;
    }
    if (echoToMain) {
        logToFile(message);
    }
}