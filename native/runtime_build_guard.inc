#include "anyapi_build_v1.h"
namespace build_runtime {
static std::mutex mutex;
static AnyBuildInfoV1 info;
static bool copy(AnyBuildInfoV1* out) {
    if (!out || out->struct_size != sizeof(*out) || out->version != 1) return false;
    std::lock_guard lock(mutex); *out = info; return info.status != ANY_BUILD_UNCHECKED;
}
static const AnyBuildV1 api{sizeof(AnyBuildV1), 1, copy};
}
static const AnyBuildV1* anyapi_build_service() { return &build_runtime::api; }
static std::string p27_file_sha256(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD bytes=0,object_size=0;std::string result;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&object_size,sizeof(object_size),&bytes,0)>=0) {
        std::vector<unsigned char> object(object_size),buffer(1u<<20);unsigned char digest[32]{};
        if(BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0)>=0) {
            std::ifstream input(path,std::ios::binary);bool ok=input.is_open();
            while(ok && input) {
                input.read((char*)buffer.data(),(std::streamsize)buffer.size());auto count=input.gcount();
                if(count>0 && BCryptHashData(hash,buffer.data(),(ULONG)count,0)<0) ok=false;
            }
            if(input.bad()) ok=false;
            if(ok && BCryptFinishHash(hash,digest,sizeof(digest),0)>=0) {
                char encoded[65]{};for(size_t i=0;i<32;++i) std::snprintf(encoded+i*2,3,"%02x",digest[i]);result=encoded;
            }
            BCryptDestroyHash(hash);hash=nullptr;
        }
    }
    BCryptCloseAlgorithmProvider(algorithm,0);return result;
}
static bool p27_build_matches() {
    auto exe=p27_file_sha256(g_game_dir/"game.exe");auto gcl=p27_file_sha256(g_game_dir/"bin"/"game.gcl");
    bool ok=exe==P27_EXPECTED_EXE_SHA256 && gcl==P27_EXPECTED_GCL_SHA256;
    {
        std::lock_guard lock(build_runtime::mutex);
        auto& info = build_runtime::info;
        info.api_revision = 36;
        info.status = ok ? ANY_BUILD_MATCH : ANY_BUILD_MISMATCH;
        strcpy_s(info.api_version, "0.35.0"); strcpy_s(info.game_version, "0.1.24");
        strcpy_s(info.steam_build, "25826614");
        strcpy_s(info.executable_sha256, exe.c_str()); strcpy_s(info.game_data_sha256, gcl.c_str());
    }
    log_line(ok?ANY_LOG_INFO:ANY_LOG_ERROR,"framework",("PHASE27_BUILD_GUARD status="+std::string(ok?"MATCH":"MISMATCH_HOOKS_DISABLED")+" exe_sha256="+exe+" gcl_sha256="+gcl).c_str());return ok;
}
