/*
 * daemon.cpp
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

#include "daemon.h"
#include "logger.h"
#include "connection.h"
#include "booster.h"
#include "mbooster.h"
#include "qtbooster.h"

#include <cstdlib>
#include <cerrno>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#include <signal.h>

#include <fcntl.h>
#include <iostream>

Daemon * Daemon::m_instance = NULL;

Daemon::Daemon(int & argc, char * argv[]) :
    m_testMode(false),
    m_daemon(false),
    m_quiet(false)
{
    if (!Daemon::m_instance)
    {
        Daemon::m_instance = this;
    }
    else
    {
        std::cerr << "Daemon already created!" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Parse arguments
    parseArgs(vector<string>(argv, argv + argc));

    // Disable console output
    if (m_quiet)
        consoleQuiet();

    // Store arguments list
    m_initialArgv = argv;
    m_initialArgc = argc;

    // Daemonize if desired
    if (m_daemon)
    {
        daemonize();
    }
}

void Daemon::consoleQuiet()
{
    close(0);
    close(1);
    close(2);

    if (open("/dev/null", O_RDONLY) < 0)
        Logger::logErrorAndDie(EXIT_FAILURE, "opening /dev/null readonly");

    int fd = open("/dev/null", O_WRONLY);
    if ((fd == -1) || (dup(fd) < 0))
        Logger::logErrorAndDie(EXIT_FAILURE, "opening /dev/null writeonly");
}


Daemon * Daemon::instance()
{
    return Daemon::m_instance;
}

Daemon::~Daemon()
{}

void Daemon::run()
{
    // Make sure that LD_BIND_NOW does not prevent dynamic linker to
    // use lazy binding in later dlopen() calls.
    unsetenv("LD_BIND_NOW");

    // create sockets for each of the boosters
    Connection::initSocket(MBooster::socketName());
    Connection::initSocket(QtBooster::socketName());

    // Pipe used to tell the parent that a new
    // booster is needed
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        Logger::logErrorAndDie(EXIT_FAILURE, "Creating a pipe failed!!!\n");
    }

    forkBooster(MBooster::type(), pipefd);
    forkBooster(QtBooster::type(),  pipefd);

    while (true)
    {
        // Wait for something appearing in the pipe
        char msg;
        ssize_t count = read(pipefd[0], reinterpret_cast<void *>(&msg), 1);
        if (count)
        {
            // Guarantee some time for the just launched application to
            // start up before forking new booster. Not doing this would
            // slow down the start-up significantly on single core CPUs.
            sleep(2);

            // Fork a new booster of the given type
            forkBooster(msg, pipefd);
        }
        else
        {
            Logger::logWarning("Nothing read from the pipe\n");
        }
    }
}

bool Daemon::forkBooster(char type, int pipefd[2])
{
    // Fork a new process
    pid_t newPid = fork();

    if (newPid == -1)
        Logger::logErrorAndDie(EXIT_FAILURE, "Forking while invoking");

    if (newPid == 0) /* Child process */
    {
        // Reset used signal handlers
        signal(SIGCHLD, SIG_DFL);

        // Will get this signal if applauncherd dies
        prctl(PR_SET_PDEATHSIG, SIGHUP);

        // Close unused read end
        close(pipefd[0]);

        if (setsid() < 0)
        {
            Logger::logError("Setting session id\n");
        }

        Logger::logNotice("Running a new Booster of %c type...", type);

        // Create a new booster and initialize it
        Booster * booster = NULL;
        if (MBooster::type() == type)
        {
            booster = new MBooster();
        }
        else if (QtBooster::type() == type)
        {
            booster = new QtBooster();
        }
        else
        {
            Logger::logErrorAndDie(EXIT_FAILURE, "Unknown booster type \n");
        }

        // Preload stuff
        booster->preload();

        // Clean-up all the env variables
        clearenv();

        // Rename launcher process to booster
        booster->renameProcess(m_initialArgc, m_initialArgv);

        Logger::logNotice("Wait for message from invoker");

        // Wait and read commands from the invoker
        booster->readCommand();

        // Give to the process an application specific name
        booster->renameProcess(m_initialArgc, m_initialArgv);

        // Signal the parent process that it can create a new
        // waiting booster process and close write end
        const char msg = booster->boosterType();
        ssize_t ret = write(pipefd[1], reinterpret_cast<const void *>(&msg), 1);
        if (ret == -1) {
            Logger::logError("Can't send signal to launcher process' \n");
        }

        close(pipefd[1]);

        // Don't care about fate of parent applauncherd process any more
        prctl(PR_SET_PDEATHSIG, 0);

        // Run the current Booster
        booster->run();

        // Finish
        delete booster;
        exit(EXIT_SUCCESS);
    }
    else /* Parent process */
    {
        // Store the pid so that we can reap it later
        m_children.push_back(newPid);
    }

    return true;
}

