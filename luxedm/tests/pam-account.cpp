/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "PAM.h"
#include <cstdlib>
#include <iostream>

static int account_result;
static int fake_handle;
extern "C" int __wrap_pam_start(const char *, const char *, const pam_conv *, pam_handle_t **handle)
{
    *handle = reinterpret_cast<pam_handle_t *>(&fake_handle);
    return PAM_SUCCESS;
}
extern "C" int __wrap_pam_end(pam_handle_t *, int) { return PAM_SUCCESS; }
extern "C" int __wrap_pam_acct_mgmt(pam_handle_t *, int) { return account_result; }
extern "C" const char *__wrap_pam_strerror(pam_handle_t *, int) { return "mock account result"; }

int main()
{
    PAM::Authenticator auth(nullptr);
    auth.start("test-only");
    for (int result = -1; result <= 64; ++result) {
        account_result = result;
        bool accepted = false;
        try { auth.check_acct(); accepted = true; }
        catch (const PAM::Auth_Exception &error) {
            if (error.errnum != result) return EXIT_FAILURE;
        }
        if (accepted != (result == PAM_SUCCESS)) {
            std::cerr << "Unexpected account acceptance for PAM result " << result << '\n';
            return EXIT_FAILURE;
        }
    }
    std::cout << "Only PAM_SUCCESS permits automatic login (66 results checked).\n";
}
