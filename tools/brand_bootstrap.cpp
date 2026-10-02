// Copy only icon/version resources into a build-local copy of the matching CEF
// bootstrap. Its entry point, sandbox startup and executable sections are not
// rebuilt. tests/branding-resources.mjs independently verifies that boundary.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

struct Resource {
  WORD type{}, language{};
  bool numeric{};
  WORD id{};
  std::wstring name;
  std::vector<unsigned char> bytes;
  LPCWSTR key() const { return numeric ? MAKEINTRESOURCEW(id) : name.c_str(); }
};
struct Scan { std::vector<Resource> entries; bool failed=false; };
BOOL CALLBACK language(HMODULE module,LPCWSTR type,LPCWSTR name,WORD lang,LONG_PTR parameter) {
  auto& scan=*reinterpret_cast<Scan*>(parameter);
  const auto resource=FindResourceExW(module,type,name,lang);
  const auto size=resource?SizeofResource(module,resource):0;
  const auto loaded=resource?LoadResource(module,resource):nullptr;
  const auto data=loaded?static_cast<const unsigned char*>(LockResource(loaded)):nullptr;
  if(!data||!size){scan.failed=true;return FALSE;}
  Resource item;item.type=static_cast<WORD>(reinterpret_cast<ULONG_PTR>(type));item.language=lang;
  item.numeric=IS_INTRESOURCE(name);if(item.numeric)item.id=static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));else item.name=name;
  item.bytes.assign(data,data+size);scan.entries.push_back(std::move(item));return TRUE;
}
BOOL CALLBACK name(HMODULE module,LPCWSTR type,LPWSTR key,LONG_PTR parameter) {
  if(!EnumResourceLanguagesW(module,type,key,language,parameter))reinterpret_cast<Scan*>(parameter)->failed=true;
  return !reinterpret_cast<Scan*>(parameter)->failed;
}
Scan scan(const std::filesystem::path& path) {
  const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  if(!module)throw std::runtime_error("Cannot read build resource module");
  Scan result;
  for(const auto type:{RT_ICON,RT_GROUP_ICON,RT_VERSION}){
    SetLastError(ERROR_SUCCESS);
    if(!EnumResourceNamesW(module,type,name,reinterpret_cast<LONG_PTR>(&result))){
      const auto error=GetLastError();
      if(error!=ERROR_RESOURCE_TYPE_NOT_FOUND&&error!=ERROR_RESOURCE_DATA_NOT_FOUND)result.failed=true;
    }
  }
  FreeLibrary(module);if(result.failed)throw std::runtime_error("Cannot enumerate build resources");return result;
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc!=4)throw std::runtime_error("Usage: xenon_brand_bootstrap original-bootstrap target-exe source-dll");
    const auto original=std::filesystem::weakly_canonical(argv[1]);
    const auto target=std::filesystem::weakly_canonical(argv[2]);
    const auto source=std::filesystem::weakly_canonical(argv[3]);
    if(!std::filesystem::is_regular_file(original)||!std::filesystem::is_regular_file(target)||!std::filesystem::is_regular_file(source)||
       original==target||source==target||std::filesystem::equivalent(original,target)||std::filesystem::equivalent(source,target)||
       target.extension()!=L".exe"||source.extension()!=L".dll")throw std::runtime_error("Invalid resource branding paths");
    auto resources=scan(source);const auto existing=scan(target);
    bool group=false,version=false;
    for(auto& resource:resources.entries){
      group|=resource.type==14&&resource.numeric&&resource.id==101;
      version|=resource.type==16;
      if(resource.type==16){
        // DLL and EXE suffixes have the same length; keep the version block
        // layout intact while correctly identifying this bootstrap executable.
        auto* words=reinterpret_cast<wchar_t*>(resource.bytes.data());
        const auto count=resource.bytes.size()/sizeof(wchar_t);
        for(size_t i=0;i+4<=count;++i)if(words[i]==L'.'&&words[i+1]==L'd'&&words[i+2]==L'l'&&words[i+3]==L'l'){
          words[i+1]=L'e';words[i+2]=L'x';words[i+3]=L'e';
        }
        // VS_FIXEDFILEINFO.dwFileType follows signature + eight DWORD fields.
        for(size_t i=0;i+sizeof(VS_FIXEDFILEINFO)<=resource.bytes.size();i+=4){
          auto* info=reinterpret_cast<VS_FIXEDFILEINFO*>(resource.bytes.data()+i);
          if(info->dwSignature==VS_FFI_SIGNATURE){info->dwFileType=VFT_APP;break;}
        }
      }
    }
    if(!group||!version)throw std::runtime_error("Xenon icon or version resource is missing");
    const auto update=BeginUpdateResourceW(target.c_str(),FALSE);
    if(!update)throw std::runtime_error("Cannot open build-local bootstrap resources");
    bool okay=true;
    for(const auto& resource:existing.entries)okay=UpdateResourceW(update,MAKEINTRESOURCEW(resource.type),resource.key(),resource.language,nullptr,0)&&okay;
    for(const auto& resource:resources.entries)okay=UpdateResourceW(update,MAKEINTRESOURCEW(resource.type),resource.key(),resource.language,const_cast<unsigned char*>(resource.bytes.data()),static_cast<DWORD>(resource.bytes.size()))&&okay;
    if(!EndUpdateResourceW(update,!okay)||!okay)throw std::runtime_error("Could not commit bootstrap branding resources");
    std::wcout<<L"Branded copied bootstrap resources: "<<target.filename().wstring()<<L"\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
