/*
 * SLiM - Simple Login Manager
 * Copyright (C) 2009 Eygene Ryabinkin <rea@codelabs.ru>
 * Copyright (C) 2026 Jerry Howell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
*/

#include <sys/types.h>
#include <sys/random.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <cctype>
#include <string>
#include <vector>

#include "util.h"

/*
 * Adds the given cookie to the specified Xauthority file.
 * Returns true on success, false on fault.
 */
bool Util::add_mcookie(const std::string &mcookie, const char *display,
	const std::string &xauth_cmd, const std::string &authfile)
{
	if (display == nullptr || *display == '\0' || mcookie.empty() ||
			(mcookie.size() % 2) != 0 || xauth_cmd.empty() || authfile.empty() ||
			::strpbrk(display, "\r\n") != nullptr)
		return false;
	for (const unsigned char ch : mcookie) {
		if (!std::isxdigit(ch))
			return false;
	}
	int input_pipe[2];
	if (pipe2(input_pipe, O_CLOEXEC) != 0)
		return false;

	pid_t child = fork();
	if (child == 0) {
		dup2(input_pipe[0], STDIN_FILENO);
		close(input_pipe[0]);
		close(input_pipe[1]);
		char *const args[] = {
			const_cast<char *>(xauth_cmd.c_str()),
			const_cast<char *>("-f"), const_cast<char *>(authfile.c_str()),
			const_cast<char *>("-q"), nullptr};
		execvp(xauth_cmd.c_str(), args);
		_exit(127);
	}
	close(input_pipe[0]);
	if (child < 0) {
		close(input_pipe[1]);
		return false;
	}

	FILE *input = fdopen(input_pipe[1], "w");
	if (!input) {
		close(input_pipe[1]);
		waitpid(child, NULL, 0);
		return false;
	}
	fprintf(input, "remove %s\n", display);
	fprintf(input, "add %s . %s\n", display, mcookie.c_str());
	fprintf(input, "exit\n");
	const bool wrote = fclose(input) == 0;

	int status = 0;
	pid_t waited;
	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);
	return wrote && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool Util::secure_random_hex(std::string &output)
{
	static const char digits[] = "0123456789abcdef";
	std::vector<unsigned char> bytes((output.size() + 1) / 2);
	size_t done = 0;
	while (done < bytes.size()) {
		ssize_t count = getrandom(bytes.data() + done, bytes.size() - done, 0);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			return false;
		done += static_cast<size_t>(count);
	}
	for (size_t i = 0; i < output.size(); ++i) {
		const unsigned char byte = bytes[i / 2];
		output[i] = digits[(i % 2 == 0) ? (byte & 0x0f) : (byte >> 4)];
	}
	return true;
}

/*
 * Shell-free command handling for root-side helpers (halt, reboot, suspend,
 * console, session hooks). Configured commands are split into argv and run
 * with fork+execvp so no shell interprets metacharacters, globs, or
 * command substitutions.
 */
std::vector<std::string> Util::splitCommand(const std::string &command)
{
	std::vector<std::string> args;
	std::string current;
	bool in_single = false;
	bool in_double = false;
	bool has_token = false;

	for (size_t i = 0; i < command.size(); ++i) {
		const char c = command[i];
		if (in_single) {
			if (c == '\'') {
				in_single = false;
			} else {
				current += c;
				has_token = true;
			}
			continue;
		}
		if (in_double) {
			if (c == '"') {
				in_double = false;
			} else if (c == '\\') {
				if (i + 1 >= command.size())
					return {};
				current += command[++i];
				has_token = true;
			} else {
				current += c;
				has_token = true;
			}
			continue;
		}
		if (c == '\'') {
			in_single = true;
			has_token = true;
		} else if (c == '"') {
			in_double = true;
			has_token = true;
		} else if (c == '\\') {
			if (i + 1 >= command.size())
				return {};
			current += command[++i];
			has_token = true;
		} else if (c == '\0') {
			return {};
		} else if (std::isspace(static_cast<unsigned char>(c))) {
			if (has_token) {
				args.push_back(current);
				current.clear();
				has_token = false;
			}
		} else {
			current += c;
			has_token = true;
		}
	}
	if (in_single || in_double)
		return {};
	if (has_token)
		args.push_back(current);
	return args;
}

