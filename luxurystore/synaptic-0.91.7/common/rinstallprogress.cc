/* rinstallprogress.cc
 *
 * Copyright (c) 2000, 2001 Conectiva S/A
 *                     2002 Michael Vogt
 *
 * Author: Alfredo K. Kojima <kojima@conectiva.com.br>
 *         Michael Vogt <mvo@debian.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <iostream>
#include <cstdio>
#include <cerrno>
#include <csignal>
#include <apt-pkg/error.h>
#include <apt-pkg/install-progress.h>
#ifdef HAVE_RPM
#include <apt-pkg/configuration.h>
#endif

#include "rinstallprogress.h"

#include "i18n.h"
std::string RInstallProgress::finishMsg = _("\nSuccessfully applied all changes. You can close the window now.");
std::string RInstallProgress::errorMsg = _("\nNot all changes and updates succeeded. For further details of the failure, please expand the 'Details' panel below.");
std::string RInstallProgress::incompleteMsg = 
      _("\nSuccessfully installed all packages of the current medium. "
	"To continue the installation with the next medium close "
	"this window.");

const char* RInstallProgress::getResultStr(pkgPackageManager::OrderResult res)
{
   int size;
   switch( res ) {
   case 0: // completed
      return finishMsg.c_str();
      break;
   case 1: // failed 
      return errorMsg.c_str();
      break;
   case 2: // incomplete
      return incompleteMsg.c_str();
      break;
   }

   return "Unknown install result.";
}


pkgPackageManager::OrderResult RInstallProgress::start(pkgPackageManager *pm,
                                                       int numPackages,
                                                       int numPackagesTotal)
{
   pkgPackageManager::OrderResult res;
   int status = 0;
   pid_t child = -1;
   int fd[2] = {-1, -1};

   if (pm == nullptr)
      return pkgPackageManager::Failed;

#ifdef HAVE_RPM
   _config->Set("RPM::Interactive", "false");
#endif

   res = pm->DoInstallPreFork();
   if (res == pkgPackageManager::Failed)
      return res;

#ifdef HAVE_RPM
   if (pipe(fd) != 0)
      return pkgPackageManager::Failed;
#endif

   child = fork();
   if (child < 0) {
#ifdef HAVE_RPM
      close(fd[0]);
      close(fd[1]);
#endif
      return pkgPackageManager::Failed;
   }

   if (child == 0) {
#ifdef HAVE_RPM
      if (dup2(fd[1], STDOUT_FILENO) < 0 || dup2(fd[1], STDERR_FILENO) < 0)
         _exit(127);
      close(fd[0]);
      close(fd[1]);
      APT::Progress::PackageManagerProgressFd progress(-1);
      auto child_result = pm->DoInstallPostFork(&progress);
      _error->DumpErrors();
      _exit(child_result == pkgPackageManager::Failed ? 1 : 0);
#else
      APT::Progress::PackageManagerProgressFd progress(-1);
      auto child_result = pm->DoInstallPostFork(&progress);
      _exit(child_result == pkgPackageManager::Failed ? 1 : 0);
#endif
   }

#ifdef HAVE_RPM
   _childin = fd[0];
   close(fd[1]);
   int flags = fcntl(_childin, F_GETFL, 0);
   if (flags < 0 || fcntl(_childin, F_SETFL, flags | O_NONBLOCK) < 0) {
      close(_childin);
      _childin = -1;
      kill(child, SIGTERM);
      waitpid(child, &status, 0);
      return pkgPackageManager::Failed;
   }
   _donePackages = 0;
   _numPackages = numPackages;
   _numPackagesTotal = numPackagesTotal;
#else
   _childin = -1;
   (void)numPackages;
   (void)numPackagesTotal;
#endif

   startUpdate();
   bool failed = false;
   for (;;) {
      pid_t waited = waitpid(child, &status, WNOHANG);
      if (waited == 0) {
         updateInterface();
         usleep(10000);
         continue;
      }
      if (waited < 0 && errno == EINTR)
         continue;
      if (waited != child)
         failed = true;
      else if (WIFEXITED(status))
         res = WEXITSTATUS(status) == 0 ? static_cast<pkgPackageManager::OrderResult>(0) : pkgPackageManager::Failed;
      else if (WIFSIGNALED(status))
         failed = true;
      else
         failed = true;
      break;
   }
   if (failed)
      res = pkgPackageManager::Failed;
   finishUpdate();
#ifdef HAVE_RPM
   if (_childin >= 0) {
      close(_childin);
      _childin = -1;
   }
#endif
   return res;
}

// vim:sts=4:sw=4
