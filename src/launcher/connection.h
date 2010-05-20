/*
 * connection.h
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

#ifndef CONNECTION_H
#define CONNECTION_H

#include "appdata.h"
#include "protocol.h"

#include <stdint.h>
#include <string>

using std::string;

#include <map>

using std::map;

#include <vector>

using std::vector;

typedef map<string, int> PoolType;

#ifdef HAVE_CREDS
    #include <sys/creds.h>
#endif


/*!
 * \class Connection.
 * \brief Wrapper class for the connection between invoker and launcher.
 *
 * This class wraps up the UNIX file socket connection between the invoker
 * and the launcher daemon.
 */
class Connection
{
public:

    /*! \brief Constructor.
     *  \param socketId Path to the UNIX file socket to be used.
     */
    explicit Connection(const string socketId);

    //! \brief Destructor
    virtual ~Connection();

    /*! \brief Accept connection.
     * Accept a socket connection from the invoker.
     * \return true on success.
     */
    bool acceptConn();

    //! \brief Close the socket connection.
    void closeConn();

    //! \brief Receive application data to rApp.
    bool receiveApplicationData(AppData & rApp);

    /*! \brief Initialize a file socket.
     * \param socketId Path to the socket file
     */
    static void initSocket(const string socketId);

private:

    /*! \brief Receive actions.
     * This method executes the actual data-receiving loop and terminates
     * after INVOKER_MSG_END is received.
     * \return True on success
     */
    bool receiveActions();

    /*! \brief Receive and return the magic number.
     * \return The magic number received from the invoker.
     */
    int receiveMagic();

    /*! \brief Receive and return the application name.
     * \return Name string
     */
    string receiveAppName();

    /*! \brief Return initialized socket.
     * \param socketId Path to the socket file
     */
    static int findSocket(const string socketId);

    //! Disable copy-constructor
    Connection(const Connection & r);

    //! Disable assignment operator
    Connection & operator= (const Connection & r);

    //! Receive executable name
    bool receiveExec();

    //! Receive arguments
    bool receiveArgs();

    //! Receive environment
    bool receiveEnv();

    //! Receive I/O descriptors
    bool receiveIO();

    //! Receive priority
    bool receivePriority();

    //! Send message to a socket. This is a virtual to help unit testing.
    virtual bool sendMsg(uint32_t msg);

    //! Receive a message from a socket. This is a virtual to help unit testing.
    virtual bool recvMsg(uint32_t *msg);

    //! Send a string. This is a virtual to help unit testing.
    virtual bool sendStr(const char * str);
    //! Receive a string. This is a virtual to help unit testing.
    virtual const char * recvStr();

    //! Pool of sockets mapped to id's
    static PoolType socketPool;

    //! Socket fd
    int      m_fd;
    int      m_curSocket;
    string   m_fileName;
    uint32_t m_argc;
    const char **  m_argv;
    int      m_io[3];
    uint32_t m_priority;

#if defined (HAVE_CREDS) && ! defined (DISABLE_VERIFICATION)
    static const char * m_credsStr;
    creds_value_t m_credsValue;
    creds_type_t  m_credsType;
#endif

#ifdef UNIT_TEST
    friend class Ut_Connection;
#endif
};

#endif //CONNECTION_H
