// logging.h
#ifndef BC_SPLITTER_LOGGING_H
#define BC_SPLITTER_LOGGING_H

#include <iostream>
#include <fstream>
#include <string>
#include "llvm/Support/raw_ostream.h"

class Logger {
private:
    std::ofstream logFile;
    std::string currentLogFile;

public:
    Logger();
    ~Logger();

    // 基本日志方法
    void log(const std::string& message);
    void logError(const std::string& message);
    void logWarning(const std::string& message);
    void logToFile(const std::string& message);

    // 独立日志文件支持
    std::ofstream createIndividualLogFile(const std::string& bcFilename, const std::string& suffix = "");
    void logToIndividualLog(std::ofstream& individualLog, const std::string& message, bool echoToMain = false);

    // 日志配置
    bool isOpen() const { return logFile.is_open(); }
    void close() { if (logFile.is_open()) logFile.close(); }
};

#endif // BC_SPLITTER_LOGGING_H