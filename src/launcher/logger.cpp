/*
 * logger.cpp
 *
 * This file is part of applauncherd
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * This software, including documentation, is protected by copyright
 * controlled by Nokia Corporation. All rights are reserved.
 * Copying, including reproducing, storing, adapting or translating,
 * any or all of this material requires the prior written consent of
 * Nokia Corporation. This material also contains confidential
 * information which may not be disclosed to others without the prior
 * written consent of Nokia.
 */

#include "logger.h"
#include <stdlib.h>

#include <syslog.h>
#include <cstdarg>
#include <QDateTime>
#include <QDir>
#include <QString>

namespace {
    const QString logDirectory("/var/log");
    const QString logFileName(logDirectory + "/launcher.log");
    const QString oldLogFileName(logDirectory + "/launcher.log.old");
    const QString dateFormat("yyyy-MM-dd hh:mm:ss.zzz");
}

bool Logger::m_isOpened = false;
QTextStream Logger::m_logStream;
QFile Logger::m_logFile;
bool Logger::m_useSyslog = false;
bool Logger::m_echoMode = false;

void Logger::openLog(const char * progName)
{
    if (!Logger::m_isOpened)
    {
        // Check if it's possible to write under /var/log
        // Should make it possible to get the logs in the enviroments
        // with and without syslog.
        QDir logDir;
        if (logDir.exists(logDirectory)) {
            // Directory exists, is it possible to create a file in it?
            m_logFile.setFileName(oldLogFileName);
            if (m_logFile.open(QIODevice::WriteOnly)) {
                m_logFile.close();
                m_logFile.remove();
            }
            else {
                // It is not possible to write to file. Use syslog
                m_useSyslog = true;
            }
        }
        else { 
            // Directory does not exist. Is it possible to create it?
            if (logDir.mkdir(logDirectory) == false) {
                // Not possible to create directory. Use syslog.
                m_useSyslog = true;
            }
        }

        // Initialize the logging interface
        if (m_useSyslog == false) {
            // Remove the oldest log file
            m_logFile.setFileName(oldLogFileName);
            m_logFile.remove();
            // Copy latest log file to applifed.log.old
            m_logFile.setFileName(logFileName);
            m_logFile.rename(oldLogFileName);
            // Open current log file
            m_logFile.setFileName(logFileName);

            if (m_logFile.open(QIODevice::WriteOnly)) { 
                Logger::m_logStream.setDevice(&m_logFile);
            }
            else {
                m_useSyslog = true;
            }
        } 

        if (m_useSyslog) {
            openlog(progName, LOG_PID, LOG_DAEMON);
        }

        Logger::m_isOpened = true;
    }
}

void Logger::closeLog()
{
    if (Logger::m_isOpened) {
        if (m_useSyslog) {
            closelog();
        }
        else {
            m_logFile.close();
        }
        Logger::m_isOpened = false;
    }
}

void Logger::writeLog(const int priority, const char * format, va_list ap) 
{
    if (Logger::m_isOpened) {
        if (m_echoMode) {
            vprintf(format, ap);
            printf("\n");
        }

        if (m_useSyslog) {
            vsyslog(priority, format, ap);
        }
        else {
            QString msg;
            msg.vsprintf(format, ap);
            m_logStream << 
                QDateTime::currentDateTime().toString(dateFormat);

            switch (priority) {
            case LOG_NOTICE:
                m_logStream << " [NOTICE] ";
                break;
            case LOG_ERR:
                m_logStream << " [ERROR] ";
                break;
            case LOG_WARNING:
                m_logStream << " [WARNING] ";
                break;
            case LOG_INFO:
                m_logStream << " [INFO] ";
                break;
            default:
                m_logStream << " [N/A] ";
                break;
            }

            m_logStream << msg << "\n";
            m_logStream.flush();
        }
    }
}


void Logger::logNotice(const char * format, ...)
{
    va_list(ap);
    va_start(ap, format);
    writeLog(LOG_NOTICE, format, ap);
    va_end(ap);
}

void Logger::logError(const char * format, ...)
{
    va_list(ap);
    va_start(ap, format);
    writeLog(LOG_ERR, format, ap); 
    va_end(ap);
}

void Logger::logWarning(const char * format, ...)
{
    va_list(ap);
    va_start(ap, format);
    writeLog(LOG_WARNING, format, ap); 
    va_end(ap);
}

void Logger::logInfo(const char * format, ...)
{
    va_list(ap);
    va_start(ap, format);
    writeLog(LOG_INFO, format, ap); 
    va_end(ap);
}

void Logger::logErrorAndDie(int code, const char * format, ...)
{
    va_list(ap);
    va_start(ap, format);
    writeLog(LOG_ERR, format, ap);
    va_end(ap);

    exit(code);
}

void Logger::setEchoMode(bool enable)
{
    Logger::m_echoMode = enable;
}

