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
    void log(llvm::StringRef message);
    void logError(llvm::StringRef message);
    void logWarning(llvm::StringRef message);
    void logToFile(llvm::StringRef message);

    // 独立日志文件支持
    std::ofstream createIndividualLogFile(llvm::StringRef bcFilename, llvm::StringRef suffix = "");
    void logToIndividualLog(std::ofstream& individualLog, llvm::StringRef message, bool echoToMain = false);

    // 日志配置
    bool isOpen() const { return logFile.is_open(); }
    void close() { if (logFile.is_open()) logFile.close(); }
};

#endif // BC_SPLITTER_LOGGING_H