namespace {
bool isEnvNameStart(char c)
{
	return c == '_' || std::isalpha(static_cast<unsigned char>(c));
}

bool isEnvNameChar(char c)
{
	return c == '_' || std::isalnum(static_cast<unsigned char>(c));
}

std::string expandOneEnv(const std::string &value)
{
	std::string expanded;
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] != '$' || i + 1 >= value.size()) {
			expanded += value[i];
			continue;
		}
		const char next = value[i + 1];
		if (next == '{') {
			const size_t end = value.find('}', i + 2);
			if (end == std::string::npos) {
				expanded += value[i];
				continue;
			}
			const std::string name = value.substr(i + 2, end - (i + 2));
			if (name.empty()) {
				expanded += value[i];
				continue;
			}
			bool valid = isEnvNameStart(name[0]);
			for (size_t k = 1; valid && k < name.size(); ++k)
				valid = isEnvNameChar(name[k]);
			if (!valid) {
				expanded += value[i];
				continue;
			}
			const char *env = getenv(name.c_str());
			if (env)
				expanded += env;
			i = end;
		} else if (isEnvNameStart(next)) {
			size_t end = i + 1;
			while (end < value.size() && isEnvNameChar(value[end]))
				++end;
			const char *env = getenv(value.substr(i + 1, end - (i + 1)).c_str());
			if (env)
				expanded += env;
			i = end - 1;
		} else {
			/* "$$", "$ ", "$\"" etc. stay literal: never synthesize a PID
			 * or reinterpret quoting here. */
			expanded += value[i];
		}
	}
	return expanded;
}
} // namespace

void Util::expandEnvVars(std::vector<std::string> &args)
{
	for (std::string &arg : args)
		arg = expandOneEnv(arg);
}

void Util::replacePlaceholder(std::vector<std::string> &args,
	const std::string &placeholder, const std::string &value)
{
	if (placeholder.empty())
		return;
	for (std::string &arg : args) {
		std::string replaced;
		size_t pos = 0;
		size_t found;
		while ((found = arg.find(placeholder, pos)) != std::string::npos) {
			replaced.append(arg, pos, found - pos);
			replaced.append(value);
			pos = found + placeholder.size();
		}
		replaced.append(arg, pos, std::string::npos);
		arg.swap(replaced);
	}
}

bool Util::spawnAndWait(const std::vector<std::string> &args)
{
	if (args.empty())
		return false;
	std::vector<char *> argv;
	argv.reserve(args.size() + 1);
	for (const std::string &arg : args)
		argv.push_back(const_cast<char *>(arg.c_str()));
	argv.push_back(NULL);

	const pid_t child = fork();
	if (child == 0) {
		execvp(argv[0], argv.data());
		_exit(127);
	}
	if (child < 0)
		return false;
	int status = 0;
	pid_t waited;
	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);
	return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool Util::runCommand(const std::string &command)
{
	std::vector<std::string> args = splitCommand(command);
	if (args.empty())
		return command.empty() || command.find_first_not_of(" \t\r\n") == std::string::npos;
	expandEnvVars(args);
	return spawnAndWait(args);
}

/*
 * Interface for random number generator.  Just now it uses ordinary
 * random/srandom routines and serves as a wrapper for them.
 */
void Util::srandom(unsigned long seed)
{
	::srandom(seed);
}

long Util::random(void)
{
	return ::random();
}

/*
 * Makes seed for the srandom() using "random" values obtained from
 * getpid(), time(NULL) and others.
 */
long Util::makeseed(void)
{
	struct timespec ts;
	long pid = getpid();
	long tm = time(NULL);

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		ts.tv_sec = ts.tv_nsec = 0;
	}

	return pid + tm + (ts.tv_sec ^ ts.tv_nsec);
}
