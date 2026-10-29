// Focused pure tests: no APT files or privileged helpers are used.
#include "../synaptic-0.91.7/gtk4/repositories.h"
#include <glib/gstdio.h>
#include <iostream>

int main() {
    const std::string source = "Types: deb\nEnabled: no\nURIs: https://example.test\nSuites: stable\n";
    const auto enabled = repository_rewrite(source, true, true);
    const auto expected = "Types: deb\nEnabled: yes\nURIs: https://example.test\nSuites: stable\n";
    if (enabled != expected || repository_rewrite(enabled, true, true) != expected ||
        repository_rewrite(enabled, true, false) != source) {
        std::cerr << "Enabled field did not replace its previous value\n";
        return 1;
    }
    gchar *temporary = g_dir_make_tmp("luxurystore-cas-XXXXXX", nullptr);
    if (!temporary) return 1;
    std::string path = std::string(temporary) + "/source.list";
    const std::string original = "deb [signed-by=/key] https://example.test stable main\n";
    const std::string replacement = "# luxurystore-disabled deb [signed-by=/key] https://example.test stable main\n";
    if (!g_file_set_contents(path.c_str(), original.c_str(), original.size(), nullptr)) return 1;
    if (chmod(path.c_str(), 0644) != 0) return 1;
    if (!write_repository_atomic(path, replacement, &original)) return 1;
    const std::string external = "# administrator edit\ndeb https://admin.test stable main\n";
    if (!g_file_set_contents(path.c_str(), external.c_str(), external.size(), nullptr)) return 1;
    if (chmod(path.c_str(), 0644) != 0) return 1;
    try {
        write_repository_atomic(path, original, &replacement);
        std::cerr << "Concurrent repository edit was overwritten\n";
        return 1;
    } catch (const std::runtime_error &) {
        // Compare-before-rename detects and preserves an edit made after review.
    }
    gchar *contents = nullptr;
    gsize contents_size = 0;
    if (!g_file_get_contents(path.c_str(), &contents, &contents_size, nullptr) ||
        std::string(contents, contents_size) != external) {
        g_free(contents); return 1;
    }
    g_free(contents);
    g_unlink(path.c_str()); g_rmdir(temporary); g_free(temporary);
    const std::string crlf = "Types: deb\r\n  Enabled: no\r\nURIs: https://example.test\r\nSuites: stable\r\n";
    if (repository_rewrite(crlf, true, true) !=
        "Types: deb\r\n  Enabled: yes\r\nURIs: https://example.test\r\nSuites: stable\r\n") {
        std::cerr << "CRLF or field indentation changed\n";
        return 1;
    }
    try {
        repository_rewrite("Types: deb\nURIs: https://example.test\nSuites: stable\nEnabled: yes\nEnabled: no\n", true, true);
        std::cerr << "Duplicate Enabled fields were accepted\n";
        return 1;
    } catch (const std::runtime_error &) {
        // Refuse ambiguous definitions instead of changing either value.
    }
    const std::string multi = "Types: deb\nURIs: https://one.test\n\nTypes: deb\nURIs: https://two.test\n";
    try {
        repository_rewrite(multi, true, false);
        std::cerr << "Multiple source paragraphs were accepted\n";
        return 1;
    } catch (const std::runtime_error &) {}
    const std::string comment = "# deb [signed-by=/key] https://other.test stable main\ndeb [signed-by=/key] https://one.test stable main\n";
    auto disabled = repository_rewrite(comment, false, false);
    if (repository_rewrite(disabled, false, true) != comment) {
        std::cerr << "An unrelated commented source was changed\n";
        return 1;
    }
    auto check = [](bool condition, const char *message) {
        if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    };
    auto old = repository_inspect(disabled, false);
    check(old.valid && !old.enabled && old.can_enable, "Disabled scoped list source cannot be re-enabled");
    const std::string mixed = "deb [signed-by=/key] https://one.test stable main\n"
                              "deb https://two.test stable main\n";
    check(!repository_inspect(mixed, false).can_enable, "An unscoped source was accepted");
    check(!repository_inspect("deb [signed-by=/key trusted=yes] https://one.test stable main\n", false).can_enable,
          "List Trusted bypass was accepted");
    check(!repository_inspect("deb [signed-by=/key allow-insecure=yes] https://one.test stable main\n", false).can_enable,
          "Insecure list source was accepted");
    const std::string deb822 = "Types: deb\r\nURIs: https://one.test\r\nSuites: stable\r\nSigned-By: /key\r\n";
    auto off = repository_rewrite(deb822, true, false);
    check(off == "Types: deb\r\nEnabled: no\r\nURIs: https://one.test\r\nSuites: stable\r\nSigned-By: /key\r\n",
          "Missing Enabled insertion changed line endings");
    auto off_state = repository_inspect(off, true);
    check(off_state.valid && !off_state.enabled && off_state.can_enable, "Scoped deb822 source rejected");
    check(!repository_inspect(deb822 + "Trusted: yes\r\n", true).can_enable, "deb822 Trusted bypass was accepted");
    check(!repository_inspect(deb822 + "Allow-Insecure: yes\r\n", true).can_enable, "Insecure deb822 source was accepted");
    check(!repository_inspect("Types: deb\nEnabled: perhaps\nURIs: https://one.test\nSuites: stable\n", true).valid,
          "Malformed Enabled was accepted");
    check(!repository_inspect("Types: deb\nURIs: https://one.test\n", true).valid,
          "Missing source fields were accepted");
    check(!repository_inspect("deb [signed-by=/key] https://one.test stable main\n"
                              "# luxurystore-disabled deb [signed-by=/key] https://two.test stable main\n", false).valid,
          "Mixed on/off source file was accepted");
    const std::string no_newline = "Types: deb\nURIs: https://one.test\nSuites: stable";
    check(repository_rewrite(no_newline, true, false) ==
          "Types: deb\nEnabled: no\nURIs: https://one.test\nSuites: stable", "Final newline changed");
    const std::string types_last = "URIs: https://one.test\nSuites: stable\nTypes: deb";
    check(repository_rewrite(types_last, true, false) ==
          "URIs: https://one.test\nSuites: stable\nTypes: deb\nEnabled: no", "Insertion added a final newline");
    return 0;
}
