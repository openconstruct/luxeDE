/* SLiM - Simple Login Manager
   Copyright (C) 2009 Eygene Ryabinkin <rea@codelabs.ru>
   Copyright (C) 2026 Jerry Howell

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#ifndef _UTIL_H__
#define _UTIL_H__

#include <string>
#include <vector>

namespace Util
{
	bool add_mcookie(const std::string &mcookie, const char *display,
		const std::string &xauth_cmd, const std::string &authfile);
	bool secure_random_hex(std::string &output);

	/* Split a configured command into argv without invoking a shell.
	 * Whitespace separates arguments; single/double quotes group words
	 * and backslash escapes the next character (outside single quotes).
	 * Quotes are removed. Returns an empty vector for empty input or
	 * malformed/unbalanced quoting. */
	std::vector<std::string> splitCommand(const std::string &command);

	/* Expand $NAME / ${NAME} from the current environment in each argument.
	 * Unknown variables expand to empty. "$$" is left literal so a
	 * configured "$$" can never become an attacker-observable PID.
	 * Expansion never re-splits: one input argument stays one output. */
	void expandEnvVars(std::vector<std::string> &args);

	/* Replace every occurrence of placeholder in each argument with value.
	 * The value is inserted literally (no re-splitting, no glob, no shell),
	 * so usernames with spaces or metacharacters stay a single argument. */
	void replacePlaceholder(std::vector<std::string> &args,
		const std::string &placeholder, const std::string &value);

	/* Fork, execvp the argv directly (no shell) and wait. Returns true only
	 * when the child exits with status 0. */
	bool spawnAndWait(const std::vector<std::string> &args);

	/* Split + env-expand a configured command and run it without a shell.
	 * Empty/whitespace-only commands are a successful no-op (used for
	 * optional suspend/session hooks). Returns true on exit status 0. */
	bool runCommand(const std::string &command);

	void srandom(unsigned long seed);
	long random(void);

	long makeseed(void);
}

#endif /* _UTIL_H__ */
