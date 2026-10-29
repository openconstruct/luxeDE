// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostic-common.hh"
#include <array>
#include <cmath>
#include <iomanip>
#include <regex>
static std::string xml(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '&')
      out += "&amp;";
    else if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else
      out += c;
  }
  return out;
}
int main(int argc, char **argv) {
  try {
    require(argc == 3, "Usage: import-themes LUCID_ROOT PINNED_CHECKOUT");
    fs::path root = argv[1], upstream = argv[2], out = root / "data/styles";
    std::vector<std::string> names = {"catppuccin-latte",
                                      "catppuccin-frappe",
                                      "catppuccin-macchiato",
                                      "catppuccin-mocha",
                                      "nord",
                                      "nord-light",
                                      "solarized-dark",
                                      "solarized-light",
                                      "gruvbox-dark-medium",
                                      "gruvbox-light-medium",
                                      "tokyo-night-dark",
                                      "tokyo-night-storm",
                                      "tokyo-night-light",
                                      "dracula",
                                      "onedark",
                                      "rose-pine",
                                      "rose-pine-moon",
                                      "rose-pine-dawn",
                                      "everforest-dark-medium",
                                      "everforest-light-medium"};
    auto revision =
        trim(command({"git", "-C", upstream.string(), "rev-parse", "HEAD"}));
    std::vector<std::pair<fs::path, std::string>> outputs;
    for (auto &name : names) {
      auto raw = read_file(upstream / "base16" / (name + ".yaml"));
      auto field = [&](const std::string &key) {
        std::smatch m;
        require(std::regex_search(
                    raw, m,
                    std::regex("^" + key + ":\\s*[\"']?(.*?)[\"']?\\s*$",
                               std::regex::multiline)),
                "Missing " + key + " in " + name);
        return m[1].str();
      };
      auto title = field("name"), author = field("author"),
           variant = field("variant");
      require(variant == "dark" || variant == "light", "Invalid variant");
      std::vector<std::pair<std::string, std::string>> colors;
      std::map<std::string, std::string> palette;
      std::regex pattern("(base[0-9A-Fa-f]{2}):\\s*[\"']?(#[0-9A-Fa-f]{6})");
      for (std::sregex_iterator i(raw.begin(), raw.end(), pattern), end;
           i != end; ++i) {
        auto key = (*i)[1].str(), value = (*i)[2].str();
        if (!palette.count(key))
          colors.emplace_back(key, value);
        else
          for (auto &p : colors)
            if (p.first == key)
              p.second = value;
        palette[key] = value;
      }
      auto rgb = [&](const std::string &key) {
        auto value = palette.at(key);
        return std::array<double, 3>{
            std::stoi(value.substr(1, 2), nullptr, 16) / 255.0,
            std::stoi(value.substr(3, 2), nullptr, 16) / 255.0,
            std::stoi(value.substr(5, 2), nullptr, 16) / 255.0};
      };
      auto luminance = [](std::array<double, 3> v) {
        for (auto &c : v)
          c = c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
        return v[0] * .2126 + v[1] * .7152 + v[2] * .0722;
      };
      auto background = luminance(rgb("base00"));
      auto comment = rgb("base04"), fg = rgb("base05"), blend = comment;
      for (int step = 0; step <= 100; step++) {
        for (int i = 0; i < 3; i++)
          blend[i] = comment[i] + (fg[i] - comment[i]) * (step / 100.0);
        auto lum = luminance(blend);
        if ((std::max(lum, background) + .05) /
                (std::min(lum, background) + .05) >=
            4.5)
          break;
      }
      std::ostringstream hex;
      hex << '#' << std::hex << std::setfill('0');
      for (auto c : blend)
        hex << std::setw(2) << static_cast<int>(std::nearbyint(c * 255));
      colors.emplace_back("comment", hex.str());
      std::string terminal;
      for (auto key :
           {"base00", "base08", "base0B", "base0A", "base0D", "base0E",
            "base0C", "base05", "base03", "base08", "base0B", "base0A",
            "base0D", "base0E", "base0C", "base07"}) {
        if (!terminal.empty())
          terminal += ' ';
        terminal += palette.at(key);
      }
      std::ostringstream text;
      text
          << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<style-scheme "
             "id=\"lucid-"
          << name << "\" _name=\"" << xml(title)
          << "\" version=\"1.0\">\n  <author>" << xml(author)
          << "</author>\n  <description>Lucid adaptation of the Tinted Theming "
             "palette.</description>\n  <metadata><property name=\"variant\">"
          << variant << "</property><property name=\"terminal-palette\">"
          << terminal << "</property></metadata>\n";
      for (auto &[key, value] : colors)
        text << "  <color name=\"" << key << "\" value=\"" << value << "\"/>\n";
      auto style = [&](const std::string &n, const std::string &foreground = "",
                       const std::string &bg = "", bool bold = false) {
        text << "  <style name=\"" << n << "\" ";
        bool spaced = false;
        auto attr = [&](const std::string &key, const std::string &value) {
          if (spaced)
            text << ' ';
          text << key << "=\"" << value << '"';
          spaced = true;
        };
        if (bold)
          attr("bold", "true");
        if (!foreground.empty())
          attr("foreground", foreground);
        if (!bg.empty())
          attr("background", bg);
        text << "/>\n";
      };
      style("text", "base05", "base00");
      style("selection", "base05", "base02");
      style("selection-unfocused", "base05", "base02");
      style("cursor", "base05");
      style("secondary-cursor", "base08");
      style("current-line", "", "base01");
      style("line-numbers", "base04", "base00");
      style("current-line-number", "base0D", "base01", true);
      style("right-margin", "base03", "base01");
      style("draw-spaces", "base03");
      style("search-match", "base00", "base0A");
      style("bracket-match", "base00", "base0C", true);
      style("bracket-mismatch", "base00", "base08");
      for (auto [n, c] :
           {std::pair{"comment", "comment"}, {"shebang", "comment"},
            {"doc-comment", "comment"},      {"string", "base0B"},
            {"special-char", "base0C"},      {"number", "base09"},
            {"floating-point", "base09"},    {"decimal", "base09"},
            {"base-n-integer", "base09"},    {"boolean", "base09"},
            {"constant", "base09"},          {"keyword", "base0E"},
            {"statement", "base0E"},         {"type", "base0A"},
            {"function", "base0D"},          {"identifier", "base05"},
            {"preprocessor", "base0C"},      {"operator", "base0C"},
            {"special-constant", "base08"},  {"note", "base0A"},
            {"net-address", "base0D"},       {"error", "base08"}})
        style(std::string("def:") + n, c);
      text << "</style-scheme>\n";
      outputs.emplace_back(out / (name + ".xml"), text.str());
    }
    auto license = read_file(upstream / "LICENSE");
    std::string sources =
        "# Lucid color schemes\n\n20 GtkSourceView adaptations generated by "
        "tools/import-themes.sh.\n\nPalettes: "
        "https://github.com/tinted-theming/schemes\nRevision: " +
        revision +
        "\nLicense: MIT (see LICENSE-tinted-themes.txt). Individual palette "
        "authors are credited in each XML.\n\n";
    for (auto &name : names)
      sources += "- " + name + "\n";
    fs::create_directories(out);
    for (auto &[path, text] : outputs)
      write_file(path, text);
    write_file(out / "LICENSE-tinted-themes.txt", license);
    write_file(out / "SOURCES.md", sources);
    std::cout << "Generated " << names.size() << " schemes from " << revision
              << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
