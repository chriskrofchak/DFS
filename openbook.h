#ifndef OPENBOOK_H
#define OPENBOOK_H

#include <string>
#include <unordered_map>

struct fd_pair {
    int cli_fd, ser_fd;
    uint64_t cli_flags, ser_flags;
    fd_pair(int cfd, int sfd, uint64_t cfl, uint64_t sfl) : 
        cli_fd(cfd),
        ser_fd(sfd),
        cli_flags(cfl),
        ser_flags(sfl)
    {}
};

class OpenBook {
    // map for filename, client file descriptor fd, and client flags.
    std::string cache_path_;
    time_t t; // this is little t
    std::unordered_map<std::string, fd_pair> open_files;
    std::unordered_map<std::string, time_t> Tc;
    std::unordered_map<int, int> local_fd;
    std::unordered_map<int, int> server_fd;
public:
    OpenBook(const char* path, time_t cache_int) : 
        cache_path_(std::string(path)),
        t(cache_int),
        open_files(),
        Tc() {}
    ~OpenBook() {}

    // functions
    int OB_open(std::string filename, int cli_fd, uint64_t cli_flags, int ser_fd, uint64_t ser_flags);
    int OB_close(std::string filename);
    fd_pair get_fd_pair(std::string filename);
    int get_local_fd(std::string filename);
    int get_server_fd(std::string filename);
    void set_cli_fd(std::string filename, int fd);
    std::string get_cache();
    time_t get_interval();
    time_t get_last_validated(std::string filename);
    void set_validate(std::string filename, time_t new_Tc);
    bool is_open(const char *path);
};

#endif