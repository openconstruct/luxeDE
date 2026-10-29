// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include <archive.h>
#include <archive_entry.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
namespace fs = std::filesystem;
struct Process {
  pid_t pid = -1; int fd = -1;
  explicit Process(std::vector<std::string> args) {
    int pipefd[2]; if (pipe(pipefd)) throw std::runtime_error("pipe failed");
    pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); throw std::runtime_error("fork failed"); }
    if (!pid) {
      close(pipefd[0]); dup2(pipefd[1], STDOUT_FILENO); close(pipefd[1]);
      std::vector<char*> raw; for (auto& arg : args) raw.push_back(arg.data()); raw.push_back(nullptr);
      execvp(raw[0], raw.data()); perror("execvp"); _exit(127);
    }
    close(pipefd[1]); fd = pipefd[0];
  }
  int finish() {
    if (fd >= 0) { close(fd); fd = -1; }
    int status = 0; pid_t got;
    do { got = waitpid(pid, &status, 0); } while (got < 0 && errno == EINTR);
    pid = -1; return got > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 128;
  }
  ~Process() { if (pid > 0) finish(); }
};
static std::pair<int,std::string> run(std::vector<std::string> args) {
  Process process(std::move(args)); std::string out; char buf[8192]; ssize_t n;
  while ((n=read(process.fd,buf,sizeof buf)) != 0) {
    if (n < 0) { if(errno == EINTR) continue; throw std::runtime_error("read failed"); }
    out.append(buf,n);
  }
  return {process.finish(),out};
}
static std::string output(std::vector<std::string> args) {
  auto result = run(args); if(result.first) throw std::runtime_error(args[0]+" failed: "+result.second);
  while (!result.second.empty() && (result.second.back()=='\n' || result.second.back()=='\r')) result.second.pop_back();
  return result.second;
}
static std::string hash(const fs::path& p) { auto s=output({"sha256sum","--zero","--",p.string()}); if(s.size()<64) throw std::runtime_error("Invalid SHA256 output"); return s.substr(0,64); }
static std::string field(const fs::path& p,const char* f) { return output({"dpkg-deb","-f",p.string(),f}); }
static std::string normalize(const char* raw) {
  if (!raw) throw std::runtime_error("Missing archive pathname");
  fs::path p(raw); if(p.is_absolute()) throw std::runtime_error("Unsafe absolute payload path: "+p.string());
  for (auto& part:p) if(part=="..") throw std::runtime_error("Unsafe parent payload path: "+p.string());
  auto name=p.lexically_normal().generic_string(); while(name.size()>1 && name.back()=='/') name.pop_back();
  return name;
}
template<class F> static void payload(const fs::path& p,F inspect) {
  Process process({"dpkg-deb","--fsys-tarfile",p.string()});
  auto a=archive_read_new(); if(!a) throw std::runtime_error("archive allocation failed");
  try {
    archive_read_support_filter_all(a); archive_read_support_format_tar(a);
    if(archive_read_open_fd(a,process.fd,65536)!=ARCHIVE_OK) throw std::runtime_error(archive_error_string(a));
    archive_entry* entry=nullptr; int status;
    while ((status=archive_read_next_header(a,&entry))==ARCHIVE_OK) inspect(entry);
    if(status!=ARCHIVE_EOF) throw std::runtime_error(archive_error_string(a));
    // Drain trailing tar blocks before waiting for dpkg-deb.
    char buffer[8192]; while(read(process.fd,buffer,sizeof buffer)>0) {}
    archive_read_free(a); a=nullptr;
    if(process.finish()) throw std::runtime_error("dpkg-deb failed: "+p.string());
  } catch(...) { if(a) archive_read_free(a); throw; }
}
static void audit(const fs::path& p,const std::string& expected) {
  if(!fs::is_regular_file(p)) throw std::runtime_error("Not a DEB file: "+p.string());
  if(field(p,"Package")!=expected) throw std::runtime_error("Unexpected package name: "+p.string());
  std::vector<std::string> errors;
  payload(p,[&](archive_entry* e) {
    auto name=normalize(archive_entry_pathname(e)); if(name=="." || name.empty()) return;
    if(name=="usr/local" || name.rfind("usr/local/",0)==0) errors.push_back("Forbidden /usr/local payload: "+name);
    if(archive_entry_uid(e)!=0 || archive_entry_gid(e)!=0) errors.push_back("Non-root ownership: "+name);
    auto mode=archive_entry_perm(e)&07777; auto type=archive_entry_filetype(e);
    if(type==AE_IFDIR && mode!=0755) errors.push_back("Directory mode must be 0755: "+name);
    if(type==AE_IFREG && !archive_entry_hardlink(e) && mode!=0644 && mode!=0755) errors.push_back("Regular-file mode must be 0644 or 0755: "+name);
  });
  if(!errors.empty()) { std::string message; for(size_t i=0;i<std::min<size_t>(50,errors.size());++i) message+=errors[i]+"\n"; throw std::runtime_error(message); }
  std::cout<<"DEB payload audit passed: "<<p.filename().string()<<" ("<<expected<<")\n";
}
static void ownership(const std::vector<fs::path>& paths) {
  if(paths.empty()) throw std::runtime_error("At least one DEB is required");
  std::map<std::string,std::vector<std::string>> owners;
  for(auto& p:paths) payload(p,[&](archive_entry* e) { if(archive_entry_filetype(e)!=AE_IFDIR) owners[normalize(archive_entry_pathname(e))].push_back(p.filename().string()); });
  bool conflicts=false;
  for(auto& [name,packages]:owners) if(packages.size()>1) { conflicts=true; std::cerr<<name<<":"; for(auto& p:packages) std::cerr<<" "<<p; std::cerr<<'\n'; }
  if(conflicts) throw std::runtime_error("Cross-package ownership conflicts");
  std::cout<<"PASS: "<<paths.size()<<" DEBs have no cross-package file ownership conflicts\n";
}
struct Package { std::string name,version,architecture,digest; fs::path path; };
static Package package(const fs::path& path) {
  if(fs::is_symlink(path) || !fs::is_regular_file(path)) throw std::runtime_error("Not a regular DEB: "+path.string());
  return {field(path,"Package"),field(path,"Version"),field(path,"Architecture"),hash(path),path};
}
static bool newer(const Package& a,const Package& b) {
  if(a.version==b.version) return false;
  auto status=run({"dpkg","--compare-versions",a.version,"gt",b.version}).first;
  if(status>1) throw std::runtime_error("Invalid Debian version");
  return status==0;
}
static std::string json(const std::string& s) {
  std::ostringstream out; out<<'"';
  for(unsigned char c:s) {
    if(c=='"' || c=='\\') out<<'\\'<<c;
    else if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c);
    else out<<c;
  }
  out<<'"'; return out.str();
}
struct Temporary {
  fs::path path;
  Temporary(const fs::path& parent,const char* prefix) {
    std::string name=(parent/(std::string(prefix)+"XXXXXX")).string();
    if(!mkdtemp(name.data())) throw std::runtime_error("Cannot create temporary directory");
    path=name;
  }
  ~Temporary() { std::error_code ec; fs::remove_all(path,ec); }
};
static void write(const fs::path& p,const std::string& text) {
  std::ofstream out(p); out<<text; out.close(); if(!out) throw std::runtime_error("Cannot write "+p.string());
}
static void organize(int argc,char** argv) {
  fs::path root; std::vector<fs::path> incoming; bool rotate=false,dry=false,keep=true;
  for(int i=2;i<argc;++i) {
    std::string arg=argv[i];
    if(arg=="--root" && i+1<argc) root=fs::absolute(argv[++i]).lexically_normal();
    else if(arg=="--incoming") { while(i+1<argc && std::string(argv[i+1]).rfind("--",0)!=0) incoming.emplace_back(fs::absolute(argv[++i])); }
    else if(arg=="--rotate-old") rotate=true;
    else if(arg=="--dry-run") dry=true;
    else if(arg=="--keep-old" && i+1<argc) { std::string v=argv[++i]; if(v!="0" && v!="1") throw std::runtime_error("--keep-old requires 0 or 1"); keep=v=="1"; }
    else throw std::runtime_error("Unknown or incomplete argument: "+arg);
  }
  if(root.empty()) throw std::runtime_error("--root is required");
  fs::create_directories(root); root=fs::canonical(root);
  std::vector<fs::path> candidates;
  for(auto dir:{"debs","old","test"}) if(fs::is_directory(root/dir)) {
    std::vector<fs::path> paths;
    for(auto& entry:fs::recursive_directory_iterator(root/dir)) if(entry.path().extension()==".deb") paths.push_back(entry.path());
    std::sort(paths.begin(),paths.end()); candidates.insert(candidates.end(),paths.begin(),paths.end());
  }
  candidates.insert(candidates.end(),incoming.begin(),incoming.end());
  std::map<std::string,std::map<std::string,Package>> groups;
  for(auto& path:candidates) {
    auto p=package(path); auto& versions=groups[p.name]; auto found=versions.find(p.version);
    if(found!=versions.end()) {
      if(found->second.digest!=p.digest || found->second.architecture!=p.architecture) throw std::runtime_error("Conflicting DEBs for "+p.name+" "+p.version);
    } else versions.emplace(p.version,p);
  }
  if(groups.empty()) throw std::runtime_error("No DEBs found");
  std::map<std::string,std::vector<Package>> selected;
  for(auto& [name,versions]:groups) {
    std::vector<Package> packages;
    for(auto& [version,p]:versions) {
      if(!packages.empty() && packages[0].architecture!=p.architecture) throw std::runtime_error("Multiple architectures for "+name);
      packages.push_back(p);
    }
    std::sort(packages.begin(),packages.end(),newer);
    auto& chosen=selected[name]; chosen.push_back(packages[0]);
    if(keep && rotate && packages.size()>1) chosen.push_back(packages[1]);
    if(keep && !rotate) for(auto dir:{"old","debs"}) {
      auto found=std::find_if(packages.begin(),packages.end(),[&](const Package& p){return p.path.parent_path()==root/dir;});
      if(found!=packages.end()) { chosen.push_back(*found); break; }
    }
    std::cout<<name<<": debs="<<chosen[0].version<<", old="<<(chosen.size()>1?chosen[1].version:"none")<<'\n';
  }
  if(dry) return;
  for(auto& [name,versions]:selected) for(auto& p:versions) audit(p.path,name);
  Temporary stage(root,".organize-");
  std::string manifest="{\n  \"format\": 1,\n  \"builder\": \"CMake/CPack\",\n  \"packages\": [";
  bool first=true; size_t previous=0;
  for(int index=0;index<2;++index) {
    auto dest=stage.path/(index?"old":"debs"); fs::create_directory(dest); fs::permissions(dest,fs::perms(0755));
    std::string sums;
    for(auto& [name,versions]:selected) {
      if(versions.size()<=size_t(index)) continue;
      auto& p=versions[index]; auto target=dest/p.path.filename();
      if(fs::exists(target)) throw std::runtime_error("Duplicate output filename");
      fs::copy_file(p.path,target); fs::permissions(target,fs::perms(0644));
      if(hash(target)!=p.digest) throw std::runtime_error("Copy verification failed");
      sums+=p.digest+"  "+target.filename().string()+"\n";
      if(index==0) { if(!first) manifest+=","; first=false; manifest+="\n    {\"file\": "+json("debs/"+target.filename().string())+", \"sha256\": "+json(p.digest)+"}"; }
      else ++previous;
    }
    write(dest/"SHA256SUMS",sums);
  }
  write(stage.path/"manifest.json",manifest+"\n  ]\n}\n");
  Temporary backup(root,".previous-"); std::vector<std::string> preserved,activated;
  try {
    for(auto name:{"debs","old","manifest.json"}) if(fs::exists(root/name)) { fs::rename(root/name,backup.path/name); preserved.emplace_back(name); }
    for(auto name:{"debs","old","manifest.json"}) { fs::rename(stage.path/name,root/name); activated.emplace_back(name); }
  } catch(...) {
    // Keep the backup if recovery itself fails.
    auto backup_path=backup.path; backup.path.clear();
    for(auto it=activated.rbegin();it!=activated.rend();++it) fs::remove_all(root/ *it);
    for(auto it=preserved.rbegin();it!=preserved.rend();++it) fs::rename(backup_path/ *it,root/ *it);
    fs::remove(backup_path); throw;
  }
  for(auto name:{"test","older"}) if(fs::is_directory(root/name)) fs::remove_all(root/name);
  std::cout<<"Organized "<<selected.size()<<" current DEBs and "<<previous<<" previous versions\n";
}
int main(int argc,char** argv) {
  try {
    if(argc<2) throw std::runtime_error("Expected audit, ownership, or organize");
    std::string mode=argv[1];
    if(mode=="audit" && argc==4) audit(fs::absolute(argv[2]),argv[3]);
    else if(mode=="ownership") { std::vector<fs::path> paths; for(int i=2;i<argc;++i) paths.emplace_back(fs::absolute(argv[i])); ownership(paths); }
    else if(mode=="organize") organize(argc,argv);
    else throw std::runtime_error("Invalid command or arguments");
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
