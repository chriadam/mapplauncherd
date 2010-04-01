/*
 * ut_daemon.cpp
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

#include "ut_daemon.h"
#include "daemon.h"

Ut_Daemon::Ut_Daemon()
{
    int argc = 3;
    char **argv = new char * [argc];
    argv[0] = strdup("app");
    argv[1] = strdup("--testParameter");
    argv[2] = strdup("--123");

    m_subject.reset(new Daemon( argc, argv ));
}

Ut_Daemon::~Ut_Daemon()
{
}

void Ut_Daemon::initTestCase()
{}

void Ut_Daemon::cleanupTestCase()
{}

void Ut_Daemon::testInitialArguments()
{
    QVERIFY2(m_subject->m_initialArgc == 3, "Failure");
    QCOMPARE(m_subject->m_initialArgv[0], "app");
    QCOMPARE(m_subject->m_initialArgv[1], "--testParameter");
    QCOMPARE(m_subject->m_initialArgv[2], "--123");
}


void Ut_Daemon::testParseArgs()
{
    int argc = 4;
    char **argv    = new char * [argc];

    argv[0] = strdup("app");
    argv[1] = strdup("--daemon");
    argv[2] = strdup("--quiet");
    argv[3] = strdup("--test");

    QVERIFY2(m_subject->m_daemon == false, "Failure");
    QVERIFY2(m_subject->m_quiet == false, "Failure");
    QVERIFY2(m_subject->m_testMode == false, "Failure");

    m_subject->parseArgs(vector<string>(argv, argv + argc));

    QVERIFY2(m_subject->m_daemon == true, "Failure");
    QVERIFY2(m_subject->m_quiet == true, "Failure");
    QVERIFY2(m_subject->m_testMode == true, "Failure");

    delete argv[0];
    delete argv[1];
    delete argv[2];
    delete argv[3];
    delete [] argv;
}

void Ut_Daemon::testVerifyInstance()
{
    QVERIFY2(m_subject.get() == Daemon::instance(), "Failure");
}

void Ut_Daemon::testReapZombies()
{
    QVERIFY2(m_subject->m_children.size() == 0, "Failure");

    for (int i=234234; i<234245; i++) {
        m_subject->m_children.push_back(i);
    }

    QVERIFY2(m_subject->m_children.size() == 11, "Failure");

    m_subject->reapZombies();

    QVERIFY2(m_subject->m_children.size() == 0, "Failure");
}

QTEST_APPLESS_MAIN(Ut_Daemon);
