// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
namespace fs = std::filesystem;
static std::string read(const fs::path& path) {
  std::ifstream file(path,std::ios::binary);
  if(!file) throw std::runtime_error("Cannot read "+path.string());
  std::string data((std::istreambuf_iterator<char>(file)),{});
  if(file.bad()) throw std::runtime_error("Read failed: "+path.string());
  return data;
}
int main(int argc,char** argv) {
  try {
    if(argc!=2) throw std::runtime_error("Usage: deduplicate-icons GENERATED_SHARE_ICONS");
    auto root=fs::canonical(argv[1]);
    if(root.filename()!="icons" || root.parent_path().filename()!="share") throw std::runtime_error("Expected generated share/icons directory");
    auto generated = root.parent_path().parent_path().parent_path();
    auto marker = generated / ".luxe-icons-generated";
    if (fs::is_symlink(marker) || !fs::is_regular_file(marker) ||
        fs::file_size(marker) != 24 || read(marker) != "luxe-icons-generated-v1\n")
      throw std::runtime_error("Refusing to deduplicate an unowned icon tree");
    for(auto theme:{"Luxe","Luxe-Light","Luxe-Dark"})
      if(!fs::is_regular_file(root/theme/"index.theme")) throw std::runtime_error("Missing theme index");
    // Hashes only locate candidates; full byte equality decides whether to link.
    // Buckets retain unequal collisions so no artwork can be lost to a collision.
    std::map<std::pair<size_t,size_t>,std::vector<fs::path>> canonical;
    size_t count=0,saved=0;
    for(auto theme:{"Luxe","Luxe-Dark","Luxe-Light","hicolor"}) {
      if(!fs::exists(root/theme)) continue;
      std::vector<fs::path> paths;
      for(auto& entry:fs::recursive_directory_iterator(root/theme))
        if(entry.path().extension()==".svg" && !entry.is_symlink() && entry.is_regular_file()) paths.push_back(entry.path());
      std::sort(paths.begin(),paths.end());
      for(auto& path:paths) {
        auto content=read(path);
        auto& candidates=canonical[{content.size(),std::hash<std::string>{}(content)}];
        auto target=std::find_if(candidates.begin(),candidates.end(),[&](const fs::path& p){return read(p)==content;});
        if(target==candidates.end()) { candidates.push_back(path); continue; }
        auto link=target->lexically_relative(path.parent_path());
        // Stage the symlink before replacing the original regular file.
        auto staging=path; staging+=".luxe-deduplicate-link";
        if(fs::exists(staging) || fs::is_symlink(staging)) throw std::runtime_error("Unexpected staging path: "+staging.string());
        fs::create_symlink(link,staging);
        try { fs::rename(staging,path); } catch(...) { fs::remove(staging); throw; }
        saved+=content.size(); ++count;
      }
    }
    std::cout<<"Linked "<<count<<" identical SVGs; saved "<<saved<<" regular-file content bytes\n";
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
