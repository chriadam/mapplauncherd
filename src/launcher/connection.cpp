/*
 * connection.cpp
 *
 * This file is part of applauncherd
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "connection.h"
#include "logger.h"

#include <sys/socket.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>

PoolType Connection::socketPool;

Connection::Connection(const string socketId) :
        m_fd(-1),
        m_curSocket(findSocket(socketId)),
        m_fileName(""),
        m_argc(0),
        m_argv(NULL),
        m_priority(0)
{
    m_io[0] = -1;
    m_io[1] = -1;
    m_io[2] = -1;

    if (m_curSocket == -1)
    {
        Logger::logErrorAndDie(EXIT_FAILURE, "socket isn't initialized\n");
    }
}

Connection::~Connection()
{
    closeConn();
}

int Connection::findSocket(const string socketId)
{
    PoolType::iterator it(socketPool.find(socketId));
    return it == socketPool.end() ? -1 : it->second;
}

void Connection::initSocket(const string socketId)
{
    PoolType::iterator it(socketPool.find(socketId));
    if (it == socketPool.end())
    {
        Logger::logInfo("%s: init socket '%s'", __FUNCTION__, socketId.c_str());

        int sockfd = socket(PF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0)
            Logger::logErrorAndDie(EXIT_FAILURE, "opening invoker socket\n");

        unlink(socketId.c_str());

        struct sockaddr sun;
        sun.sa_family = AF_UNIX;
        int maxLen = sizeof(sun.sa_data) - 1;
        strncpy(sun.sa_data, socketId.c_str(), maxLen);
        sun.sa_data[maxLen] = '\0';

        if (bind(sockfd, &sun, sizeof(sun)) < 0)
            Logger::logErrorAndDie(EXIT_FAILURE, "binding to invoker socket\n");

        if (listen(sockfd, 10) < 0)
            Logger::logErrorAndDie(EXIT_FAILURE, "listening to invoker socket\n");

        socketPool[socketId] = sockfd;
    }
}

bool Connection::acceptConn()
{
    m_fd = accept(m_curSocket, NULL, NULL);
    if (m_fd < 0)
    {
        if (errno != EINTR)
        {
            Logger::logError("accepting connections (%s)\n", strerror(errno));
            return false;
        }
    }
    return true;
}

void Connection::closeConn()
{
    if (m_fd != -1)
    {
        close(m_fd);
        m_fd = -1;
    }
}

bool Connection::sendMsg(uint32_t msg)
{
    Logger::logInfo("%s: %08x", __FUNCTION__, msg);
    return write(m_fd, &msg, sizeof(msg)) != -1;
}

bool Connection::recvMsg(uint32_t *msg)
{
    uint32_t buf = 0;
    int len = sizeof(buf);
    ssize_t  ret = read(m_fd, &buf, len);
    if (ret < len) {
        Logger::logError("can't read data from connecton in %s", __FUNCTION__);
        *msg = 0;
    } else {
        Logger::logInfo("%s: %08x", __FUNCTION__, *msg);
        *msg = buf;
    }
    return ret != -1;
}

bool Connection::sendStr(const char * str)
{
    // Send size.
    uint32_t size = strlen(str) + 1;
    sendMsg(size);

    Logger::logInfo("%s: '%s'", __FUNCTION__, str);

    // Send the string.
    return write(m_fd, str, size) != -1;
}

const char * Connection::recvStr()
{
    // Get the size.
    uint32_t size = 0;
    
    bool res = recvMsg(&size);
    if (!res || size == 0)
    {
        Logger::logError("string receiving failed in %s", __FUNCTION__);
        return NULL;
    }

    char * str = new char[size];
    if (!str)
    {
        Logger::logError("mallocing in %s", __FUNCTION__);
        return NULL;
    }

    // Get the string.
    uint32_t ret = read(m_fd, str, size);
    if (ret < size)
    {
        Logger::logError("getting string, got %u of %u bytes", ret, size);
        delete [] str;
        return NULL;
    }
    str[size - 1] = '\0';
    Logger::logInfo("%s: '%s'", __FUNCTION__, str);
    return str;
}


int Connection::receiveMagic()
{
    uint32_t magic = 0;

    // Receive the magic.
    recvMsg(&magic);

    if ((magic & INVOKER_MSG_MASK) == INVOKER_MSG_MAGIC)
    {
        if ((magic & INVOKER_MSG_MAGIC_VERSION_MASK) == INVOKER_MSG_MAGIC_VERSION)
            sendMsg(INVOKER_MSG_ACK);
        else
        {
            Logger::logError("receiving bad magic version (%08x)\n", magic);
            return -1;
        }
    }
    return magic & INVOKER_MSG_MAGIC_OPTION_MASK;
}

string Connection::receiveAppName()
{
    uint32_t msg = 0;

    // Get the action.
    recvMsg(&msg);
    if (msg != INVOKER_MSG_NAME)
    {
        Logger::logError("receiving invalid action (%08x)", msg);
        return string();
    }

    const char* name = recvStr();
    if (!name)
    {
        Logger::logError("receiving application name");
        return string();
    }
    sendMsg(INVOKER_MSG_ACK);

    string appName(name);
    delete [] name;
    return appName;
}

bool Connection::receiveExec()
{
    const char* filename = recvStr();
    if (!filename)
        return false;

    sendMsg(INVOKER_MSG_ACK);

    m_fileName = filename;
    delete [] filename;
    return true;
}

bool Connection::receivePriority()
{
    recvMsg(&m_priority);
    sendMsg(INVOKER_MSG_ACK);

    return true;
}

bool Connection::receiveArgs()
{
    // Get argc
    recvMsg(&m_argc);
    if (m_argc > 0 && m_argc < static_cast<uint32_t>(sysconf(_SC_ARG_MAX)))
    {
        // Reserve memory for argv
        m_argv = new const char * [m_argc];
        if (!m_argv)
        {
            Logger::logError("reserving memory for argv");
            return false;
        }

        // Get argv
        for (uint i = 0; i < m_argc; i++)
        {
            m_argv[i] = recvStr();
            if (!m_argv[i])
            {
                Logger::logError("receiving argv[%i]", i);
                return false;
            }
        }
    }
    
    sendMsg(INVOKER_MSG_ACK);
    return true;
}

// coverity[ +tainted_string_sanitize_content : arg-0 ]
bool putenv_sanitize(const char * s)
{
    return static_cast<bool>(strchr(s, '='));
}

bool Connection::receiveEnv()
{
    // Have some "reasonable" limit for environment variables to protect from
    // malicious data
    const uint32_t MAX_VARS = 1024;

    // Get number of environment variables.
    uint32_t n_vars = 0;
    recvMsg(&n_vars);
    if (n_vars > 0 && n_vars < MAX_VARS)
    {
        // Get environment variables
        for (uint32_t i = 0; i < n_vars; i++)
        {
            const char * var = recvStr();
            if (var == NULL)
            {
                Logger::logError("receiving environ[%i]", i);
                return false;
            }

            // In case of error, just warn and try to continue, as the other side is
            // going to keep sending the reset of the message.
            // String pointed to by var shall become part of the environment, so altering
            // the string shall change the environment, don't free it
            if (putenv_sanitize(var))
            {
                // coverity[+free : arg-0]
                if (putenv(const_cast<char *>(var)) != 0)
                {
                    Logger::logWarning("allocating environment variable");
                }
            }
            else
            {
                Logger::logWarning("invalid environment data");
            }
        }
    }
    else
    {
        Logger::logError("invalid environment variable count %d", n_vars);
        return false;
    }

    return true;
}

bool Connection::receiveIO()
{
    int dummy = 0;

    struct iovec iov;
    iov.iov_base = &dummy;
    iov.iov_len = 1;

    char buf[CMSG_SPACE(sizeof(m_io))];
    
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = buf;
    msg.msg_controllen = sizeof(buf);

    struct cmsghdr *cmsg;
    cmsg             = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len   = CMSG_LEN(sizeof(m_io));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;

    memcpy(CMSG_DATA(cmsg), m_io, sizeof(m_io));

    if (recvmsg(m_fd, &msg, 0) < 0)
    {
        Logger::logWarning("recvmsg failed in invoked_get_io: %s", strerror(errno));
        return false;
    }

    if (msg.msg_flags)
    {
        Logger::logWarning("unexpected msg flags in invoked_get_io");
        return false;
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_len != CMSG_LEN(sizeof(m_io)) ||
        cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
    {
        Logger::logWarning("invalid cmsg in invoked_get_io");
        return false;
    }

    memcpy(m_io, CMSG_DATA(cmsg), sizeof(m_io));

    return true;
}

bool Connection::receiveActions()
{
    Logger::logInfo("enter: %s", __FUNCTION__);

    while (1)
    {
        uint32_t action = 0;

        // Get the action.
        recvMsg(&action);

        switch (action)
        {
        case INVOKER_MSG_EXEC:
            receiveExec();
            break;
        case INVOKER_MSG_ARGS:
            receiveArgs();
            break;
        case INVOKER_MSG_ENV:
            receiveEnv();
            break;
        case INVOKER_MSG_PRIO:
            receivePriority();
            break;
        case INVOKER_MSG_IO:
            receiveIO();
            break;
        case INVOKER_MSG_END:
            sendMsg(INVOKER_MSG_ACK);
            return true;
        default:
            Logger::logError("receiving invalid action (%08x)\n", action);
            return false;
        }
    }
}

bool Connection::receiveApplicationData(AppData & rApp)
{
    // Read magic number
    rApp.setOptions(receiveMagic());
    if (rApp.options() == -1)
        return false;

    // Read application name
    rApp.setAppName(receiveAppName());
    if (rApp.appName().empty())
        return false;

    // Read application parameters
    if (receiveActions())
    {
        rApp.setFileName(m_fileName);
        rApp.setPriority(m_priority);
        rApp.setArgc(m_argc);
        rApp.setArgv(m_argv);
        rApp.setIODescriptors(vector<int>(m_io, m_io + sizeof(m_io)));
    }
    else
    {
        return false;
    }

    return true;
}