void Daemon::reapZombies()
{
    vector<pid_t>::iterator i(m_children.begin());
    while (i != m_children.end())
    {
        if (waitpid(*i, NULL, WNOHANG))
        {
            i = m_children.erase(i);
        }
        else
        {
            i++;
        }
    }
}

void Daemon::daemonize()
{
    // Our process ID and Session ID
    pid_t pid, sid;

    // Fork off the parent process: first fork
    pid = fork();
    if (pid < 0)
    {
        Logger::logError("Unable to fork daemon, code %d (%s)", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // If we got a good PID, then we can exit the parent process.
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    // Fork off the parent process: second fork
    pid = fork();
    if (pid < 0)
    {
        Logger::logError("Unable to fork daemon, code %d (%s)", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // If we got a good PID, then we can exit the parent process.
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    // Change the file mode mask
    umask(0);

    // Open any logs here

    // Create a new SID for the child process
    sid = setsid();
    if (sid < 0)
    {
        Logger::logError("Unable to create a new session, code %d (%s)", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Change the current working directory
    if ((chdir("/")) < 0)
    {
        Logger::logError("Unable to change directory to %s, code %d (%s)", "/", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Open file descriptors pointing to /dev/null
    // Redirect standard file descriptors to /dev/null
    // Close new file descriptors

    const int new_stdin  = open("/dev/null", O_RDONLY);
    if (new_stdin != -1) {
        dup2(new_stdin,  STDIN_FILENO);
        close(new_stdin);
    }

    const int new_stdout = open("/dev/null", O_WRONLY);
    if (new_stdout != -1) {
        dup2(new_stdout, STDOUT_FILENO);
        close(new_stdout);
    }

    const int new_stderr = open("/dev/null", O_WRONLY);
    if (new_stderr != -1) {
        dup2(new_stderr, STDERR_FILENO);
        close(new_stderr);
    }
}

void Daemon::usage() const
{
    std::cout << "Usage: "<< PROG_NAME << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --daemon            Fork and go into the background.\n"
              //<< "  --pidfile FILE      Specify a different pid file (default " << LAUNCHER_PIDFILE << " ).\n"
              //<< "  --send-app-died     Send application died signal.\n"
              << "  --quiet             Do not print anything.\n"
              << "  --help              Print this help message.\n"
              << "\n"
              << "Use the invoker to start a <shared object> from the launcher.\n"
              << "Where <shared object> is a binary including a 'main' symbol.\n"
              << "Note that the binary needs to be linked with -shared or -pie.\n";

    exit(EXIT_SUCCESS);
}

void Daemon::parseArgs(const vector<string> & args)
{
    for (vector<string>::const_iterator i(args.begin()); i != args.end(); i++)
    {
        if ((*i) == "--help")
        {
            usage();
        }
        else if ((*i) == "--daemon")
        {
            m_daemon = true;
        }
        else if  ((*i) ==  "--quiet")
        {
            m_quiet = true;
        }
        else if ((*i) == "--test")
        {
            m_testMode = true;
        }
    }
}
