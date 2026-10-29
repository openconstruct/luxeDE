/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <glib.h>
#include <apt-pkg/init.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/error.h>
#include <apt-pkg/algorithms.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <sstream>
#include "rpackagelister.h"

inline std::string safe(const char *s) { return s ? s : ""; }
inline std::string apt_errors() {
    std::string result, message;
    while (!_error->empty()) { _error->PopMessage(message); result += message + "\n"; }
    return result;
}
inline std::unique_ptr<RPackageLister> open_apt() {
    if (!pkgInitConfig(*_config) || !pkgInitSystem(*_config, _system))
        throw std::runtime_error(apt_errors());
    _config->Set("Program", "luxurystore");
    _config->Set("Synaptic::Log::Changes", false);
    auto l = std::make_unique<RPackageLister>();
    if (!l->openCache()) throw std::runtime_error(apt_errors());
    return l;
}
using Requests = std::map<std::string, std::string>;
inline bool valid_name(const std::string &name) {
    if (name.empty() || name.size() > 250 || !g_ascii_isalnum(name[0])) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return g_ascii_islower(c) || g_ascii_isdigit(c) || c == '+' || c == '-' || c == '.' || c == ':';
    });
}
inline void resolve(RPackageLister &l, const Requests &requests) {
    for (auto &[name, action] : requests) {
        if (!valid_name(name) || (action != "install" && action != "remove"))
            throw std::runtime_error("Invalid package request.");
        auto p = l.getPackage(name);
        if (!p) throw std::runtime_error("Package no longer available: " + name);
    }
    // Stage the complete selection without RPackage::setInstall/setRemove,
    // which each run their own solver against an incomplete request.
    // Protect every explicit request before any automatic dependency marking.
    auto cache = l.getCache()->deps();
    // Modern APT stores Protect() in the dependency cache as well as in the
    // solver. Synaptic's legacy flag snapshot does not restore that bit.
    // Clear prior UI selections so they cannot constrain a later review.
    // Iterate the full dependency cache (PkgBegin), not the filtered RPackage
    // view, so multi-arch duplicates and virtual packages are cleared too.
    for (pkgCache::PkgIterator p = cache->PkgBegin(); !p.end(); ++p)
        (*cache)[p].iFlags &= ~pkgDepCache::Protected;
    pkgProblemResolver solver(cache);
    for (auto &[name, action] : requests) {
        auto pkg = *l.getPackage(name)->package();
        solver.Clear(pkg);
        solver.Protect(pkg);
        if (action == "remove") solver.Remove(pkg);
    }
    for (auto &[name, action] : requests) {
        auto pkg = *l.getPackage(name)->package();
        if (action == "remove") {
            cache->SetReInstall(pkg, false);
            cache->MarkDelete(pkg, false);
        } else cache->MarkInstall(pkg, true);
    }
    if (!requests.empty() && !solver.Resolve(true))
        throw std::runtime_error("The requested changes cannot be resolved.\n" + apt_errors());
    if (!l.check() || _error->PendingError())
        throw std::runtime_error("The requested changes cannot be resolved.\n" + apt_errors());
}
inline std::string action_for(RPackage *p) {
    int f = p->getFlags();
    if (f & RPackage::FRemove) return "Remove";
    if (f & RPackage::FReInstall) return "Reinstall";
    if (f & RPackage::FDowngrade) return "Downgrade";
    if (f & RPackage::FUpgrade) return "Upgrade";
    if (f & RPackage::FInstall) return "Install";
    return "";
}
inline std::string plan(RPackageLister &l) {
    std::vector<std::string> lines;
    for (auto p : l.getPackages()) {
        std::string action = action_for(p);
        if (action.empty()) continue;
        lines.push_back(action + "\t" + p->name() + "\t" + safe(p->installedVersion()) + "\t" +
                        (action == "Remove" ? "" : safe(p->availableVersion())) + "\n");
    }
    std::sort(lines.begin(), lines.end());
    std::string out;
    for (auto &s : lines) out += s;
    return out;
}
inline std::string digest(const std::string &s) {
    gchar *h = g_compute_checksum_for_string(G_CHECKSUM_SHA256, s.c_str(), s.size());
    std::string out(h); g_free(h); return out;
}
inline std::string encode_request(const Requests &requests, const std::string &review) {
    GKeyFile *key = g_key_file_new();
    for (auto &[name, action] : requests) g_key_file_set_string(key, "Packages", name.c_str(), action.c_str());
    g_key_file_set_string(key, "Review", "SHA256", digest(review).c_str());
    gsize size; gchar *data = g_key_file_to_data(key, &size, nullptr);
    gchar *b64 = g_base64_encode(reinterpret_cast<guchar *>(data), size);
    std::string out(b64); g_free(b64); g_free(data); g_key_file_unref(key); return out;
}
